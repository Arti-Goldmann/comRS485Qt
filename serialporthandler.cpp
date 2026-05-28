#include "serialporthandler.h"
#include <QDebug>

SerialPortHandler::SerialPortHandler(QObject *parent)
    : QObject(parent)
    , m_serialPort(new QSerialPort(this))
    , m_isConnected(false)
    , m_readTimer(new QTimer(this))
{
    connect(m_serialPort, &QSerialPort::readyRead, this, &SerialPortHandler::handleReadyRead);
    connect(m_serialPort, QOverload<QSerialPort::SerialPortError>::of(&QSerialPort::errorOccurred),
            this, &SerialPortHandler::handleError);
    
    m_readTimer->setSingleShot(true);
    m_readTimer->setInterval(50); // 50ms таймаут для сбора данных
    connect(m_readTimer, &QTimer::timeout, [this]() {
        if (!m_readBuffer.isEmpty()) {
            emit dataReceived(m_readBuffer);
            m_readBuffer.clear();
        }
    });
}

SerialPortHandler::~SerialPortHandler()
{
    if (m_serialPort && m_serialPort->isOpen()) {
        m_serialPort->close();
    }
}

bool SerialPortHandler::open(const Settings &settings)
{
    if (m_serialPort->isOpen()) {
        close();
    }
    
    m_currentSettings = settings;
    
    m_serialPort->setPortName(settings.portName);
    m_serialPort->setBaudRate(settings.baudRate);
    m_serialPort->setDataBits(settings.dataBits);
    m_serialPort->setParity(settings.parity);
    m_serialPort->setStopBits(settings.stopBits);
    m_serialPort->setFlowControl(settings.flowControl);
    
    if (m_serialPort->open(QIODevice::ReadWrite)) {
        m_isConnected = true;
        emit connectionStatusChanged(true);
        qDebug() << "Порт открыт:" << settings.portName;
        return true;
    } else {
        m_isConnected = false;
        emit connectionStatusChanged(false);
        qDebug() << "Ошибка открытия порта:" << m_serialPort->errorString();
        return false;
    }
}

void SerialPortHandler::close()
{
    if (m_serialPort && m_serialPort->isOpen()) {
        // Отправляем оставшиеся данные из буфера
        if (!m_readBuffer.isEmpty()) {
            emit dataReceived(m_readBuffer);
            m_readBuffer.clear();
        }
        m_readTimer->stop();
        
        m_serialPort->close();
        m_isConnected = false;
        emit connectionStatusChanged(false);
        qDebug() << "Порт закрыт";
    }
}

bool SerialPortHandler::isOpen() const
{
    return m_serialPort && m_serialPort->isOpen();
}

bool SerialPortHandler::write(const QByteArray &data)
{
    if (!m_serialPort || !m_serialPort->isOpen()) {
        return false;
    }
    
    qint64 bytesWritten = m_serialPort->write(data);
    if (bytesWritten == -1) {
        qDebug() << "Ошибка записи:" << m_serialPort->errorString();
        return false;
    }
    
    bool result = m_serialPort->waitForBytesWritten(3000);
    if (!result) {
        qDebug() << "Таймаут записи:" << m_serialPort->errorString();
        return false;
    }
    
    qDebug() << "Отправлено байт:" << bytesWritten;
    return true;
}

QString SerialPortHandler::errorString() const
{
    return m_serialPort ? m_serialPort->errorString() : "Порт не инициализирован";
}

QList<QString> SerialPortHandler::availablePorts()
{
    QList<QString> portNames;
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    
    for (const QSerialPortInfo &port : ports) {
        portNames.append(port.portName());
    }
    
    return portNames;
}

void SerialPortHandler::handleReadyRead()
{
    if (!m_serialPort) {
        return;
    }
    
    QByteArray data = m_serialPort->readAll();
    if (!data.isEmpty()) {
        m_readBuffer.append(data);
        
        // Перезапускаем таймер для сбора данных
        m_readTimer->start();
    }
}

void SerialPortHandler::handleError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::ResourceError) {
        emit errorOccurred("Устройство было отключено");
        close();
    } else if (error != QSerialPort::NoError) {
        emit errorOccurred(m_serialPort->errorString());
    }
}