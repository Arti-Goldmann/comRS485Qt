#include "mainwindow.h"
#include "serialporthandler.h"
#include "tcpclienthandler.h"
#include "modbusrtu.h"
#include "modbustcp.h"
#include "networkconfigurator.h"
#include <QApplication>
#include <QDateTime>
#include <QAbstractButton>
#include <QCloseEvent>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_centralWidget(nullptr)
    , m_serialHandler(nullptr)
    , m_tcpHandler(nullptr)
    , m_cycleTimer(new QTimer(this))
    , m_isConnected(false)
    , m_transport(TransportSerial)
    , m_cycleMode(CycleNone)
    , m_transactionId(0)
{
    setupUI();
    setupConnections();
    refreshPorts();

    m_serialHandler = new SerialPortHandler(this);
    connect(m_serialHandler, &SerialPortHandler::dataReceived, this, &MainWindow::onDataReceived);
    connect(m_serialHandler, &SerialPortHandler::errorOccurred, this, &MainWindow::onPortError);
    connect(m_serialHandler, &SerialPortHandler::connectionStatusChanged, this,
            [this](bool connected) {
                if (!connected) updateConnectionStatus(false);
            });

    m_tcpHandler = new TcpClientHandler(this);
    connect(m_tcpHandler, &TcpClientHandler::dataReceived, this, &MainWindow::onDataReceived);
    connect(m_tcpHandler, &TcpClientHandler::errorOccurred, this, &MainWindow::onPortError);
    connect(m_tcpHandler, &TcpClientHandler::connectionStatusChanged, this,
            [this](bool connected) {
                if (!connected) updateConnectionStatus(false);
            });

    m_modbusRTU = new ModbusRTU(this);
    m_modbusTCP = new ModbusTCP(this);
    m_netConfigurator = new NetworkConfigurator(this);

    setWindowTitle("RS485 / Modbus Qt Terminal");
    resize(800, 600);
}

MainWindow::~MainWindow()
{
    if (m_serialHandler && m_serialHandler->isOpen()) {
        m_serialHandler->close();
    }
    if (m_tcpHandler && m_tcpHandler->isOpen()) {
        m_tcpHandler->close();
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Если приложение меняло настройки Ethernet — предлагаем вернуть их в обычный
    // режим (DHCP) перед выходом.
    if (m_netConfigurator && m_netConfigurator->wasApplied()) {
        QMessageBox::StandardButton answer = QMessageBox::question(
            this, "Восстановление сети",
            QString("Сетевой адаптер «%1» был переведён в статический режим для\n"
                    "работы с устройством. Вернуть его в обычный режим (DHCP) перед выходом?\n\n"
                    "Потребуются права администратора.")
                .arg(m_netConfigurator->appliedInterface()),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);

        if (answer == QMessageBox::Cancel) {
            event->ignore();
            return;
        }

        if (answer == QMessageBox::Yes) {
            // Закрываем активное соединение — перезапуск адаптера всё равно его разорвёт.
            if (m_serialHandler && m_serialHandler->isOpen()) m_serialHandler->close();
            if (m_tcpHandler && m_tcpHandler->isOpen()) m_tcpHandler->close();

            QApplication::setOverrideCursor(Qt::WaitCursor);
            QString err;
            bool ok = m_netConfigurator->restore(nullptr, &err);
            QApplication::restoreOverrideCursor();

            if (!ok) {
                QMessageBox::StandardButton retry = QMessageBox::warning(
                    this, "Не удалось восстановить сеть",
                    QString("%1\n\nВсё равно выйти из приложения?").arg(err),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                if (retry != QMessageBox::Yes) {
                    event->ignore();
                    return;
                }
            }
        }
    }

    stopCyclicSending();
    QMainWindow::closeEvent(event);
}

void MainWindow::setupUI()
{
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);
    
    m_mainLayout = new QVBoxLayout(m_centralWidget);
    
    m_connectionGroup = new QGroupBox("Подключение", this);
    QVBoxLayout *connectionOuterLayout = new QVBoxLayout(m_connectionGroup);

    // --- Выбор типа транспорта ---
    QHBoxLayout *transportLayout = new QHBoxLayout();
    transportLayout->addWidget(new QLabel("Тип:"));
    m_serialRadio = new QRadioButton("Serial (Modbus RTU)");
    m_tcpRadio = new QRadioButton("Ethernet (Modbus TCP)");
    m_serialRadio->setChecked(true);
    m_transportGroup = new QButtonGroup(this);
    m_transportGroup->addButton(m_serialRadio, TransportSerial);
    m_transportGroup->addButton(m_tcpRadio, TransportTcp);
    transportLayout->addWidget(m_serialRadio);
    transportLayout->addWidget(m_tcpRadio);
    transportLayout->addStretch();
    connectionOuterLayout->addLayout(transportLayout);

    // --- Настройки последовательного порта ---
    m_serialSettingsWidget = new QWidget();
    m_connectionLayout = new QGridLayout(m_serialSettingsWidget);
    m_connectionLayout->setContentsMargins(0, 0, 0, 0);

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

    connectionOuterLayout->addWidget(m_serialSettingsWidget);

    // --- Настройки Ethernet (Modbus TCP) ---
    m_tcpSettingsWidget = new QWidget();
    QGridLayout *tcpLayout = new QGridLayout(m_tcpSettingsWidget);
    tcpLayout->setContentsMargins(0, 0, 0, 0);

    tcpLayout->addWidget(new QLabel("IP адрес:"), 0, 0);
    m_hostEdit = new QLineEdit("192.168.1.31");
    m_hostEdit->setPlaceholderText("например, 192.168.1.31");
    tcpLayout->addWidget(m_hostEdit, 0, 1);

    tcpLayout->addWidget(new QLabel("Порт:"), 1, 0);
    m_portSpinBox = new QSpinBox();
    m_portSpinBox->setRange(1, 65535);
    m_portSpinBox->setValue(502);
    tcpLayout->addWidget(m_portSpinBox, 1, 1);

    // Автонастройка сетевого адаптера ПК
    m_autoConfigCheck = new QCheckBox("Настраивать сетевой адаптер ПК при подключении");
    m_autoConfigCheck->setChecked(true);
    m_autoConfigCheck->setToolTip("Назначает компьютеру статический IP в подсети устройства\n"
                                  "(требуются права администратора). Шлюз не задаётся —\n"
                                  "интернет остаётся на Wi-Fi.");
    tcpLayout->addWidget(m_autoConfigCheck, 2, 0, 1, 2);

    tcpLayout->addWidget(new QLabel("IP ПК:"), 3, 0);
    m_pcIpEdit = new QLineEdit();
    m_pcIpEdit->setPlaceholderText("вычисляется из IP устройства");
    tcpLayout->addWidget(m_pcIpEdit, 3, 1);

    tcpLayout->addWidget(new QLabel("Маска (/префикс):"), 4, 0);
    m_prefixSpin = new QSpinBox();
    m_prefixSpin->setRange(1, 32);
    m_prefixSpin->setValue(24);
    tcpLayout->addWidget(m_prefixSpin, 4, 1);

    tcpLayout->addWidget(new QLabel("Адаптер:"), 5, 0);
    m_adapterCombo = new QComboBox();
    m_adapterCombo->addItem("Авто", QString());
    for (const QString &alias : NetworkConfigurator::wiredInterfaces()) {
        m_adapterCombo->addItem(alias, alias);
    }
    tcpLayout->addWidget(m_adapterCombo, 5, 1);

    // Связываем доступность полей автонастройки с галочкой.
    auto updateAutoConfigEnabled = [this]() {
        bool on = m_autoConfigCheck->isChecked();
        m_pcIpEdit->setEnabled(on);
        m_prefixSpin->setEnabled(on);
        m_adapterCombo->setEnabled(on);
    };
    connect(m_autoConfigCheck, &QCheckBox::toggled, this, [updateAutoConfigEnabled](bool){ updateAutoConfigEnabled(); });

    // Авто-подстановка IP ПК из адреса устройства.
    m_pcIpEdit->setText(NetworkConfigurator::deriveHostIP(m_hostEdit->text().trimmed(), m_prefixSpin->value()));

    m_tcpSettingsWidget->setVisible(false);
    connectionOuterLayout->addWidget(m_tcpSettingsWidget);

    // --- Кнопки подключения ---
    QHBoxLayout *connectButtonsLayout = new QHBoxLayout();
    m_connectBtn = new QPushButton("Подключить");
    connectButtonsLayout->addWidget(m_connectBtn);

    m_disconnectBtn = new QPushButton("Отключить");
    m_disconnectBtn->setEnabled(false);
    connectButtonsLayout->addWidget(m_disconnectBtn);
    connectionOuterLayout->addLayout(connectButtonsLayout);

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
    m_sendBtn->setCheckable(true);
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
    m_modbusGroup = new QGroupBox("Modbus (RTU / TCP)", m_modbusTab);
    m_modbusLayout = new QVBoxLayout(m_modbusGroup);
    
    m_modbusFormLayout = new QGridLayout();
    
    m_modbusFormLayout->addWidget(new QLabel("Адрес устройства / Unit ID:"), 0, 0);
    m_slaveAddressSpinBox = new QSpinBox();
    m_slaveAddressSpinBox->setRange(0, 247); // 0 допустим для Unit ID в Modbus TCP
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
    m_functionCodeCombo->addItem("17 - Чтение/запись нескольких регистров", 0x17);
    m_functionCodeCombo->setCurrentIndex(2);
    m_modbusFormLayout->addWidget(m_functionCodeCombo, 1, 1);

    m_startAddressLabel = new QLabel("Начальный адрес:");
    m_modbusFormLayout->addWidget(m_startAddressLabel, 2, 0);
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

    // Отдельные адрес и количество для части записи функции 0x17
    m_writeAddressLabel = new QLabel("Адрес записи:");
    m_modbusFormLayout->addWidget(m_writeAddressLabel, 4, 0);
    m_writeAddressSpinBox = new QSpinBox();
    m_writeAddressSpinBox->setRange(0, 65535);
    m_writeAddressSpinBox->setValue(0);
    m_modbusFormLayout->addWidget(m_writeAddressSpinBox, 4, 1);

    m_writeQuantityLabel = new QLabel("Количество записи:");
    m_modbusFormLayout->addWidget(m_writeQuantityLabel, 5, 0);
    m_writeQuantitySpinBox = new QSpinBox();
    m_writeQuantitySpinBox->setRange(1, 121); // предел функции 0x17 (0x79 регистров)
    m_writeQuantitySpinBox->setValue(1);
    m_modbusFormLayout->addWidget(m_writeQuantitySpinBox, 5, 1);

    m_dataLabel = new QLabel("Данные (HEX):");
    m_modbusFormLayout->addWidget(m_dataLabel, 6, 0);
    m_dataEdit = new QLineEdit();
    m_dataEdit->setPlaceholderText("Например: 01 02 03 04");
    m_modbusFormLayout->addWidget(m_dataEdit, 6, 1, 1, 2);

    m_modbusLayout->addLayout(m_modbusFormLayout);
    
    m_sendModbusBtn = new QPushButton("Отправить Modbus запрос");
    m_sendModbusBtn->setCheckable(true);
    m_sendModbusBtn->setEnabled(false);
    m_modbusLayout->addWidget(m_sendModbusBtn);
    
    QVBoxLayout *modbusTabLayout = new QVBoxLayout(m_modbusTab);
    modbusTabLayout->addWidget(m_modbusGroup);
    modbusTabLayout->addStretch();
    
    m_dataTabWidget->addTab(m_modbusTab, "Modbus");

    m_mainLayout->addWidget(m_dataTabWidget);

    // Общие настройки циклической отправки. Поле периода показывается только
    // когда режим включён, но сам цикл запускается кнопкой активной вкладки.
    m_cycleLayout = new QHBoxLayout();
    m_cycleCheck = new QCheckBox("Отправлять циклически");
    m_cycleLayout->addWidget(m_cycleCheck);

    m_cyclePeriodLabel = new QLabel("Период:");
    m_cyclePeriodLabel->setVisible(false);
    m_cycleLayout->addWidget(m_cyclePeriodLabel);

    m_cyclePeriodSpin = new QSpinBox();
    m_cyclePeriodSpin->setRange(10, 3600000);
    m_cyclePeriodSpin->setValue(1000);
    m_cyclePeriodSpin->setSuffix(" мс");
    m_cyclePeriodSpin->setVisible(false);
    m_cycleLayout->addWidget(m_cyclePeriodSpin);
    m_cycleLayout->addStretch();
    m_mainLayout->addLayout(m_cycleLayout);
    
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

    connect(m_cycleCheck, &QCheckBox::toggled, this, [this](bool enabled) {
        m_cyclePeriodLabel->setVisible(enabled);
        m_cyclePeriodSpin->setVisible(enabled);
        if (!enabled) {
            stopCyclicSending();
        }
    });
    connect(m_cyclePeriodSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int periodMs) {
                if (m_cycleTimer->isActive()) {
                    m_cycleTimer->setInterval(periodMs);
                }
            });
    connect(m_cycleTimer, &QTimer::timeout, this, &MainWindow::handleCycleTimeout);

    connect(m_transportGroup, QOverload<QAbstractButton*>::of(&QButtonGroup::buttonClicked),
            this, [this](QAbstractButton *) { onTransportChanged(); });

    // Авто-подстановка IP ПК из адреса устройства / маски (пользователь может
    // затем поправить значение вручную).
    auto deriveIp = [this]() {
        m_pcIpEdit->setText(NetworkConfigurator::deriveHostIP(m_hostEdit->text().trimmed(),
                                                              m_prefixSpin->value()));
    };
    connect(m_hostEdit, &QLineEdit::editingFinished, this, deriveIp);
    connect(m_prefixSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [deriveIp](int){ deriveIp(); });

    connect(m_functionCodeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updateModbusFormFields(); });

    updateModbusFormFields();
}

void MainWindow::updateModbusFormFields()
{
    int funcCode = m_functionCodeCombo->currentData().toInt();
    bool isWrite = (funcCode == 0x05 || funcCode == 0x06);
    bool isMultipleWrite = (funcCode == 0x0F || funcCode == 0x10);
    bool isReadWrite = (funcCode == 0x17);

    m_quantitySpinBox->setVisible(!isWrite);
    m_valueSpinBox->setVisible(isWrite);

    // Поля записи нужны только функции 0x17: у неё своя пара адрес/количество
    // для записываемых регистров.
    m_writeAddressLabel->setVisible(isReadWrite);
    m_writeAddressSpinBox->setVisible(isReadWrite);
    m_writeQuantityLabel->setVisible(isReadWrite);
    m_writeQuantitySpinBox->setVisible(isReadWrite);

    bool needsData = (isMultipleWrite || isReadWrite);
    m_dataLabel->setVisible(needsData);
    m_dataEdit->setVisible(needsData);

    m_startAddressLabel->setText(isReadWrite ? "Адрес чтения:" : "Начальный адрес:");

    if (isWrite) {
        m_quantityLabel->setText("Значение:");
    } else if (isReadWrite) {
        m_quantityLabel->setText("Количество чтения:");
    } else {
        m_quantityLabel->setText("Количество:");
    }

    m_dataLabel->setText(isReadWrite ? "Данные записи (HEX):" : "Данные (HEX):");
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
    // --- Ethernet (Modbus TCP) ---
    if (selectedTransport() == TransportTcp) {
        TcpClientHandler::Settings tcpSettings;
        tcpSettings.host = m_hostEdit->text().trimmed();
        tcpSettings.port = static_cast<quint16>(m_portSpinBox->value());

        if (tcpSettings.host.isEmpty()) {
            QMessageBox::warning(this, "Ошибка", "Укажите IP адрес устройства");
            return;
        }

        // Автонастройка сетевого адаптера ПК (статический IP в подсети устройства).
        if (m_autoConfigCheck->isChecked()) {
            NetworkConfigurator::Config cfg;
            cfg.deviceIP = tcpSettings.host;
            cfg.staticIP = m_pcIpEdit->text().trimmed();
            cfg.prefix = m_prefixSpin->value();
            cfg.interfaceAlias = m_adapterCombo->currentData().toString();
            cfg.toggleWifi = false;

            if (cfg.staticIP.isEmpty()) {
                QMessageBox::warning(this, "Ошибка",
                                     "Не удалось определить IP для ПК. Укажите его вручную в поле «IP ПК».");
                return;
            }

            QString ts = QDateTime::currentDateTime().toString("hh:mm:ss");
            m_logText->append(QString("[%1] Настройка адаптера: %2/%3 для устройства %4...")
                              .arg(ts).arg(cfg.staticIP).arg(cfg.prefix).arg(cfg.deviceIP));
            QApplication::setOverrideCursor(Qt::WaitCursor);

            QString scriptLog;
            QString errorMsg;
            bool ok = m_netConfigurator->applyConfig(cfg, &scriptLog, &errorMsg);

            QApplication::restoreOverrideCursor();

            if (!scriptLog.isEmpty()) {
                const QStringList lines = scriptLog.split('\n', Qt::SkipEmptyParts);
                for (const QString &line : lines) {
                    m_logText->append("    " + line.trimmed());
                }
            }

            if (!ok) {
                QMessageBox::StandardButton answer = QMessageBox::question(
                    this, "Настройка адаптера не удалась",
                    QString("%1\n\nПродолжить подключение без настройки адаптера?").arg(errorMsg),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                if (answer != QMessageBox::Yes) {
                    return;
                }
            }
        }

        if (m_tcpHandler->open(tcpSettings)) {
            m_transport = TransportTcp;
            updateConnectionStatus(true);
            m_logText->append(QString("[%1] Подключен к %2:%3")
                              .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                              .arg(tcpSettings.host)
                              .arg(tcpSettings.port));
        } else {
            QMessageBox::critical(this, "Ошибка подключения", m_tcpHandler->errorString());
        }
        return;
    }

    // --- Serial (Modbus RTU) ---
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
        m_transport = TransportSerial;
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
    stopCyclicSending();

    bool wasOpen = false;
    if (m_serialHandler && m_serialHandler->isOpen()) {
        m_serialHandler->close();
        wasOpen = true;
    }
    if (m_tcpHandler && m_tcpHandler->isOpen()) {
        m_tcpHandler->close();
        wasOpen = true;
    }

    if (wasOpen) {
        updateConnectionStatus(false);
        m_logText->append(QString("[%1] Отключено")
                          .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
    }
}

void MainWindow::sendData()
{
    handleSendAction(CycleRaw);
}

bool MainWindow::sendRawOnce(QString *errorMessage)
{
    if (!isLinkOpen()) {
        if (errorMessage) *errorMessage = "Нет активного подключения";
        return false;
    }

    const QString data = m_sendEdit->text();
    if (data.isEmpty()) {
        if (errorMessage) *errorMessage = "Введите данные для отправки";
        return false;
    }

    QByteArray dataToSend;
    if (m_hexModeCheck->isChecked()) {
        QString hexData = data;
        hexData.remove(' ');
        if (hexData.isEmpty() || hexData.length() % 2 != 0) {
            if (errorMessage) *errorMessage = "Некорректный HEX формат";
            return false;
        }

        for (int i = 0; i < hexData.length(); i += 2) {
            bool ok = false;
            const unsigned char byte = hexData.mid(i, 2).toUInt(&ok, 16);
            if (!ok) {
                if (errorMessage) *errorMessage = "Некорректный HEX формат";
                return false;
            }
            dataToSend.append(byte);
        }
    } else {
        dataToSend = data.toUtf8();
    }

    if (!writeActive(dataToSend)) {
        if (errorMessage) {
            *errorMessage = QString("Не удалось отправить данные: %1").arg(activeErrorString());
        }
        return false;
    }

    m_logText->append(QString("[%1] TX: %2")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                      .arg(m_hexModeCheck->isChecked()
                               ? dataToSend.toHex(' ').toUpper()
                               : QString::fromUtf8(dataToSend)));

    if (m_autoScrollCheck->isChecked()) {
        m_logText->moveCursor(QTextCursor::End);
    }
    return true;
}

void MainWindow::clearData()
{
    m_logText->clear();
}

void MainWindow::onDataReceived(const QByteArray &data)
{
    // Пропускаем пустые данные
    if (data.isEmpty()) {
        return;
    }

    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");

    // Выделяем из принятого потока отдельные кадры Modbus. Это работает как при
    // обычном обмене (запрос -> ответ), так и в режиме прослушивания шины, когда
    // в одном пакете приходит несколько склеенных кадров. Способ обрамления
    // (RTU c CRC либо TCP с MBAP-заголовком) выбираем по активному транспорту.
    QByteArray unrecognized;
    QList<QByteArray> frames;
    const bool tcp = (m_transport == TransportTcp);

    if (tcp) {
        frames = ModbusTCP::splitFrames(data, unrecognized);
    } else {
        frames = ModbusRTU::splitFrames(data, unrecognized);
    }

    if (!frames.isEmpty()) {
        // Расшифровываем каждый найденный кадр (запрос или ответ)
        for (const QByteArray &frame : frames) {
            QString decoded = tcp ? ModbusTCP::parseFrame(frame)
                                  : ModbusRTU::parseFrame(frame);
            m_logText->append(QString("[%1] RX (Modbus): %2 | %3")
                              .arg(timestamp)
                              .arg(frame.toHex(' ').toUpper())
                              .arg(decoded));
        }
        // Байты, не вошедшие ни в один кадр (мусор / обрезки)
        if (!unrecognized.isEmpty()) {
            m_logText->append(QString("[%1] RX (нераспознано): %2")
                              .arg(timestamp)
                              .arg(unrecognized.toHex(' ').toUpper()));
        }
    } else {
        // Ни одного кадра Modbus — показываем как обычные данные
        appendRawData(data, timestamp);
    }

    if (m_autoScrollCheck->isChecked()) {
        m_logText->moveCursor(QTextCursor::End);
    }
}

void MainWindow::appendRawData(const QByteArray &data, const QString &timestamp)
{
    QString hexData = data.toHex(' ').toUpper();

    if (m_hexModeCheck->isChecked()) {
        m_logText->append(QString("[%1] RX: %2").arg(timestamp).arg(hexData));
        return;
    }

    // Проверяем, содержит ли данные только печатаемые символы
    bool hasPrintableChars = false;
    bool hasNonPrintableChars = false;

    for (int i = 0; i < data.length(); ++i) {
        unsigned char byte = static_cast<unsigned char>(data[i]);
        if (byte >= 32 && byte <= 126) {
            hasPrintableChars = true;
        } else if (byte != 0x0A && byte != 0x0D && byte != 0x09) { // исключаем \n, \r, \t
            hasNonPrintableChars = true;
        }
    }

    if (hasNonPrintableChars || !hasPrintableChars) {
        // Показываем в HEX если есть непечатаемые символы
        m_logText->append(QString("[%1] RX (HEX): %2").arg(timestamp).arg(hexData));
    } else {
        // Показываем как текст только если все символы печатаемые
        m_logText->append(QString("[%1] RX: %2").arg(timestamp).arg(QString::fromUtf8(data)));
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
    if (!connected) {
        stopCyclicSending();
    }

    m_isConnected = connected;

    // В режиме TCP кнопка «Подключить» доступна всегда; в режиме Serial — только
    // при наличии хотя бы одного доступного порта.
    bool canConnect = (selectedTransport() == TransportTcp) || (m_portCombo->count() > 0);
    m_connectBtn->setEnabled(!connected && canConnect);
    m_disconnectBtn->setEnabled(connected);
    m_sendBtn->setEnabled(connected);
    m_sendEdit->setEnabled(connected);
    m_sendModbusBtn->setEnabled(connected);

    // Пока соединение активно, запрещаем менять транспорт и его настройки.
    QList<QWidget*> settingsWidgets = {m_portCombo, m_baudRateCombo, m_dataBitsCombo,
                                       m_parityCombo, m_stopBitsCombo, m_flowControlCombo,
                                       m_refreshPortsBtn, m_hostEdit, m_portSpinBox,
                                       m_serialRadio, m_tcpRadio,
                                       m_autoConfigCheck, m_pcIpEdit, m_prefixSpin, m_adapterCombo};
    for (QWidget* widget : settingsWidgets) {
        widget->setEnabled(!connected);
    }
}

void MainWindow::sendModbusRequest()
{
    handleSendAction(CycleModbus);
}

bool MainWindow::sendModbusOnce(QString *errorMessage)
{
    if (!isLinkOpen()) {
        if (errorMessage) *errorMessage = "Нет активного подключения";
        return false;
    }

    uint8_t slaveAddress = m_slaveAddressSpinBox->value();
    int funcCode = m_functionCodeCombo->currentData().toInt();
    uint16_t startAddress = m_startAddressSpinBox->value();
    uint16_t quantity = m_quantitySpinBox->value();
    uint16_t value = m_valueSpinBox->value();
    uint16_t writeAddress = m_writeAddressSpinBox->value();
    uint16_t writeQuantity = m_writeQuantitySpinBox->value();
    const QString dataString = m_dataEdit->text();

    // Для функций записи нескольких регистров/катушек разбираем поле данных.
    QByteArray writeData;
    if (funcCode == 0x0F || funcCode == 0x10 || funcCode == 0x17) {
        QString hexData = dataString;
        hexData.remove(' ');
        if (hexData.isEmpty() || hexData.length() % 2 != 0) {
            if (errorMessage) *errorMessage = "Некорректный формат данных Modbus (ожидаются HEX-байты)";
            return false;
        }
        for (int i = 0; i < hexData.length(); i += 2) {
            bool ok = false;
            const uint8_t byte = hexData.mid(i, 2).toUInt(&ok, 16);
            if (!ok) {
                if (errorMessage) *errorMessage = "Некорректный формат данных Modbus (ожидаются HEX-байты)";
                return false;
            }
            writeData.append(byte);
        }
    }

    int expectedDataSize = -1;
    if (funcCode == 0x0F) {
        expectedDataSize = (quantity + 7) / 8;
    } else if (funcCode == 0x10) {
        expectedDataSize = quantity * 2;
    } else if (funcCode == 0x17) {
        expectedDataSize = writeQuantity * 2;
    }
    if (expectedDataSize >= 0 && writeData.length() != expectedDataSize) {
        if (errorMessage) {
            *errorMessage = QString("Для выбранной операции нужно %1 байт данных, введено %2")
                                .arg(expectedDataSize)
                                .arg(writeData.length());
        }
        return false;
    }

    const bool tcp = (m_transport == TransportTcp);
    QByteArray packet;

    if (tcp) {
        // Modbus TCP: тот же набор функций, но с MBAP-заголовком и счётчиком транзакций.
        uint16_t transactionId = ++m_transactionId;
        switch (funcCode) {
            case 0x01:
            case 0x02:
            case 0x03:
            case 0x04: {
                ModbusTCP::ReadRequest request;
                request.unitId = slaveAddress;
                request.functionCode = static_cast<ModbusTCP::FunctionCode>(funcCode);
                request.startAddress = startAddress;
                request.quantity = quantity;
                packet = ModbusTCP::createReadRequest(request, transactionId);
                break;
            }
            case 0x05:
            case 0x06: {
                ModbusTCP::WriteSingleRequest request;
                request.unitId = slaveAddress;
                request.functionCode = static_cast<ModbusTCP::FunctionCode>(funcCode);
                request.address = startAddress;
                request.value = value;
                packet = ModbusTCP::createWriteSingleRequest(request, transactionId);
                break;
            }
            case 0x0F:
            case 0x10: {
                ModbusTCP::WriteMultipleRequest request;
                request.unitId = slaveAddress;
                request.functionCode = static_cast<ModbusTCP::FunctionCode>(funcCode);
                request.startAddress = startAddress;
                request.quantity = quantity;
                request.data = writeData;
                packet = ModbusTCP::createWriteMultipleRequest(request, transactionId);
                break;
            }
            case 0x17: {
                ModbusTCP::ReadWriteMultipleRequest request;
                request.unitId = slaveAddress;
                request.readStartAddress = startAddress;
                request.readQuantity = quantity;
                request.writeStartAddress = writeAddress;
                request.writeQuantity = writeQuantity;
                request.data = writeData;
                packet = ModbusTCP::createReadWriteMultipleRequest(request, transactionId);
                break;
            }
            default:
                if (errorMessage) *errorMessage = "Неподдерживаемый код функции";
                return false;
        }
    } else {
        // Modbus RTU
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
                request.data = writeData;
                packet = ModbusRTU::createWriteMultipleRequest(request);
                break;
            }
            case 0x17: {
                ModbusRTU::ReadWriteMultipleRequest request;
                request.slaveAddress = slaveAddress;
                request.readStartAddress = startAddress;
                request.readQuantity = quantity;
                request.writeStartAddress = writeAddress;
                request.writeQuantity = writeQuantity;
                request.data = writeData;
                packet = ModbusRTU::createReadWriteMultipleRequest(request);
                break;
            }
            default:
                if (errorMessage) *errorMessage = "Неподдерживаемый код функции";
                return false;
        }
    }

    if (!writeActive(packet)) {
        if (errorMessage) {
            *errorMessage = QString("Не удалось отправить Modbus-запрос: %1").arg(activeErrorString());
        }
        return false;
    }

    m_logText->append(QString("[%1] TX (Modbus): %2")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                      .arg(packet.toHex(' ').toUpper()));

    if (m_autoScrollCheck->isChecked()) {
        m_logText->moveCursor(QTextCursor::End);
    }
    return true;
}

void MainWindow::handleSendAction(CycleMode mode)
{
    QPushButton *button = (mode == CycleRaw) ? m_sendBtn : m_sendModbusBtn;

    // Повторное нажатие активной кнопки останавливает цикл.
    if (m_cycleMode != CycleNone) {
        if (m_cycleMode == mode) {
            stopCyclicSending();
        } else {
            button->setChecked(false);
        }
        return;
    }

    QString errorMessage;
    const bool sent = (mode == CycleRaw)
                          ? sendRawOnce(&errorMessage)
                          : sendModbusOnce(&errorMessage);

    if (!sent) {
        button->setChecked(false);
        reportSendError(errorMessage);
        return;
    }

    if (!m_cycleCheck->isChecked()) {
        // В обычном режиме checkable-кнопка должна сразу вернуться в отпущенное состояние.
        button->setChecked(false);
        return;
    }

    m_cycleMode = mode;
    button->setChecked(true);
    m_cycleTimer->start(m_cyclePeriodSpin->value());

    // Пока работает один источник, не даём запустить второй с другой вкладки.
    const int rawIndex = m_dataTabWidget->indexOf(m_rawDataTab);
    const int modbusIndex = m_dataTabWidget->indexOf(m_modbusTab);
    if (rawIndex >= 0) m_dataTabWidget->setTabEnabled(rawIndex, mode == CycleRaw);
    if (modbusIndex >= 0) m_dataTabWidget->setTabEnabled(modbusIndex, mode == CycleModbus);
}

void MainWindow::handleCycleTimeout()
{
    const CycleMode mode = m_cycleMode;
    if (mode == CycleNone) {
        return;
    }

    QString errorMessage;
    const bool sent = (mode == CycleRaw)
                          ? sendRawOnce(&errorMessage)
                          : sendModbusOnce(&errorMessage);
    if (!sent) {
        // Сначала останавливаем таймер, чтобы модальное окно не повторялось.
        stopCyclicSending();
        reportSendError(errorMessage);
    }
}

void MainWindow::stopCyclicSending()
{
    if (m_cycleTimer) {
        m_cycleTimer->stop();
    }
    m_cycleMode = CycleNone;

    if (m_sendBtn) m_sendBtn->setChecked(false);
    if (m_sendModbusBtn) m_sendModbusBtn->setChecked(false);

    if (m_dataTabWidget) {
        const int rawIndex = m_dataTabWidget->indexOf(m_rawDataTab);
        const int modbusIndex = m_dataTabWidget->indexOf(m_modbusTab);
        if (rawIndex >= 0) m_dataTabWidget->setTabEnabled(rawIndex, true);
        if (modbusIndex >= 0) m_dataTabWidget->setTabEnabled(modbusIndex, true);
    }
}

void MainWindow::reportSendError(const QString &errorMessage)
{
    const QString text = errorMessage.isEmpty() ? "Не удалось отправить данные" : errorMessage;
    m_logText->append(QString("[%1] ОШИБКА отправки: %2")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                      .arg(text));
    if (m_autoScrollCheck->isChecked()) {
        m_logText->moveCursor(QTextCursor::End);
    }
    QMessageBox::warning(this, "Ошибка", text);
}

void MainWindow::onTransportChanged()
{
    bool tcp = (selectedTransport() == TransportTcp);
    m_serialSettingsWidget->setVisible(!tcp);
    m_tcpSettingsWidget->setVisible(tcp);

    // Заголовок вкладки/группы подсказывает текущий протокол.
    m_modbusGroup->setTitle(tcp ? "Modbus TCP" : "Modbus RTU");

    // Пересчитываем доступность кнопки «Подключить» под выбранный транспорт.
    if (!m_isConnected) {
        updateConnectionStatus(false);
    }
}

MainWindow::Transport MainWindow::selectedTransport() const
{
    return m_tcpRadio->isChecked() ? TransportTcp : TransportSerial;
}

bool MainWindow::isLinkOpen() const
{
    return (m_serialHandler && m_serialHandler->isOpen()) ||
           (m_tcpHandler && m_tcpHandler->isOpen());
}

bool MainWindow::writeActive(const QByteArray &data)
{
    if (m_transport == TransportTcp && m_tcpHandler && m_tcpHandler->isOpen()) {
        return m_tcpHandler->write(data);
    }
    if (m_serialHandler && m_serialHandler->isOpen()) {
        return m_serialHandler->write(data);
    }
    return false;
}

QString MainWindow::activeErrorString() const
{
    if (m_transport == TransportTcp && m_tcpHandler) {
        return m_tcpHandler->errorString();
    }
    return m_serialHandler ? m_serialHandler->errorString() : QString("Нет подключения");
}
