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
class ModbusRTU;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void connectToPort();
    void disconnectFromPort();
    void sendData();
    void clearData();
    void onDataReceived(const QByteArray &data);
    void onPortError(const QString &error);
    void refreshPorts();
    void sendModbusRequest();

private:
    void setupUI();
    void setupConnections();
    void updateConnectionStatus(bool connected);
    
    QWidget *m_centralWidget;
    QVBoxLayout *m_mainLayout;
    
    QGroupBox *m_connectionGroup;
    QGridLayout *m_connectionLayout;
    QComboBox *m_portCombo;
    QComboBox *m_baudRateCombo;
    QComboBox *m_dataBitsCombo;
    QComboBox *m_parityCombo;
    QComboBox *m_stopBitsCombo;
    QComboBox *m_flowControlCombo;
    QPushButton *m_refreshPortsBtn;
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
    ModbusRTU *m_modbusRTU;
    QTimer *m_statusTimer;
    
    bool m_isConnected;
};

#endif // MAINWINDOW_H