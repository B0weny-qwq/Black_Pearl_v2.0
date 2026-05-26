$ErrorActionPreference = "Stop"
Set-Location -LiteralPath $PSScriptRoot
$viewerScript = Join-Path $PSScriptRoot "start_ship_log_viewer.py"

if (Get-Command py -ErrorAction SilentlyContinue) {
    py -3 $viewerScript
    exit $LASTEXITCODE
}

if (Get-Command python -ErrorAction SilentlyContinue) {
    python $viewerScript
    exit $LASTEXITCODE
}

Write-Host "[viewer] Python launcher not found. Install Python or add py/python to PATH."
