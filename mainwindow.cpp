#include "mainwindow.h"
#include "serialporthandler.h"
#include "modbusrtu.h"
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
    
    m_modbusRTU = new ModbusRTU(this);
    
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
    
    m_dataTabWidget = new QTabWidget(this);
    
    m_rawDataTab = new QWidget();
    m_dataGroup = new QGroupBox("Отправка данных", m_rawDataTab);
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
    
    QVBoxLayout *rawTabLayout = new QVBoxLayout(m_rawDataTab);
    rawTabLayout->addWidget(m_dataGroup);
    rawTabLayout->addStretch();
    
    m_dataTabWidget->addTab(m_rawDataTab, "Сырые данные");
    
    m_modbusTab = new QWidget();
    m_modbusGroup = new QGroupBox("Modbus RTU", m_modbusTab);
    m_modbusLayout = new QVBoxLayout(m_modbusGroup);
    
    m_modbusFormLayout = new QGridLayout();
    
    m_modbusFormLayout->addWidget(new QLabel("Адрес устройства:"), 0, 0);
    m_slaveAddressSpinBox = new QSpinBox();
    m_slaveAddressSpinBox->setRange(1, 247);
    m_slaveAddressSpinBox->setValue(1);
    m_modbusFormLayout->addWidget(m_slaveAddressSpinBox, 0, 1);
    
    m_modbusFormLayout->addWidget(new QLabel("Функция:"), 1, 0);
    m_functionCodeCombo = new QComboBox();
    m_functionCodeCombo->addItem("01 - Чтение катушек", 0x01);
    m_functionCodeCombo->addItem("02 - Чтение дискретных входов", 0x02);
    m_functionCodeCombo->addItem("03 - Чтение регистров хранения", 0x03);
    m_functionCodeCombo->addItem("04 - Чтение входных регистров", 0x04);
    m_functionCodeCombo->addItem("05 - Запись одной катушки", 0x05);
    m_functionCodeCombo->addItem("06 - Запись одного регистра", 0x06);
    m_functionCodeCombo->addItem("0F - Запись нескольких катушек", 0x0F);
    m_functionCodeCombo->addItem("10 - Запись нескольких регистров", 0x10);
    m_functionCodeCombo->setCurrentIndex(2);
    m_modbusFormLayout->addWidget(m_functionCodeCombo, 1, 1);
    
    m_modbusFormLayout->addWidget(new QLabel("Начальный адрес:"), 2, 0);
    m_startAddressSpinBox = new QSpinBox();
    m_startAddressSpinBox->setRange(0, 65535);
    m_startAddressSpinBox->setValue(0);
    m_modbusFormLayout->addWidget(m_startAddressSpinBox, 2, 1);
    
    m_quantityLabel = new QLabel("Количество/Значение:");
    m_modbusFormLayout->addWidget(m_quantityLabel, 3, 0);
    m_quantitySpinBox = new QSpinBox();
    m_quantitySpinBox->setRange(1, 125);
    m_quantitySpinBox->setValue(1);
    m_modbusFormLayout->addWidget(m_quantitySpinBox, 3, 1);
    
    m_valueSpinBox = new QSpinBox();
    m_valueSpinBox->setRange(0, 65535);
    m_valueSpinBox->setValue(0);
    m_valueSpinBox->setVisible(false);
    m_modbusFormLayout->addWidget(m_valueSpinBox, 3, 2);
    
    m_modbusFormLayout->addWidget(new QLabel("Данные (HEX):"), 4, 0);
    m_dataEdit = new QLineEdit();
    m_dataEdit->setPlaceholderText("Например: 01 02 03 04");
    m_dataEdit->setVisible(false);
    m_modbusFormLayout->addWidget(m_dataEdit, 4, 1, 1, 2);
    
    m_modbusLayout->addLayout(m_modbusFormLayout);
    
    m_sendModbusBtn = new QPushButton("Отправить Modbus запрос");
    m_sendModbusBtn->setEnabled(false);
    m_modbusLayout->addWidget(m_sendModbusBtn);
    
    QVBoxLayout *modbusTabLayout = new QVBoxLayout(m_modbusTab);
    modbusTabLayout->addWidget(m_modbusGroup);
    modbusTabLayout->addStretch();
    
    m_dataTabWidget->addTab(m_modbusTab, "Modbus RTU");
    
    m_mainLayout->addWidget(m_dataTabWidget);
    
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
    connect(m_sendModbusBtn, &QPushButton::clicked, this, &MainWindow::sendModbusRequest);
    
    connect(m_functionCodeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index) {
        int funcCode = m_functionCodeCombo->currentData().toInt();
        bool isWrite = (funcCode == 0x05 || funcCode == 0x06);
        bool isMultipleWrite = (funcCode == 0x0F || funcCode == 0x10);
        
        m_quantitySpinBox->setVisible(!isWrite);
        m_valueSpinBox->setVisible(isWrite);
        m_dataEdit->setVisible(isMultipleWrite);
        
        if (isWrite) {
            m_quantityLabel->setText("Значение:");
        } else {
            m_quantityLabel->setText("Количество:");
        }
    });
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
    QString hexData = data.toHex(' ').toUpper();
    QString logEntry;
    
    // Проверяем, является ли это Modbus ответом
    bool isModbusResponse = (data.length() >= 4 && ModbusRTU::validateResponse(data));
    
    if (isModbusResponse) {
        // Для Modbus всегда показываем HEX
        logEntry = QString("[%1] RX (Modbus): %2")
                  .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                  .arg(hexData);
        
        QString modbusInfo = ModbusRTU::parseResponse(data);
        logEntry += QString(" | %1").arg(modbusInfo);
    } else {
        // Для обычных данных используем настройку HEX режима
        logEntry = QString("[%1] RX: %2")
                  .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                  .arg(m_hexModeCheck->isChecked() ? 
                       hexData : 
                       QString::fromUtf8(data));
    }
    
    m_logText->append(logEntry);
    
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
    m_sendModbusBtn->setEnabled(connected);
    
    QList<QWidget*> settingsWidgets = {m_portCombo, m_baudRateCombo, m_dataBitsCombo, 
                                       m_parityCombo, m_stopBitsCombo, m_flowControlCombo};
    for (QWidget* widget : settingsWidgets) {
        widget->setEnabled(!connected);
    }
}

void MainWindow::sendModbusRequest()
{
    if (!m_serialHandler || !m_serialHandler->isOpen()) {
        QMessageBox::warning(this, "Ошибка", "Порт не подключен");
        return;
    }
    
    uint8_t slaveAddress = m_slaveAddressSpinBox->value();
    int funcCode = m_functionCodeCombo->currentData().toInt();
    uint16_t startAddress = m_startAddressSpinBox->value();
    uint16_t quantity = m_quantitySpinBox->value();
    uint16_t value = m_valueSpinBox->value();
    QString dataString = m_dataEdit->text();
    
    QByteArray packet;
    
    switch (funcCode) {
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04: {
            ModbusRTU::ReadRequest request;
            request.slaveAddress = slaveAddress;
            request.functionCode = static_cast<ModbusRTU::FunctionCode>(funcCode);
            request.startAddress = startAddress;
            request.quantity = quantity;
            packet = ModbusRTU::createReadRequest(request);
            break;
        }
        case 0x05:
        case 0x06: {
            ModbusRTU::WriteSingleRequest request;
            request.slaveAddress = slaveAddress;
            request.functionCode = static_cast<ModbusRTU::FunctionCode>(funcCode);
            request.address = startAddress;
            request.value = value;
            packet = ModbusRTU::createWriteSingleRequest(request);
            break;
        }
        case 0x0F:
        case 0x10: {
            ModbusRTU::WriteMultipleRequest request;
            request.slaveAddress = slaveAddress;
            request.functionCode = static_cast<ModbusRTU::FunctionCode>(funcCode);
            request.startAddress = startAddress;
            request.quantity = quantity;
            
            QStringList hexBytes = dataString.split(' ', Qt::SkipEmptyParts);
            for (const QString &hexByte : hexBytes) {
                bool ok;
                uint8_t byte = hexByte.toUInt(&ok, 16);
                if (!ok) {
                    QMessageBox::warning(this, "Ошибка", "Некорректный формат данных");
                    return;
                }
                request.data.append(byte);
            }
            
            packet = ModbusRTU::createWriteMultipleRequest(request);
            break;
        }
        default:
            QMessageBox::warning(this, "Ошибка", "Неподдерживаемый код функции");
            return;
    }
    
    if (m_serialHandler->write(packet)) {
        m_logText->append(QString("[%1] TX (Modbus): %2")
                          .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                          .arg(packet.toHex(' ').toUpper()));
        
        if (m_autoScrollCheck->isChecked()) {
            m_logText->moveCursor(QTextCursor::End);
        }
    }
}