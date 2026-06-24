# restore-adapter.ps1 - возврат Ethernet-адаптера в обычный режим (DHCP).
# Запускается приложением comRS485Qt уже с правами администратора.
# Откатывает то, что сделал configure-adapter.ps1: снимает статический IP,
# включает DHCP, сбрасывает DNS и перезапускает адаптер.
#
# Коды выхода: 0 - успех, 1 - ошибка.

param(
    [Parameter(Mandatory = $true)][string]$EthName,
    [switch]$EnableWifi,
    [string]$LogFile = ""
)

$ErrorActionPreference = "Stop"

function Log {
    param([string]$Message)
    if ($LogFile -ne "") {
        Add-Content -LiteralPath $LogFile -Value $Message -Encoding UTF8
    }
    Write-Host $Message
}

try {
    Log "=== Возврат адаптера $EthName в режим DHCP ==="

    # 1. Сбрасываем Ethernet на DHCP.
    Log "Сбрасываю IPv4 на DHCP..."
    Remove-NetIPAddress -InterfaceAlias $EthName -AddressFamily IPv4 -Confirm:$false -ErrorAction SilentlyContinue
    Remove-NetRoute    -InterfaceAlias $EthName -AddressFamily IPv4 -Confirm:$false -ErrorAction SilentlyContinue
    Set-NetIPInterface -InterfaceAlias $EthName -Dhcp Enabled
    Set-DnsClientServerAddress -InterfaceAlias $EthName -ResetServerAddresses
    Log "DHCP включён"

    # 2. Перезапускаем адаптер, чтобы DHCP запросил адрес.
    Log "Перезапускаю $EthName..."
    Restart-NetAdapter -Name $EthName
    Start-Sleep -Seconds 3
    Log "Адаптер перезапущен"

    # 3. (Опционально) включаем Wi-Fi обратно.
    if ($EnableWifi) {
        Log "Включаю беспроводные адаптеры..."
        $wifi = Get-NetAdapter -IncludeHidden | Where-Object {
            $_.PhysicalMediaType -eq "Native 802.11" -or
            $_.InterfaceDescription -match "Wireless|Wi-Fi|WiFi|802\.11"
        }
        foreach ($w in $wifi) {
            if ($w.Status -eq "Disabled") {
                try { Enable-NetAdapter -Name $w.Name -Confirm:$false; Log "  Включён: $($w.Name)" }
                catch { Log "  Не удалось включить $($w.Name): $_" }
            }
        }
    }

    Log "=== Готово ==="
    exit 0
}
catch {
    Log "ОШИБКА: $_"
    exit 1
}
