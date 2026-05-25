param(
    [switch]$Run
)

$ErrorActionPreference = "Stop"

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
    $defaultCmake = "C:\Program Files\CMake\bin\cmake.exe"
    if (Test-Path $defaultCmake) {
        $cmake = Get-Item $defaultCmake
    }
}

if (-not $cmake) {
    throw "CMake was not found. Install Kitware CMake or add cmake.exe to PATH."
}

$cmakeExe = $cmake.Source
if (-not $cmakeExe) {
    $cmakeExe = $cmake.FullName
}

& $cmakeExe -S . -B build-native -G "Visual Studio 17 2022" -A x64
& $cmakeExe --build build-native --config Release

if ($Run) {
    Start-Process -FilePath "$PSScriptRoot\build-native\Release\SysmonWidgetNative.exe" `
        -WorkingDirectory "$PSScriptRoot\build-native\Release"
}
