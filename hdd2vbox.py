import os
import shutil
import subprocess
import sys
from pathlib import Path


def find_converter():
    """Finds either VBoxManage or qemu-img on the system."""
    # Check system PATH first
    vbox = shutil.which("VBoxManage") or shutil.which("VBoxManage.exe")
    if vbox:
        return ("vbox", vbox)

    # Standard Windows install locations
    win_paths = [
        Path(os.environ.get("ProgramFiles", "C:\\Program Files"))
        / "Oracle"
        / "VirtualBox"
        / "VBoxManage.exe",
        Path(os.environ.get("ProgramFiles(x86)", "C:\\Program Files (x86)"))
        / "Oracle"
        / "VirtualBox"
        / "VBoxManage.exe",
    ]
    for path in win_paths:
        if path.is_file():
            return ("vbox", str(path))

    # Fallback to qemu-img
    qemu = shutil.which("qemu-img") or shutil.which("qemu-img.exe")
    if qemu:
        return ("qemu", qemu)

    return (None, None)


def convert_raw_to_vdi():
    # 1. Locate source hdd.img
    raw_path = Path("../hdd.img")
    if not raw_path.exists():
        raw_path = Path("hdd.img")

    if not raw_path.exists():
        print("Error: Could not locate 'hdd.img'.", file=sys.stderr)
        sys.exit(1)

    vdi_path = raw_path.with_suffix(".vdi")

    # 2. Locate tool
    tool_type, tool_path = find_converter()
    if not tool_type:
        print(
            "Error: Neither VBoxManage nor qemu-img was found. "
            "Please install VirtualBox or QEMU.",
            file=sys.stderr,
        )
        sys.exit(1)

    # 3. Clean up existing VDI file to prevent overwrite/UUID collision issues
    if vdi_path.exists():
        print(f"Removing existing '{vdi_path}'...")
        vdi_path.unlink()

    # 4. Run conversion
    print(f"Converting '{raw_path}' -> '{vdi_path}' using {tool_type}...")

    if tool_type == "vbox":
        cmd = [tool_path, "convertfromraw", str(raw_path), str(vdi_path), "--format", "VDI"]
    else:
        cmd = [tool_path, "convert", "-f", "raw", "-O", "vdi", str(raw_path), str(vdi_path)]

    result = subprocess.run(cmd)

    if result.returncode == 0 and vdi_path.exists():
        print(f"Conversion successful: '{vdi_path}' created.")
    else:
        print(f"Conversion failed with exit code {result.returncode}.", file=sys.stderr)
        sys.exit(result.returncode)


if __name__ == "__main__":
    convert_raw_to_vdi()