$pipeName = "86Box\test"
$serverName = "."

Write-Host "Connecting to \\.\pipe\$pipeName..." -ForegroundColor Cyan

try {
    # Initialize NamedPipeClientStream (Direction: InOut or In)
    $pipe = [System.IO.Pipes.NamedPipeClientStream]::new(
        $serverName,
        $pipeName,
        [System.IO.Pipes.PipeDirection]::In
    )

    # Connect with a 10-second timeout
    $pipe.Connect(10000)
    Write-Host "Connected. Reading COM1 output (Press Ctrl+C to stop)..." -ForegroundColor Green

    $reader = [System.IO.StreamReader]::new($pipe)

    while ($pipe.IsConnected -and -not $reader.EndOfStream) {
        $line = $reader.ReadLine()
        if ($null -ne $line) {
            Write-Host $line
        }
    }
}
catch [System.TimeoutException] {
    Write-Error "Connection timed out. Ensure 86Box is running and the pipe is created."
}
catch {
    Write-Error $_.Exception.Message
}
finally {
    if ($reader) { $reader.Dispose() }
    if ($pipe) { $pipe.Dispose() }
    Write-Host "Pipe closed." -ForegroundColor Yellow
}