# configure-adapter.ps1 - настройка Ethernet-адаптера ПК для связи с устройством
# по Modbus TCP. Запускается приложением comRS485Qt уже с правами администратора
# (через Start-Process -Verb RunAs), поэтому самоповышение прав здесь не нужно.
#
# Назначает статический IP в подсети устройства. Шлюз НЕ задаётся намеренно:
# для прямого обмена ПК <-> устройство в одной подсети он не нужен, а его
# отсутствие сохраняет маршрут по умолчанию (интернет) на Wi-Fi.
#
# Коды выхода: 0 - успех, 1 - ошибка.

param(
    [Parameter(Mandatory = $true)][string]$DeviceIP,
    [Parameter(Mandatory = $true)][string]$StaticIP,
    [int]$Prefix = 24,
    [string]$EthName = "",
    [switch]$NoWifiToggle,
    [string]$LogFile = ""
)

$ErrorActionPreference = "Stop"

# --- Логирование в файл (приложение прочитает и покажет в окне) ---
function Log {
    param([string]$Message)
    if ($LogFile -ne "") {
        Add-Content -LiteralPath $LogFile -Value $Message -Encoding UTF8
    }
    Write-Host $Message
}

try {
    Log "=== Настройка адаптера для устройства $DeviceIP ==="

    # 1. Определяем имя Ethernet-адаптера, если не задано явно.
    if ($EthName -eq "") {
        $adapter = Get-NetAdapter -Physical |
            Where-Object {
                $_.Status -eq "Up" -and
                $_.InterfaceDescription -notmatch "Wireless|Wi-Fi|WiFi|802\.11"
            } |
            Sort-Object -Property ifIndex |
            Select-Object -First 1

        if (-not $adapter) {
            Log "ОШИБКА: не найден активный проводной адаптер. Подключите кабель Ethernet."
            exit 1
        }
        $EthName = $adapter.Name
    }
    Log "Адаптер: $EthName"
    # Машиночитаемый маркер для приложения (имя настроенного адаптера).
    Log "RESULT-ADAPTER:$EthName"

    # 2. Чистим старые адреса и маршруты IPv4 на адаптере, отключаем DHCP.
    Log "Очищаю старые настройки IPv4..."
    Remove-NetIPAddress -InterfaceAlias $EthName -AddressFamily IPv4 -Confirm:$false -ErrorAction SilentlyContinue
    Remove-NetRoute    -InterfaceAlias $EthName -AddressFamily IPv4 -Confirm:$false -ErrorAction SilentlyContinue
    Set-NetIPInterface -InterfaceAlias $EthName -Dhcp Disabled -ErrorAction SilentlyContinue

    # 3. Назначаем статический IP (без -DefaultGateway - шлюз не нужен).
    Log "Назначаю $StaticIP/$Prefix на $EthName..."
    New-NetIPAddress -InterfaceAlias $EthName -IPAddress $StaticIP -PrefixLength $Prefix | Out-Null

    # 4. Ждём, пока адрес перейдёт в состояние Preferred (до 10 секунд).
    $state = $null
    for ($i = 0; $i -lt 20; $i++) {
        Start-Sleep -Milliseconds 500
        $state = (Get-NetIPAddress -InterfaceAlias $EthName -IPAddress $StaticIP -ErrorAction SilentlyContinue).AddressState
        if ($state -eq "Preferred") { break }
    }
    if ($state -eq "Preferred") {
        Log "Адрес $StaticIP активен (Preferred)"
    } else {
        Log "ВНИМАНИЕ: статус адреса = $state"
    }

    # 5. (Опционально) отключаем Wi-Fi. По умолчанию НЕ трогаем: разные подсети
    #    сосуществуют, интернет может оставаться на Wi-Fi.
    if (-not $NoWifiToggle) {
        Log "Отключаю беспроводные адаптеры..."
        $wifi = Get-NetAdapter | Where-Object {
            $_.PhysicalMediaType -eq "Native 802.11" -or
            $_.InterfaceDescription -match "Wireless|Wi-Fi|WiFi|802\.11"
        }
        foreach ($w in $wifi) {
            if ($w.Status -ne "Disabled") {
                try { Disable-NetAdapter -Name $w.Name -Confirm:$false; Log "  Отключён: $($w.Name)" }
                catch { Log "  Не удалось отключить $($w.Name): $_" }
            }
        }
    }

    # 6. Проверяем связь с устройством (диагностика, на результат не влияет).
    Log "Проверяю связь с $DeviceIP..."
    if (Test-Connection -ComputerName $DeviceIP -Count 2 -Quiet) {
        Log "Устройство $DeviceIP отвечает на ping."
    } else {
        Log "Устройство $DeviceIP не отвечает на ping (это нормально, если ICMP отключён; Modbus TCP может работать)."
    }

    Log "=== Готово ==="
    exit 0
}
catch {
    Log "ОШИБКА: $_"
    exit 1
}
