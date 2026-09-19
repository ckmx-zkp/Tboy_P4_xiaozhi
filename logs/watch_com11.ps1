$path = 'D:\Home_Work\ESP32_XIAOZHI\xiaozhi-esp32\logs\260818_COM11.log'
$pattern = 'Wake word|Activation|Device-Id|WS: |Application: >>|Application: <<|ERROR|E \(|abort|MCP:|tool_call|memory|State: .* -> idle|State: .* -> connecting|sample rate|goodbye|休息|闭眼|睡觉|EspTcp|disconnect|OTA:|speaking -> listening|listening -> speaking'
$last = 0
if (Test-Path -LiteralPath $path) {
    $last = (Get-Item -LiteralPath $path).Length
}
while ($true) {
    Start-Sleep -Milliseconds 800
    if (-not (Test-Path -LiteralPath $path)) { continue }
    $item = Get-Item -LiteralPath $path
    $len = $item.Length
    if ($len -lt $last) { $last = 0 }
    if ($len -le $last) { continue }
    $fs = [System.IO.File]::Open($path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try {
        [void]$fs.Seek($last, [System.IO.SeekOrigin]::Begin)
        $sr = New-Object System.IO.StreamReader($fs, [System.Text.Encoding]::UTF8, $true)
        $chunk = $sr.ReadToEnd()
        $last = $fs.Position
        $sr.Dispose()
    } finally {
        $fs.Dispose()
    }
    foreach ($line in ($chunk -split "`r?`n")) {
        if ($line -match $pattern) {
            Write-Output $line
        }
    }
}
