[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Python,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$CliArguments
)

$ErrorActionPreference = 'Stop'
$candidates = [System.Collections.Generic.List[string]]::new()
$explicitPython = if ($Python) { $Python } else { $env:DATAINSPECTOR_PYTHON }
if ($explicitPython) {
    if (-not [System.IO.Path]::IsPathRooted($explicitPython)) {
        @{ok=$false; error=@{code='python_startup_failed'; message='Python override must be an absolute executable path.'}} | ConvertTo-Json -Compress
        exit 1
    }
    $candidates.Add($explicitPython)
} else {
    foreach ($command in @(Get-Command python.exe -CommandType Application -All -ErrorAction SilentlyContinue)) { $candidates.Add($command.Source) }
    foreach ($directory in @(
        (Join-Path $env:APPDATA 'uv/python'),
        (Join-Path $env:LOCALAPPDATA 'Programs/Python'),
        (Join-Path $env:LOCALAPPDATA 'Python')
    )) {
        if (Test-Path -LiteralPath $directory -PathType Container) {
            foreach ($runtime in @(Get-ChildItem -LiteralPath $directory -Directory | Sort-Object Name -Descending)) {
                $candidates.Add((Join-Path $runtime.FullName 'python.exe'))
            }
        }
    }
}

$selectedPython = $null
$lastDiagnostic = 'No installed interpreter found.'
foreach ($candidate in @($candidates | Select-Object -Unique)) {
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
    $probe = [System.Diagnostics.Process]::new()
    try {
        $probe.StartInfo.FileName = $candidate
        $probe.StartInfo.Arguments = '-I -c "import json,sys; print(json.dumps([sys.executable,list(sys.version_info[:2])]))"'
        $probe.StartInfo.UseShellExecute = $false
        $probe.StartInfo.CreateNoWindow = $true
        $probe.StartInfo.RedirectStandardOutput = $true
        $probe.StartInfo.RedirectStandardError = $true
        $probe.StartInfo.StandardOutputEncoding = [System.Text.UTF8Encoding]::new($false)
        [void]$probe.Start()
        if (-not $probe.WaitForExit(5000)) { $probe.Kill(); $lastDiagnostic = 'Interpreter startup timed out.'; continue }
        $stdout = $probe.StandardOutput.ReadToEnd()
        $stderr = $probe.StandardError.ReadToEnd()
        if ($probe.ExitCode -ne 0 -or $stderr.Trim()) { $lastDiagnostic = $stderr.Trim(); continue }
        $identity = $stdout | ConvertFrom-Json
        if ($identity[1][0] -lt 3 -or ($identity[1][0] -eq 3 -and $identity[1][1] -lt 9)) { continue }
        # Run the exact executable that passed the probe; do not re-enter a noisy alias.
        $selectedPython = $candidate
        break
    } catch { $lastDiagnostic = $_.Exception.Message }
    finally { $probe.Dispose() }
}
if (-not $selectedPython) {
    @{ok=$false; error=@{code='python_startup_failed'; message=('No Python 3.9+ interpreter with clean startup was found. Set DATAINSPECTOR_PYTHON or -Python to a working executable. ' + $lastDiagnostic)}} | ConvertTo-Json -Compress
    exit 1
}
# Only discovery probes are captured. Preserve all stdout/stderr and the exit code of the actual CLI.
& $selectedPython (Join-Path $PSScriptRoot 'datainspector_cli.py') @CliArguments
exit $LASTEXITCODE
