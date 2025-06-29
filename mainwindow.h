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

class SerialPortHandler;

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
    
    QGroupBox *m_dataGroup;
    QVBoxLayout *m_dataLayout;
    QHBoxLayout *m_sendLayout;
    QLineEdit *m_sendEdit;
    QPushButton *m_sendBtn;
    QCheckBox *m_hexModeCheck;
    
    QGroupBox *m_logGroup;
    QVBoxLayout *m_logLayout;
    QHBoxLayout *m_logButtonsLayout;
    QTextEdit *m_logText;
    QPushButton *m_clearBtn;
    QCheckBox *m_autoScrollCheck;
    
    SerialPortHandler *m_serialHandler;
    QTimer *m_statusTimer;
    
    bool m_isConnected;
};

#endif // MAINWINDOW_H