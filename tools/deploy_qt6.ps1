param(
    [string]$QtDir = "D:/Software/Qt/6.8.3/llvm-mingw_64",
    [string]$BuildDir = "build_qt6",
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"
$exe = Join-Path $BuildDir "bin/DataInspector.exe"
if (-not (Test-Path $exe)) {
    throw "Executable not found: $exe. Build the Release target first."
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $BuildDir "bin"
}

$qtBin = Join-Path $QtDir "bin"
$qtQml = Join-Path $QtDir "qml"
$qtPlugins = Join-Path $QtDir "plugins"
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

Copy-Item $exe (Join-Path $OutputDir "DataInspector.exe") -Force
Copy-Item (Join-Path $qtBin "Qt6*.dll") $OutputDir -Force
foreach ($runtime in @("libc++.dll", "libunwind.dll")) {
    $runtimePath = Join-Path $qtBin $runtime
    if (Test-Path $runtimePath) { Copy-Item $runtimePath $OutputDir -Force }
}
Copy-Item (Join-Path $qtPlugins "platforms") (Join-Path $OutputDir "platforms") -Recurse -Force

# Qt Quick Controls, Dialogs and Layouts are QML modules, not just DLLs.
$qmlOutput = Join-Path $OutputDir "qml"
New-Item -ItemType Directory -Force -Path $qmlOutput | Out-Null
foreach ($module in @("QtQuick", "QtQml", "QML", "Qt")) {
    Copy-Item (Join-Path $qtQml $module) $qmlOutput -Recurse -Force
}

Write-Host "Deployed DataInspector to $OutputDir"
