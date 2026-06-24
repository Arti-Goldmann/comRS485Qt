#ifndef NETWORKCONFIGURATOR_H
#define NETWORKCONFIGURATOR_H

#include <QObject>
#include <QString>
#include <QStringList>

// Настраивает Ethernet-адаптер ПК для связи с устройством по Modbus TCP:
// назначает статический IP в подсети устройства, запуская встроенный
// PowerShell-скрипт с правами администратора (UAC). Шлюз на ПК не задаётся.
class NetworkConfigurator : public QObject
{
    Q_OBJECT

public:
    struct Config {
        QString deviceIP;        // IP устройства, напр. "192.168.2.37"
        QString staticIP;        // IP, назначаемый ПК, напр. "192.168.2.2"
        int     prefix = 24;     // длина префикса подсети (CIDR)
        QString interfaceAlias;  // имя адаптера; пусто = автоопределение
        bool    toggleWifi = false; // отключать ли Wi-Fi на время работы
    };

    explicit NetworkConfigurator(QObject *parent = nullptr);

    // Запускает скрипт настройки с правами администратора и ждёт завершения.
    // Возвращает true при успехе. Текст из лога скрипта пишется в logOut,
    // текст ошибки (при неудаче) — в errorOut. Оба указателя могут быть null.
    // При успехе запоминает настроенный адаптер для последующего restore().
    bool applyConfig(const Config &cfg, QString *logOut = nullptr, QString *errorOut = nullptr);

    // Возвращает настроенный адаптер в обычный режим (DHCP). Если настройка не
    // выполнялась — ничего не делает и возвращает true. Тоже требует прав
    // администратора (UAC).
    bool restore(QString *logOut = nullptr, QString *errorOut = nullptr);

    // Была ли применена настройка адаптера (и, значит, требуется восстановление).
    bool wasApplied() const { return m_applied; }
    QString appliedInterface() const { return m_appliedAlias; }

    // Проверяет, есть ли уже у активного интерфейса адрес в подсети устройства.
    static bool isDeviceSubnetReachable(const QString &deviceIP, int prefix);

    // Список проводных сетевых интерфейсов (для выпадающего списка в UI).
    static QStringList wiredInterfaces();

    // Вычисляет IP для ПК из адреса устройства: та же подсеть /prefix,
    // свободный хост (по умолчанию .2, либо .3 если устройство уже .2).
    // Возвращает пустую строку, если deviceIP некорректен.
    static QString deriveHostIP(const QString &deviceIP, int prefix);

private:
    // Извлекает встроенный скрипт во временный файл и запускает его с правами
    // администратора (UAC), добавляя -LogFile. Возвращает код выхода процесса,
    // либо -1 (не удалось запустить) / -2 (таймаут). 1223 = отмена UAC.
    // Лог скрипта (UTF-8) пишется в logOut.
    int runElevatedScript(const QString &resourcePath, const QString &tempName,
                          const QStringList &scriptArgs, QString *logOut, QString *errorOut);

    bool m_applied = false;     // была ли применена статическая настройка
    QString m_appliedAlias;     // имя настроенного адаптера (для restore)
};

#endif // NETWORKCONFIGURATOR_H
