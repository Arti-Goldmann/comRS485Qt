#ifndef TCPCLIENTHANDLER_H
#define TCPCLIENTHANDLER_H

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QByteArray>
#include <QString>

// Транспорт для Modbus TCP. По интерфейсу аналогичен SerialPortHandler:
// те же сигналы (dataReceived / errorOccurred / connectionStatusChanged)
// и методы (open / close / isOpen / write), чтобы MainWindow мог работать
// с обоими транспортами единообразно.
class TcpClientHandler : public QObject
{
    Q_OBJECT

public:
    struct Settings {
        QString host = "192.168.1.31";
        quint16 port = 502; // стандартный порт Modbus TCP
        int connectTimeoutMs = 3000;
    };

    explicit TcpClientHandler(QObject *parent = nullptr);
    ~TcpClientHandler();

    bool open(const Settings &settings);
    void close();
    bool isOpen() const;

    bool write(const QByteArray &data);
    QString errorString() const;

signals:
    void dataReceived(const QByteArray &data);
    void errorOccurred(const QString &error);
    void connectionStatusChanged(bool connected);

private slots:
    void handleReadyRead();
    void handleError(QAbstractSocket::SocketError error);
    void handleDisconnected();

private:
    QTcpSocket *m_socket;
    QByteArray m_readBuffer;
    Settings m_currentSettings;
    bool m_isConnected;
    QTimer *m_readTimer;
};

#endif // TCPCLIENTHANDLER_H
