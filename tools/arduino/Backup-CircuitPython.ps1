[CmdletBinding()]
param(
    [string] $BackupRoot = (Join-Path $PSScriptRoot '.tool-cache/backups')
)

$ErrorActionPreference = 'Stop'

$volumes = @(Get-Volume | Where-Object { $_.FileSystemLabel -ceq 'CIRCUITPY' })
if ($volumes.Count -ne 1) {
    throw "Expected exactly one mounted volume labeled CIRCUITPY; found $($volumes.Count). No files were copied."
}

$volume = $volumes[0]
if (-not $volume.DriveLetter) {
    throw 'The CIRCUITPY volume has no drive letter. No files were copied.'
}

$sourceRoot = "{0}:\" -f $volume.DriveLetter
if (-not (Test-Path -LiteralPath $sourceRoot -PathType Container)) {
    throw "CIRCUITPY root '$sourceRoot' is unavailable. No files were copied."
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$destination = Join-Path $BackupRoot "CIRCUITPY-$stamp"
if (Test-Path -LiteralPath $destination) {
    throw "Backup destination already exists: $destination"
}
New-Item -ItemType Directory -Path $destination -Force | Out-Null

$manifest = [System.Collections.Generic.List[object]]::new()
$files = @(Get-ChildItem -LiteralPath $sourceRoot -Force -File -Recurse)
foreach ($file in $files) {
    if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Refusing to follow reparse point '$($file.FullName)'; source remains untouched. Partial backup: $destination"
    }

    $relative = [IO.Path]::GetRelativePath($sourceRoot, $file.FullName)
    if ([IO.Path]::IsPathRooted($relative) -or $relative.StartsWith('..')) {
        throw "Resolved source path escaped CIRCUITPY: '$($file.FullName)'"
    }

    $target = Join-Path $destination $relative
    $targetDir = Split-Path -Parent $target
    New-Item -ItemType Directory -Path $targetDir -Force | Out-Null

    $before = Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256
    Copy-Item -LiteralPath $file.FullName -Destination $target
    $sourceAfter = Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256
    $copyHash = Get-FileHash -LiteralPath $target -Algorithm SHA256
    if ($before.Hash -ne $sourceAfter.Hash -or $before.Hash -ne $copyHash.Hash) {
        throw "Hash verification failed for '$relative'. Partial backup retained at $destination"
    }

    $manifest.Add([pscustomobject]@{
        Path = $relative.Replace('\', '/')
        Length = [int64]$file.Length
        SHA256 = $before.Hash.ToLowerInvariant()
    })
}

$manifestPath = Join-Path $destination 'backup-manifest.json'
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestPath -Encoding utf8

# Read the manifest back and independently recheck every copied file.
$verifiedManifest = @(Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json)
foreach ($entry in $verifiedManifest) {
    $target = Join-Path $destination ($entry.Path.Replace('/', [IO.Path]::DirectorySeparatorChar))
    if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
        throw "Manifest target is missing: '$($entry.Path)'"
    }
    $info = Get-Item -LiteralPath $target
    $hash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
    if ([int64]$info.Length -ne [int64]$entry.Length -or $hash -ne $entry.SHA256) {
        throw "Post-copy manifest verification failed for '$($entry.Path)'"
    }
}

[pscustomobject]@{
    Source = $sourceRoot
    Destination = $destination
    FilesCopied = $verifiedManifest.Count
    Manifest = $manifestPath
    Verified = $true
}
