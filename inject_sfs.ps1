$hddPath = "..\hdd.img"
if (-not (Test-Path $hddPath)) { $hddPath = "hdd.img" }

$fs = [System.IO.File]::Open($hddPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite)
$w  = New-Object System.IO.BinaryWriter($fs)

$bitmap = New-Object byte[](512)

$files = @(
    @{ Name = "calc.bin";    Data = [System.IO.File]::ReadAllBytes('calc.bin') },
    @{ Name = "files.bin";   Data = [System.IO.File]::ReadAllBytes('files.bin') },
    @{ Name = "hexview.bin"; Data = [System.IO.File]::ReadAllBytes('hexview.bin') },
    @{ Name = "mines.bin";   Data = [System.IO.File]::ReadAllBytes('mines.bin') }
)

$usedSectors = 0
foreach ($f in $files) {
    $usedSectors += [math]::Max(1, [math]::Ceiling($f.Data.Length / 512))
}

$fs.Seek(200 * 512, [System.IO.SeekOrigin]::Begin)
$w.Write([uint32]0x53465332)
$w.Write([uint32]4096)
$w.Write([uint32]128)
$w.Write([uint32](4096 - $usedSectors))
$w.Write((New-Object byte[](512 - 16)))

$currentLba = 218

$fs.Seek(202 * 512, [System.IO.SeekOrigin]::Begin)

foreach ($f in $files) {
    $sectors = [math]::Ceiling($f.Data.Length / 512)
    if ($sectors -eq 0) { $sectors = 1 }

    $nameBytes = [System.Text.Encoding]::ASCII.GetBytes($f.Name)
    $w.Write($nameBytes)
    $pad = 32 - $nameBytes.Length
    if ($pad -gt 0) { $w.Write((New-Object byte[]($pad))) }

    $w.Write([uint32]$f.Data.Length)
    $w.Write([byte]1)
    $w.Write([byte]4)
    $w.Write([uint16]1)

    $w.Write([uint32]$currentLba)
    $w.Write([uint32]$sectors)

    $w.Write((New-Object byte[](16)))

    $startBit = $currentLba - 218
    for ($b = 0; $b -lt $sectors; $b++) {
        $bit = $startBit + $b
        $byteIdx = [math]::Floor($bit / 8)
        $bitmap[$byteIdx] = $bitmap[$byteIdx] -bor (1 -shl ($bit % 8))
    }

    $currentLba += $sectors
}

$remainingInodes = 128 - $files.Count
$w.Write((New-Object byte[]($remainingInodes * 64)))

$fs.Seek(201 * 512, [System.IO.SeekOrigin]::Begin)
$w.Write($bitmap)

$fs.Seek(218 * 512, [System.IO.SeekOrigin]::Begin)
foreach ($f in $files) {
    $w.Write($f.Data)
    $sectorPadding = (512 - ($f.Data.Length % 512)) % 512
    if ($sectorPadding -gt 0) {
        $w.Write((New-Object byte[]($sectorPadding)))
    }
}

$w.Flush()
$w.Close()
$fs.Close()