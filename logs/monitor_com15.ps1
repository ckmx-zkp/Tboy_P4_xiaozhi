$ErrorActionPreference = 'Stop'
$logPath = 'D:\Home_Work\ESP32_XIAOZHI\xiaozhi-esp32\logs\260819_COM15.log'
$port = [System.IO.Ports.SerialPort]::new('COM15', 115200, 'None', 8, 'One')
$port.NewLine = "`n"
$port.ReadTimeout = 1000
$utf8 = [System.Text.UTF8Encoding]::new($false)
$port.Encoding = $utf8
$writer = [System.IO.StreamWriter]::new($logPath, $true, $utf8)
$writer.AutoFlush = $true
try {
    $port.Open()
    $writer.WriteLine("===== COM15 monitor started $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') =====")
    while ($true) {
        try {
            $line = $port.ReadLine().TrimEnd("`r")
            $writer.WriteLine($line)
        } catch [System.TimeoutException] {
            continue
        }
    }
} finally {
    if ($port.IsOpen) { $port.Close() }
    $port.Dispose()
    $writer.Dispose()
}
