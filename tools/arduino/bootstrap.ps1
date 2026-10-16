[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$version = '1.5.1'
$root = Join-Path $PSScriptRoot '.tool-cache'
$installDir = Join-Path $root "arduino-cli-$version"
$configPath = Join-Path $root 'arduino-cli.yaml'
$archiveName = "arduino-cli_${version}_Windows_64bit.zip"
$checksumName = "$version-checksums.txt"
$releaseUrl = "https://github.com/arduino/arduino-cli/releases/download/v$version"

New-Item -ItemType Directory -Path $root -Force | Out-Null
New-Item -ItemType Directory -Path $installDir -Force | Out-Null
$checksumsPath = Join-Path $installDir $checksumName
$archivePath = Join-Path $installDir $archiveName
if (-not (Test-Path -LiteralPath $checksumsPath -PathType Leaf)) {
    Invoke-WebRequest -Uri "$releaseUrl/$checksumName" -OutFile $checksumsPath
}

$checksumLine = Get-Content -LiteralPath $checksumsPath | Where-Object { $_ -match "\s+$([regex]::Escape($archiveName))$" } | Select-Object -First 1
if (-not $checksumLine) { throw "Official checksum manifest has no entry for $archiveName" }
$expectedHash = ($checksumLine -split '\s+')[0].ToLowerInvariant()
if ($expectedHash -ne 'fabe42e0eb04d00e776a66178299ff95a46c623dbc260f997e58fd514853dd40') {
    throw "Pinned checksum for CLI $version changed unexpectedly: $expectedHash"
}

if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
    Invoke-WebRequest -Uri "$releaseUrl/$archiveName" -OutFile $archivePath
}
$actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualHash -ne $expectedHash) { throw "CLI archive SHA-256 mismatch: $actualHash" }

$exePath = Join-Path $installDir 'arduino-cli.exe'
if (-not (Test-Path -LiteralPath $exePath -PathType Leaf)) {
    Expand-Archive -LiteralPath $archivePath -DestinationPath $installDir -Force
}

$slashRoot = $root.Replace('\', '/')
@"
board_manager:
  additional_urls:
    - https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
directories:
  data: "$slashRoot/data"
  downloads: "$slashRoot/downloads"
  user: "$slashRoot/user"
"@ | Set-Content -LiteralPath $configPath -Encoding utf8

& $exePath --config-file $configPath version
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Output "Verified official archive SHA-256: $actualHash"
Write-Output "Workspace-local Arduino CLI: $exePath"
Write-Output "Workspace-local configuration: $configPath"
