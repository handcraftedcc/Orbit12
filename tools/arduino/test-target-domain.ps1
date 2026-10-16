param([string]$BuildDirectory = 'build/domain-target')
$ErrorActionPreference = 'Stop'
$migrationRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$migrationCompiler = Join-Path $PSScriptRoot '.tool-cache/data/internal/rp2040_pqt-gcc_5.0.0-9576866_4ae0b76ce4c23340/bin/arm-none-eabi-g++.exe'
if (-not (Test-Path -LiteralPath $migrationCompiler)) { throw 'Pinned RP2040 compiler unavailable; provision T00 profile first.' }
$migrationBuild = Join-Path $migrationRoot $BuildDirectory
New-Item -ItemType Directory -Path $migrationBuild -Force | Out-Null
& $migrationCompiler --version
if ($LASTEXITCODE) { exit $LASTEXITCODE }
$migrationFlags = @('-std=c++17','-mcpu=cortex-m0plus','-mthumb','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-fstack-usage','-Wstack-usage=8192','-fno-exceptions','-fno-rtti',"-I$(Join-Path $migrationRoot 'firmware/Orbit12/src')")
foreach ($migrationArea in @('domain','persistence')) {
  $migrationDirectory = Join-Path $migrationRoot "firmware/Orbit12/src/$migrationArea"
  if (-not (Test-Path -LiteralPath $migrationDirectory)) { continue }
  foreach ($migrationSource in Get-ChildItem -LiteralPath $migrationDirectory -Filter '*.cpp' -Recurse) {
    $migrationName = $migrationSource.FullName.Substring($migrationRoot.Length + 1).Replace('\','_').Replace('/','_')
    & $migrationCompiler @migrationFlags -c $migrationSource.FullName -o (Join-Path $migrationBuild "$migrationName.o")
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
  }
}
$migrationProbe = Join-Path $migrationRoot 'tests/native/t11a_target_probe.cpp'
$migrationAssembly = Join-Path $migrationBuild 't11a_target_probe.s'
& $migrationCompiler @migrationFlags -S $migrationProbe -o $migrationAssembly
if ($LASTEXITCODE) { exit $LASTEXITCODE }
$migrationCode = Get-Content -LiteralPath $migrationAssembly -Raw
if ($migrationCode -match '__atomic|__sync|spin_lock') { throw 'Bridge probe contains an unexpected synchronization runtime call.' }
if ($migrationCode -notmatch 'dmb') { throw 'Bridge probe is missing acquire/release barriers.' }
Write-Output 'Target domain compilation and bridge atomic helper/barrier checks passed; hardware timing remains unverified.'
