#include "mainwindow.h"
#include "serialporthandler.h"
#include <QApplication>
#include <QDateTime>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_centralWidget(nullptr)
    , m_serialHandler(nullptr)
    , m_statusTimer(new QTimer(this))
    , m_isConnected(false)
{
    setupUI();
    setupConnections();
    refreshPorts();
    
    m_serialHandler = new SerialPortHandler(this);
    connect(m_serialHandler, &SerialPortHandler::dataReceived, this, &MainWindow::onDataReceived);
    connect(m_serialHandler, &SerialPortHandler::errorOccurred, this, &MainWindow::onPortError);
    
    setWindowTitle("RS485 Qt Terminal");
    resize(800, 600);
}

MainWindow::~MainWindow()
{
    if (m_serialHandler && m_serialHandler->isOpen()) {
        m_serialHandler->close();
    }
}

void MainWindow::setupUI()
{
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);
    
    m_mainLayout = new QVBoxLayout(m_centralWidget);
    
    m_connectionGroup = new QGroupBox("Подключение", this);
    m_connectionLayout = new QGridLayout(m_connectionGroup);
    
    m_connectionLayout->addWidget(new QLabel("Порт:"), 0, 0);
    m_portCombo = new QComboBox();
    m_connectionLayout->addWidget(m_portCombo, 0, 1);
    
    m_refreshPortsBtn = new QPushButton("Обновить");
    m_connectionLayout->addWidget(m_refreshPortsBtn, 0, 2);
    
    m_connectionLayout->addWidget(new QLabel("Скорость:"), 1, 0);
    m_baudRateCombo = new QComboBox();
    m_baudRateCombo->addItems({"9600", "19200", "38400", "57600", "115200"});
    m_baudRateCombo->setCurrentText("9600");
    m_connectionLayout->addWidget(m_baudRateCombo, 1, 1);
    
    m_connectionLayout->addWidget(new QLabel("Биты данных:"), 2, 0);
    m_dataBitsCombo = new QComboBox();
    m_dataBitsCombo->addItems({"5", "6", "7", "8"});
    m_dataBitsCombo->setCurrentText("8");
    m_connectionLayout->addWidget(m_dataBitsCombo, 2, 1);
    
    m_connectionLayout->addWidget(new QLabel("Четность:"), 3, 0);
    m_parityCombo = new QComboBox();
    m_parityCombo->addItems({"Нет", "Четная", "Нечетная", "Пробел", "Метка"});
    m_parityCombo->setCurrentText("Нет");
    m_connectionLayout->addWidget(m_parityCombo, 3, 1);
    
    m_connectionLayout->addWidget(new QLabel("Стоп-биты:"), 4, 0);
    m_stopBitsCombo = new QComboBox();
    m_stopBitsCombo->addItems({"1", "1.5", "2"});
    m_stopBitsCombo->setCurrentText("1");
    m_connectionLayout->addWidget(m_stopBitsCombo, 4, 1);
    
    m_connectionLayout->addWidget(new QLabel("Управление потоком:"), 5, 0);
    m_flowControlCombo = new QComboBox();
    m_flowControlCombo->addItems({"Нет", "RTS/CTS", "XON/XOFF"});
    m_flowControlCombo->setCurrentText("Нет");
    m_connectionLayout->addWidget(m_flowControlCombo, 5, 1);
    
    m_connectBtn = new QPushButton("Подключить");
    m_connectionLayout->addWidget(m_connectBtn, 6, 0);
    
    m_disconnectBtn = new QPushButton("Отключить");
    m_disconnectBtn->setEnabled(false);
    m_connectionLayout->addWidget(m_disconnectBtn, 6, 1);
    
    m_mainLayout->addWidget(m_connectionGroup);
    
    m_dataGroup = new QGroupBox("Отправка данных", this);
    m_dataLayout = new QVBoxLayout(m_dataGroup);
    
    m_sendLayout = new QHBoxLayout();
    m_sendEdit = new QLineEdit();
    m_sendEdit->setPlaceholderText("Введите данные для отправки...");
    m_sendLayout->addWidget(m_sendEdit);
    
    m_sendBtn = new QPushButton("Отправить");
    m_sendBtn->setEnabled(false);
    m_sendLayout->addWidget(m_sendBtn);
    
    m_dataLayout->addLayout(m_sendLayout);
    
    m_hexModeCheck = new QCheckBox("HEX режим");
    m_dataLayout->addWidget(m_hexModeCheck);
    
    m_mainLayout->addWidget(m_dataGroup);
    
    m_logGroup = new QGroupBox("Лог данных", this);
    m_logLayout = new QVBoxLayout(m_logGroup);
    
    m_logText = new QTextEdit();
    m_logText->setReadOnly(true);
    m_logText->setFont(QFont("Courier", 10));
    m_logLayout->addWidget(m_logText);
    
    m_logButtonsLayout = new QHBoxLayout();
    m_clearBtn = new QPushButton("Очистить");
    m_logButtonsLayout->addWidget(m_clearBtn);
    
    m_autoScrollCheck = new QCheckBox("Автопрокрутка");
    m_autoScrollCheck->setChecked(true);
    m_logButtonsLayout->addWidget(m_autoScrollCheck);
    
    m_logButtonsLayout->addStretch();
    m_logLayout->addLayout(m_logButtonsLayout);
    
    m_mainLayout->addWidget(m_logGroup);
}

void MainWindow::setupConnections()
{
    connect(m_refreshPortsBtn, &QPushButton::clicked, this, &MainWindow::refreshPorts);
    connect(m_connectBtn, &QPushButton::clicked, this, &MainWindow::connectToPort);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &MainWindow::disconnectFromPort);
    connect(m_sendBtn, &QPushButton::clicked, this, &MainWindow::sendData);
    connect(m_clearBtn, &QPushButton::clicked, this, &MainWindow::clearData);
    connect(m_sendEdit, &QLineEdit::returnPressed, this, &MainWindow::sendData);
}

void MainWindow::refreshPorts()
{
    m_portCombo->clear();
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &port : ports) {
        m_portCombo->addItem(port.portName() + " (" + port.description() + ")", port.portName());
    }
    
    if (m_portCombo->count() == 0) {
        m_portCombo->addItem("Нет доступных портов");
        m_connectBtn->setEnabled(false);
    } else {
        m_connectBtn->setEnabled(true);
    }
}

void MainWindow::connectToPort()
{
    if (m_portCombo->count() == 0 || m_portCombo->currentData().toString().isEmpty()) {
        QMessageBox::warning(this, "Ошибка", "Выберите порт для подключения");
        return;
    }
    
    SerialPortHandler::Settings settings;
    settings.portName = m_portCombo->currentData().toString();
    settings.baudRate = m_baudRateCombo->currentText().toInt();
    
    switch (m_dataBitsCombo->currentText().toInt()) {
        case 5: settings.dataBits = QSerialPort::Data5; break;
        case 6: settings.dataBits = QSerialPort::Data6; break;
        case 7: settings.dataBits = QSerialPort::Data7; break;
        case 8: settings.dataBits = QSerialPort::Data8; break;
        default: settings.dataBits = QSerialPort::Data8; break;
    }
    
    QString parityText = m_parityCombo->currentText();
    if (parityText == "Нет") settings.parity = QSerialPort::NoParity;
    else if (parityText == "Четная") settings.parity = QSerialPort::EvenParity;
    else if (parityText == "Нечетная") settings.parity = QSerialPort::OddParity;
    else if (parityText == "Пробел") settings.parity = QSerialPort::SpaceParity;
    else if (parityText == "Метка") settings.parity = QSerialPort::MarkParity;
    else settings.parity = QSerialPort::NoParity;
    
    QString stopBitsText = m_stopBitsCombo->currentText();
    if (stopBitsText == "1") settings.stopBits = QSerialPort::OneStop;
    else if (stopBitsText == "1.5") settings.stopBits = QSerialPort::OneAndHalfStop;
    else if (stopBitsText == "2") settings.stopBits = QSerialPort::TwoStop;
    else settings.stopBits = QSerialPort::OneStop;
    
    QString flowControlText = m_flowControlCombo->currentText();
    if (flowControlText == "Нет") settings.flowControl = QSerialPort::NoFlowControl;
    else if (flowControlText == "RTS/CTS") settings.flowControl = QSerialPort::HardwareControl;
    else if (flowControlText == "XON/XOFF") settings.flowControl = QSerialPort::SoftwareControl;
    else settings.flowControl = QSerialPort::NoFlowControl;
    
    if (m_serialHandler->open(settings)) {
        updateConnectionStatus(true);
        m_logText->append(QString("[%1] Подключен к %2")
                          .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                          .arg(settings.portName));
    } else {
        QMessageBox::critical(this, "Ошибка подключения", m_serialHandler->errorString());
    }
}

void MainWindow::disconnectFromPort()
{
    if (m_serialHandler && m_serialHandler->isOpen()) {
        m_serialHandler->close();
        updateConnectionStatus(false);
        m_logText->append(QString("[%1] Отключен от порта")
                          .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
    }
}

void MainWindow::sendData()
{
    if (!m_serialHandler || !m_serialHandler->isOpen()) {
        QMessageBox::warning(this, "Ошибка", "Порт не подключен");
        return;
    }
    
    QString data = m_sendEdit->text();
    if (data.isEmpty()) {
        return;
    }
    
    QByteArray dataToSend;
    
    if (m_hexModeCheck->isChecked()) {
        QString hexData = data.remove(' ');
        if (hexData.length() % 2 != 0) {
            QMessageBox::warning(this, "Ошибка", "Некорректный HEX формат");
            return;
        }
        
        for (int i = 0; i < hexData.length(); i += 2) {
            bool ok;
            unsigned char byte = hexData.mid(i, 2).toUInt(&ok, 16);
            if (!ok) {
                QMessageBox::warning(this, "Ошибка", "Некорректный HEX формат");
                return;
            }
            dataToSend.append(byte);
        }
    } else {
        dataToSend = data.toUtf8();
    }
    
    if (m_serialHandler->write(dataToSend)) {
        m_logText->append(QString("[%1] TX: %2")
                          .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                          .arg(m_hexModeCheck->isChecked() ? 
                               dataToSend.toHex(' ').toUpper() : 
                               QString::fromUtf8(dataToSend)));
        m_sendEdit->clear();
        
        if (m_autoScrollCheck->isChecked()) {
            m_logText->moveCursor(QTextCursor::End);
        }
    }
}

void MainWindow::clearData()
{
    m_logText->clear();
}

void MainWindow::onDataReceived(const QByteArray &data)
{
    m_logText->append(QString("[%1] RX: %2")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                      .arg(m_hexModeCheck->isChecked() ? 
                           data.toHex(' ').toUpper() : 
                           QString::fromUtf8(data)));
    
    if (m_autoScrollCheck->isChecked()) {
        m_logText->moveCursor(QTextCursor::End);
    }
}

void MainWindow::onPortError(const QString &error)
{
    m_logText->append(QString("[%1] ОШИБКА: %2")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                      .arg(error));
    
    updateConnectionStatus(false);
    
    if (m_autoScrollCheck->isChecked()) {
        m_logText->moveCursor(QTextCursor::End);
    }
}

void MainWindow::updateConnectionStatus(bool connected)
{
    m_isConnected = connected;
    m_connectBtn->setEnabled(!connected && m_portCombo->count() > 0);
    m_disconnectBtn->setEnabled(connected);
    m_sendBtn->setEnabled(connected);
    m_sendEdit->setEnabled(connected);
    
    QList<QWidget*> settingsWidgets = {m_portCombo, m_baudRateCombo, m_dataBitsCombo, 
                                       m_parityCombo, m_stopBitsCombo, m_flowControlCombo};
    for (QWidget* widget : settingsWidgets) {
        widget->setEnabled(!connected);
    }
}