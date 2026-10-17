$hddPath = "..\hdd.img"
if (-not (Test-Path $hddPath)) { $hddPath = "hdd.img" }

$fs = [System.IO.File]::Open($hddPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite)
$w  = New-Object System.IO.BinaryWriter($fs)

# Seek to LBA 200 
$fs.Seek(200 * 512, [System.IO.SeekOrigin]::Begin)

# Superblock: Magic ('SFS1') + File Count (2)
$w.Write([uint32]0x53465331)
$w.Write([uint32]2)


function Write-FS-Entry($name, $size, $lba) {
    # 16-byte fixed name buffer
    $nameBytes = [System.Text.Encoding]::ASCII.GetBytes($name)
    $w.Write($nameBytes)
    $pad = 16 - $nameBytes.Length
    if ($pad -gt 0) {
        $w.Write((New-Object byte[]($pad)))
    }

    # 4-byte size_bytes
    $w.Write([uint32]$size)

    # 4-byte start_lba
    $w.Write([uint32]$lba)

    # 1-byte used flag
    $w.Write([byte]1)

    # 3-byte compiler padding (16 + 4 + 4 + 1 + 3 = 28 bytes)
    $w.Write((New-Object byte[](3)))
}

$filesData = [System.IO.File]::ReadAllBytes('files.bin')
$minesData = [System.IO.File]::ReadAllBytes('mines.bin')

# Write Directory Entries (28 bytes each)
Write-FS-Entry "files.bin" $filesData.Length 208
Write-FS-Entry "mines.bin" $minesData.Length 240

$remainingEntries = 32 - 2
$w.Write((New-Object byte[]($remainingEntries * 28)))

# 3. Write binary payloads
$fs.Seek(208 * 512, [System.IO.SeekOrigin]::Begin)
$w.Write($filesData)

$fs.Seek(240 * 512, [System.IO.SeekOrigin]::Begin)
$w.Write($minesData)

$w.Close()
$fs.Close()
Write-Host "SFS injection complete: files.bin and mines.bin written successfully."