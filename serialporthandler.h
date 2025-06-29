#ifndef SERIALPORTHANDLER_H
#define SERIALPORTHANDLER_H

#include <QObject>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTimer>
#include <QByteArray>

class SerialPortHandler : public QObject
{
    Q_OBJECT

public:
    struct Settings {
        QString portName;
        qint32 baudRate = 9600;
        QSerialPort::DataBits dataBits = QSerialPort::Data8;
        QSerialPort::Parity parity = QSerialPort::NoParity;
        QSerialPort::StopBits stopBits = QSerialPort::OneStop;
        QSerialPort::FlowControl flowControl = QSerialPort::NoFlowControl;
    };

    explicit SerialPortHandler(QObject *parent = nullptr);
    ~SerialPortHandler();

    bool open(const Settings &settings);
    void close();
    bool isOpen() const;
    
    bool write(const QByteArray &data);
    QString errorString() const;
    
    static QList<QString> availablePorts();

signals:
    void dataReceived(const QByteArray &data);
    void errorOccurred(const QString &error);
    void connectionStatusChanged(bool connected);

private slots:
    void handleReadyRead();
    void handleError(QSerialPort::SerialPortError error);

private:
    QSerialPort *m_serialPort;
    QByteArray m_readBuffer;
    Settings m_currentSettings;
    bool m_isConnected;
};

#endif // SERIALPORTHANDLER_H