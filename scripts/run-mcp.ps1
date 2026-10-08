param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $HostArguments
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
    # MCP stdio owns stdout. Do not print launcher status or banners here.
    & $Python -m pvz_ai mcp @HostArguments
    exit $LASTEXITCODE
}
finally {
    Pop-Location
    $env:PYTHONPATH = $OldPythonPath
}
