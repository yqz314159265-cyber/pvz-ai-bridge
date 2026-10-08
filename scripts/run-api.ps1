param(
    [Alias('attach-running', '--attach-running')]
    [switch] $AttachRunning,
    [int] $Port = 8765,
    [string] $TokenFile,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $AdditionalHostArguments
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Python = Join-Path $ProjectRoot '.venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $Python -PathType Leaf)) {
    $Python = (Get-Command python -ErrorAction Stop).Source
}
$OldPythonPath = $env:PYTHONPATH
$env:PYTHONPATH = Join-Path $ProjectRoot 'src'
if ($OldPythonPath) { $env:PYTHONPATH += [IO.Path]::PathSeparator + $OldPythonPath }
Push-Location $ProjectRoot
try {
    $HostArguments = @('--port', [string]$Port)
    if ($AttachRunning) { $HostArguments += '--attach-running' }
    if ($TokenFile) { $HostArguments += @('--token-file', $TokenFile) }
    if ($AdditionalHostArguments) { $HostArguments += $AdditionalHostArguments }
    & $Python -m pvz_ai http @HostArguments
    exit $LASTEXITCODE
}
finally {
    Pop-Location
    $env:PYTHONPATH = $OldPythonPath
}
