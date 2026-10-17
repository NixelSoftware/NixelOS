$hddPath = "..\hdd.img"
if (-not (Test-Path $hddPath)) { $hddPath = "hdd.img" }

$fs = [System.IO.File]::Open($hddPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite)
$w  = New-Object System.IO.BinaryWriter($fs)

# Seek to LBA 200 (Superblock)
$fs.Seek(200 * 512, [System.IO.SeekOrigin]::Begin)

# Superblock: Magic ('SFS1') + File Count (4)
$w.Write([uint32]0x53465331)
$w.Write([uint32]4)

function Write-FS-Entry($name, $size, $lba) {
    $nameBytes = [System.Text.Encoding]::ASCII.GetBytes($name)
    $w.Write($nameBytes)
    $pad = 16 - $nameBytes.Length
    if ($pad -gt 0) {
        $w.Write((New-Object byte[]($pad)))
    }
    $w.Write([uint32]$size)
    $w.Write([uint32]$lba)
    $w.Write([byte]1)
    $w.Write((New-Object byte[](3)))
}

$filesData   = [System.IO.File]::ReadAllBytes('files.bin')
$minesData   = [System.IO.File]::ReadAllBytes('mines.bin')
$calcData    = [System.IO.File]::ReadAllBytes('calc.bin')
$hexviewData = [System.IO.File]::ReadAllBytes('hexview.bin')

# Write Directory Entries (28 bytes each)
Write-FS-Entry "files.bin"   $filesData.Length   208
Write-FS-Entry "mines.bin"   $minesData.Length   240
Write-FS-Entry "calc.bin"    $calcData.Length    272
Write-FS-Entry "hexview.bin" $hexviewData.Length 304

$remainingEntries = 32 - 4
$w.Write((New-Object byte[]($remainingEntries * 28)))

# Write binary payloads
$fs.Seek(208 * 512, [System.IO.SeekOrigin]::Begin)
$w.Write($filesData)

$fs.Seek(240 * 512, [System.IO.SeekOrigin]::Begin)
$w.Write($minesData)

$fs.Seek(272 * 512, [System.IO.SeekOrigin]::Begin)
$w.Write($calcData)

$fs.Seek(304 * 512, [System.IO.SeekOrigin]::Begin)
$w.Write($hexviewData)

$w.Close()
$fs.Close()

Write-Host "SFS injection complete: files.bin, mines.bin, calc.bin, hexview.bin written."