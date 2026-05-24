$ErrorActionPreference = "Stop"

python -m PyInstaller `
  --noconfirm `
  --clean `
  --onedir `
  --windowed `
  --name SysmonWidget `
  --hidden-import pystray._win32 `
  --hidden-import winrt.windows.media.control `
  main.py

Write-Host "Build complete: dist\SysmonWidget\SysmonWidget.exe"
