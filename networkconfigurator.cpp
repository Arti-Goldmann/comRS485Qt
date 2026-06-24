#include "networkconfigurator.h"

#include <QFile>
#include <QDir>
#include <QProcess>
#include <QTextStream>
#include <QStringConverter>
#include <QNetworkInterface>
#include <QHostAddress>
#include <QDebug>

NetworkConfigurator::NetworkConfigurator(QObject *parent) : QObject(parent)
{
}

// Экранирует строку для вставки в одинарные кавычки PowerShell.
static QString psQuote(const QString &s)
{
    QString escaped = s;
    escaped.replace("'", "''");
    return "'" + escaped + "'";
}

int NetworkConfigurator::runElevatedScript(const QString &resourcePath, const QString &tempName,
                                           const QStringList &scriptArgs, QString *logOut,
                                           QString *errorOut)
{
    // 1. Извлекаем встроенный скрипт во временный файл.
    const QString scriptPath = QDir(QDir::tempPath()).filePath(tempName);
    const QString logPath    = QDir(QDir::tempPath()).filePath(tempName + ".log");

    QFile::remove(scriptPath);
    if (!QFile::copy(resourcePath, scriptPath)) {
        if (errorOut) *errorOut = "Не удалось извлечь скрипт во временную папку";
        return -1;
    }
    // Ресурс копируется только для чтения — снимаем ограничение, чтобы можно было
    // перезаписать файл при следующем запуске.
    QFile::setPermissions(scriptPath,
                          QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
    QFile::remove(logPath);

    // 2. Формируем список аргументов для внутреннего (elevated) powershell.
    QStringList innerArgs;
    innerArgs << "-NoProfile" << "-ExecutionPolicy" << "Bypass" << "-File" << scriptPath;
    innerArgs << scriptArgs;
    innerArgs << "-LogFile" << logPath;

    QStringList quoted;
    for (const QString &a : innerArgs) {
        quoted << psQuote(a);
    }
    const QString argList = quoted.join(",");

    // Внешний (неэлевейтед) powershell поднимает UAC для внутреннего и ждёт его.
    // Код выхода 1223 — пользователь отменил запрос UAC.
    const QString command = QString(
        "$ErrorActionPreference='Stop'; "
        "try { "
        "$p = Start-Process -FilePath 'powershell' -ArgumentList @(%1) -Verb RunAs -PassThru -Wait; "
        "exit $p.ExitCode "
        "} catch { exit 1223 }").arg(argList);

    // 3. Запускаем и ждём завершения (UAC может ждать ввода пользователя).
    QProcess process;
    process.start("powershell",
                  {"-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", command});
    if (!process.waitForStarted(5000)) {
        if (errorOut) *errorOut = "Не удалось запустить powershell.exe";
        return -1;
    }
    if (!process.waitForFinished(120000)) {
        process.kill();
        if (errorOut) *errorOut = "Таймаут ожидания скрипта";
        return -2;
    }

    // 4. Читаем лог скрипта (UTF-8) для показа в окне приложения.
    if (logOut) {
        QFile logFile(logPath);
        if (logFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&logFile);
            in.setEncoding(QStringConverter::Utf8);
            *logOut = in.readAll().trimmed();
            logFile.close();
        }
    }

    return process.exitCode();
}

bool NetworkConfigurator::applyConfig(const Config &cfg, QString *logOut, QString *errorOut)
{
    QStringList args;
    args << "-DeviceIP" << cfg.deviceIP
         << "-StaticIP" << cfg.staticIP
         << "-Prefix" << QString::number(cfg.prefix);
    if (!cfg.interfaceAlias.isEmpty()) {
        args << "-EthName" << cfg.interfaceAlias;
    }
    if (!cfg.toggleWifi) {
        args << "-NoWifiToggle";
    }

    QString log;
    const int exitCode = runElevatedScript(":/scripts/configure-adapter.ps1",
                                           "comRS485Qt_configure-adapter.ps1", args, &log, errorOut);
    if (logOut) *logOut = log;

    if (exitCode == 1223) {
        if (errorOut) *errorOut = "Настройка отменена (не предоставлены права администратора)";
        return false;
    }
    if (exitCode < 0) {
        return false; // errorOut уже заполнен
    }

    // Имя реально настроенного адаптера: маркер из лога, иначе явно заданный алиас.
    QString resolvedAlias = cfg.interfaceAlias;
    const QStringList lines = log.split('\n');
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith("RESULT-ADAPTER:")) {
            resolvedAlias = trimmed.mid(QString("RESULT-ADAPTER:").length()).trimmed();
            break;
        }
    }

    // Успех: либо скрипт вернул 0, либо адаптер фактически в нужной подсети.
    if (exitCode == 0 || isDeviceSubnetReachable(cfg.deviceIP, cfg.prefix)) {
        if (!resolvedAlias.isEmpty()) {
            m_applied = true;
            m_appliedAlias = resolvedAlias;
        }
        return true;
    }

    if (errorOut) *errorOut = QString("Скрипт настройки завершился с кодом %1").arg(exitCode);
    return false;
}

bool NetworkConfigurator::restore(QString *logOut, QString *errorOut)
{
    if (!m_applied || m_appliedAlias.isEmpty()) {
        return true; // нечего восстанавливать
    }

    QStringList args;
    args << "-EthName" << m_appliedAlias;

    QString log;
    const int exitCode = runElevatedScript(":/scripts/restore-adapter.ps1",
                                           "comRS485Qt_restore-adapter.ps1", args, &log, errorOut);
    if (logOut) *logOut = log;

    if (exitCode == 1223) {
        if (errorOut) *errorOut = "Восстановление отменено (не предоставлены права администратора)";
        return false;
    }
    if (exitCode < 0) {
        return false; // errorOut уже заполнен
    }
    if (exitCode == 0) {
        m_applied = false;
        m_appliedAlias.clear();
        return true;
    }

    if (errorOut) *errorOut = QString("Скрипт восстановления завершился с кодом %1").arg(exitCode);
    return false;
}

bool NetworkConfigurator::isDeviceSubnetReachable(const QString &deviceIP, int prefix)
{
    QHostAddress device(deviceIP);
    if (device.isNull() || device.protocol() != QAbstractSocket::IPv4Protocol) {
        return false;
    }

    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        const QNetworkInterface::InterfaceFlags flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) ||
            !(flags & QNetworkInterface::IsRunning) ||
            (flags & QNetworkInterface::IsLoopBack)) {
            continue;
        }

        const QList<QNetworkAddressEntry> entries = iface.addressEntries();
        for (const QNetworkAddressEntry &entry : entries) {
            const QHostAddress ip = entry.ip();
            if (ip.protocol() != QAbstractSocket::IPv4Protocol) {
                continue;
            }
            if (ip.isInSubnet(device, prefix)) {
                return true;
            }
        }
    }
    return false;
}

QStringList NetworkConfigurator::wiredInterfaces()
{
    QStringList result;
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        if (iface.type() != QNetworkInterface::Ethernet) {
            continue;
        }
        if (iface.flags() & QNetworkInterface::IsLoopBack) {
            continue;
        }
        const QString name = iface.humanReadableName();
        if (!name.isEmpty() && !result.contains(name)) {
            result << name;
        }
    }
    return result;
}

QString NetworkConfigurator::deriveHostIP(const QString &deviceIP, int prefix)
{
    QHostAddress device(deviceIP);
    if (device.isNull() || device.protocol() != QAbstractSocket::IPv4Protocol ||
        prefix < 0 || prefix > 32) {
        return QString();
    }

    const quint32 ip = device.toIPv4Address();
    const quint32 mask = (prefix == 0) ? 0u : (0xFFFFFFFFu << (32 - prefix));
    const quint32 network = ip & mask;
    const quint32 broadcast = network | ~mask;

    // Подбираем свободный хост, начиная с .2 (пропускаем .1 — обычно шлюз),
    // не совпадающий с адресом устройства, сетевым и широковещательным.
    for (quint32 host = 2; host < 255; ++host) {
        const quint32 candidate = network | host;
        if (candidate <= network || candidate >= broadcast) {
            break; // вышли за пределы подсети
        }
        if (candidate != ip) {
            return QHostAddress(candidate).toString();
        }
    }
    return QString();
}
