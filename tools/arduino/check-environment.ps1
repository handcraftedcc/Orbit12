$ErrorActionPreference = 'Stop'

$cache = Join-Path $PSScriptRoot '.tool-cache'
$localCli = Join-Path $cache 'arduino-cli-1.5.1/arduino-cli.exe'
$config = Join-Path $cache 'arduino-cli.yaml'
$cliCommand = Get-Command arduino-cli -ErrorAction SilentlyContinue
$cliPath = if (Test-Path -LiteralPath $localCli -PathType Leaf) { $localCli } elseif ($cliCommand) { $cliCommand.Source } else { $null }
if (-not $cliPath) {
    Write-Error 'arduino-cli is not installed on PATH and no pinned workspace-local CLI exists. Run tools/arduino/bootstrap.ps1.'
    exit 2
}

if ((Test-Path -LiteralPath $localCli -PathType Leaf) -and (Test-Path -LiteralPath $config -PathType Leaf)) {
    & $cliPath --config-file $config version
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $cliPath --config-file $config core list
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $cliPath --config-file $config lib list
} else {
    & $cliPath version
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $cliPath core list
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $cliPath lib list
}
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
