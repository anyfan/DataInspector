param(
    [Parameter(Mandatory = $true)]
    [string]$PackageDir,
    [int]$WaitSeconds = 15
)

$ErrorActionPreference = 'Stop'
$package = (Resolve-Path -LiteralPath $PackageDir).Path
foreach ($file in @('DataInspector.exe', 'Qt6Core.dll', 'platforms/qwindows.dll',
                    'imageformats/qsvg.dll', 'iconengines/qsvgicon.dll', 'qml/QtQuick/qmldir')) {
    if (!(Test-Path -LiteralPath (Join-Path $package $file))) {
        throw "Missing packaged dependency: $file"
    }
}

# The child must resolve dependencies from the package, never the installed SDK.
$variables = @('PATH', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH',
               'QML_IMPORT_PATH', 'QML2_IMPORT_PATH', 'QT_QPA_PLATFORM',
               'QT_QUICK_CONTROLS_STYLE', 'QT_QUICK_BACKEND')
$saved = @{}
foreach ($name in $variables) {
    $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    [Environment]::SetEnvironmentVariable($name, $null, 'Process')
}
$process = $null
$stdout = Join-Path $env:TEMP "datainspector-$([guid]::NewGuid())-stdout.txt"
$stderr = Join-Path $env:TEMP "datainspector-$([guid]::NewGuid())-stderr.txt"
try {
    $env:PATH = "$package;$env:SystemRoot/System32;$env:SystemRoot"
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:QT_QUICK_CONTROLS_STYLE = 'Fusion'
    $env:QT_QUICK_BACKEND = 'software'
    $process = Start-Process -FilePath (Join-Path $package 'DataInspector.exe') `
        -WorkingDirectory $package -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    if ($process.WaitForExit($WaitSeconds * 1000)) {
        throw "Packaged application exited during startup (exit code $($process.ExitCode))."
    }
    $errors = Get-Content -LiteralPath $stderr -Raw -ErrorAction SilentlyContinue
    if ($errors -match 'failed to load|is not installed|Cannot load library|Type .* unavailable|ReferenceError|QQmlApplicationEngine failed') {
        throw "Packaged QML/dependency startup failed: $errors"
    }
    Write-Host "Packaged application stayed running for $WaitSeconds seconds with SDK paths removed."
}
finally {
    if ($null -ne $process -and !$process.HasExited) { Stop-Process -Id $process.Id -Force }
    foreach ($file in @($stdout, $stderr)) {
        if (Test-Path -LiteralPath $file) {
            Get-Content -LiteralPath $file | Write-Host
            Remove-Item -LiteralPath $file -Force
        }
    }
    foreach ($name in $variables) {
        [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process')
    }
}
