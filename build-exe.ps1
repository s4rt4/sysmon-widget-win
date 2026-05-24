$ErrorActionPreference = "Stop"

python -m PyInstaller `
  --noconfirm `
  --clean `
  --onedir `
  --windowed `
  --noupx `
  --name SysmonWidget `
  --version-file version_info.txt `
  --hidden-import pystray._win32 `
  --hidden-import winrt.windows.media.control `
  sysmon_widget.py

if ($LASTEXITCODE -ne 0) {
  throw "PyInstaller failed with exit code $LASTEXITCODE"
}

Write-Host "Build complete: dist\SysmonWidget\SysmonWidget.exe"
