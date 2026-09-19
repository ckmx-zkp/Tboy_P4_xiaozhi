$path = 'D:\Home_Work\ESP32_XIAOZHI\xiaozhi-esp32\logs\260819_COM15.log'
$pattern = 'ESP_S3_LCD_EV_Board|GC9503|Device Identity|Device-Id|MAC|Wake word|Activation|WiFi|IP|OTA:|WebSocket|WS: |Application: >>|Application: <<|State:|ERROR|E \(|abort|MCP:|tool_call|sample rate|EspTcp|disconnect|Guru Meditation|panic|assert|reboot'
$last = if (Test-Path -LiteralPath $path) { (Get-Item -LiteralPath $path).Length } else { 0 }
while ($true) {
    Start-Sleep -Milliseconds 800
    if (-not (Test-Path -LiteralPath $path)) { continue }
    $item = Get-Item -LiteralPath $path
    $len = $item.Length
    if ($len -lt $last) { $last = 0 }
    if ($len -le $last) { continue }
    $fs = [System.IO.File]::Open($path, 'Open', 'Read', 'ReadWrite')
    try {
        [void]$fs.Seek($last, 'Begin')
        $reader = [System.IO.StreamReader]::new($fs, [System.Text.Encoding]::UTF8, $true)
        $chunk = $reader.ReadToEnd()
        $last = $fs.Position
        $reader.Dispose()
    } finally {
        $fs.Dispose()
    }
    foreach ($line in ($chunk -split "`r?`n")) {
        if ($line -match $pattern) { Write-Output $line }
    }
}
