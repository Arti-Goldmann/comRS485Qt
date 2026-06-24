#include "tcpclienthandler.h"
#include <QDebug>

TcpClientHandler::TcpClientHandler(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_isConnected(false)
    , m_readTimer(new QTimer(this))
{
    connect(m_socket, &QTcpSocket::readyRead, this, &TcpClientHandler::handleReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &TcpClientHandler::handleDisconnected);
    connect(m_socket, &QAbstractSocket::errorOccurred, this, &TcpClientHandler::handleError);

    m_readTimer->setSingleShot(true);
    m_readTimer->setInterval(50); // 50ms таймаут для сбора данных (как у serial)
    connect(m_readTimer, &QTimer::timeout, [this]() {
        if (!m_readBuffer.isEmpty()) {
            emit dataReceived(m_readBuffer);
            m_readBuffer.clear();
        }
    });
}

TcpClientHandler::~TcpClientHandler()
{
    if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();
    }
}

bool TcpClientHandler::open(const Settings &settings)
{
    if (isOpen()) {
        close();
    }

    m_currentSettings = settings;

    m_socket->connectToHost(settings.host, settings.port);
    if (!m_socket->waitForConnected(settings.connectTimeoutMs)) {
        m_isConnected = false;
        emit connectionStatusChanged(false);
        qDebug() << "Ошибка подключения к" << settings.host << settings.port
                 << ":" << m_socket->errorString();
        return false;
    }

    m_isConnected = true;
    emit connectionStatusChanged(true);
    qDebug() << "Соединение установлено:" << settings.host << ":" << settings.port;
    return true;
}

void TcpClientHandler::close()
{
    if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState) {
        // Отправляем оставшиеся данные из буфера
        if (!m_readBuffer.isEmpty()) {
            emit dataReceived(m_readBuffer);
            m_readBuffer.clear();
        }
        m_readTimer->stop();

        m_socket->disconnectFromHost();
        if (m_socket->state() != QAbstractSocket::UnconnectedState) {
            m_socket->waitForDisconnected(1000);
        }
    }

    if (m_isConnected) {
        m_isConnected = false;
        emit connectionStatusChanged(false);
        qDebug() << "Соединение закрыто";
    }
}

bool TcpClientHandler::isOpen() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

bool TcpClientHandler::write(const QByteArray &data)
{
    if (!isOpen()) {
        return false;
    }

    qint64 bytesWritten = m_socket->write(data);
    if (bytesWritten == -1) {
        qDebug() << "Ошибка записи:" << m_socket->errorString();
        return false;
    }

    if (!m_socket->waitForBytesWritten(3000)) {
        qDebug() << "Таймаут записи:" << m_socket->errorString();
        return false;
    }

    qDebug() << "Отправлено байт:" << bytesWritten;
    return true;
}

QString TcpClientHandler::errorString() const
{
    return m_socket ? m_socket->errorString() : "Сокет не инициализирован";
}

void TcpClientHandler::handleReadyRead()
{
    if (!m_socket) {
        return;
    }

    QByteArray data = m_socket->readAll();
    if (!data.isEmpty()) {
        m_readBuffer.append(data);
        // Перезапускаем таймер для сбора данных
        m_readTimer->start();
    }
}

void TcpClientHandler::handleDisconnected()
{
    m_readTimer->stop();
    if (!m_readBuffer.isEmpty()) {
        emit dataReceived(m_readBuffer);
        m_readBuffer.clear();
    }
    if (m_isConnected) {
        m_isConnected = false;
        emit connectionStatusChanged(false);
        qDebug() << "Удалённый узел закрыл соединение";
    }
}

void TcpClientHandler::handleError(QAbstractSocket::SocketError error)
{
    // RemoteHostClosedError штатно обрабатывается через сигнал disconnected.
    if (error == QAbstractSocket::RemoteHostClosedError) {
        return;
    }
    emit errorOccurred(m_socket->errorString());
}
