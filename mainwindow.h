#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QTimer>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QMessageBox>
#include <QTabWidget>
#include <QRadioButton>
#include <QButtonGroup>

class SerialPortHandler;
class TcpClientHandler;
class ModbusRTU;
class ModbusTCP;
class NetworkConfigurator;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void connectToPort();
    void disconnectFromPort();
    void sendData();
    void clearData();
    void onDataReceived(const QByteArray &data);
    void onPortError(const QString &error);
    void refreshPorts();
    void sendModbusRequest();
    void onTransportChanged();

private:
    enum Transport { TransportSerial, TransportTcp };

    void setupUI();
    void setupConnections();
    void updateConnectionStatus(bool connected);
    void appendRawData(const QByteArray &data, const QString &timestamp);

    // Текущий выбранный транспорт (по состоянию радиокнопок).
    Transport selectedTransport() const;
    // Открыт ли любой из транспортов.
    bool isLinkOpen() const;
    // Запись в активный (открытый) транспорт.
    bool writeActive(const QByteArray &data);
    // Текст ошибки активного транспорта.
    QString activeErrorString() const;

    QWidget *m_centralWidget;
    QVBoxLayout *m_mainLayout;

    QGroupBox *m_connectionGroup;
    QGridLayout *m_connectionLayout;

    // Выбор типа подключения
    QRadioButton *m_serialRadio;
    QRadioButton *m_tcpRadio;
    QButtonGroup *m_transportGroup;

    // Настройки последовательного порта (Modbus RTU)
    QWidget *m_serialSettingsWidget;
    QComboBox *m_portCombo;
    QComboBox *m_baudRateCombo;
    QComboBox *m_dataBitsCombo;
    QComboBox *m_parityCombo;
    QComboBox *m_stopBitsCombo;
    QComboBox *m_flowControlCombo;
    QPushButton *m_refreshPortsBtn;

    // Настройки Ethernet (Modbus TCP)
    QWidget *m_tcpSettingsWidget;
    QLineEdit *m_hostEdit;
    QSpinBox *m_portSpinBox;

    // Автонастройка сетевого адаптера ПК
    QCheckBox *m_autoConfigCheck;
    QLineEdit *m_pcIpEdit;
    QSpinBox *m_prefixSpin;
    QComboBox *m_adapterCombo;

    QPushButton *m_connectBtn;
    QPushButton *m_disconnectBtn;
    
    QTabWidget *m_dataTabWidget;
    
    QWidget *m_rawDataTab;
    QGroupBox *m_dataGroup;
    QVBoxLayout *m_dataLayout;
    QHBoxLayout *m_sendLayout;
    QLineEdit *m_sendEdit;
    QPushButton *m_sendBtn;
    QCheckBox *m_hexModeCheck;
    
    QWidget *m_modbusTab;
    QGroupBox *m_modbusGroup;
    QVBoxLayout *m_modbusLayout;
    QGridLayout *m_modbusFormLayout;
    QSpinBox *m_slaveAddressSpinBox;
    QComboBox *m_functionCodeCombo;
    QSpinBox *m_startAddressSpinBox;
    QSpinBox *m_quantitySpinBox;
    QSpinBox *m_valueSpinBox;
    QLineEdit *m_dataEdit;
    QPushButton *m_sendModbusBtn;
    QLabel *m_quantityLabel;
    
    QGroupBox *m_logGroup;
    QVBoxLayout *m_logLayout;
    QHBoxLayout *m_logButtonsLayout;
    QTextEdit *m_logText;
    QPushButton *m_clearBtn;
    QCheckBox *m_autoScrollCheck;
    
    SerialPortHandler *m_serialHandler;
    TcpClientHandler *m_tcpHandler;
    ModbusRTU *m_modbusRTU;
    ModbusTCP *m_modbusTCP;
    NetworkConfigurator *m_netConfigurator;
    QTimer *m_statusTimer;

    bool m_isConnected;
    Transport m_transport;        // активный транспорт открытого соединения
    uint16_t m_transactionId;     // счётчик Transaction ID для Modbus TCP
};

#endif // MAINWINDOW_H