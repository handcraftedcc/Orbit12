param(
    [string]$BuildDirectory = "build/native",
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$AsanUbsan
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$sourceDir = Join-Path $repoRoot "tests/native"
$buildDir = Join-Path $repoRoot $BuildDirectory

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if ($null -eq $cmake) {
    $bundledCmake = "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
    if (Test-Path -LiteralPath $bundledCmake) {
        $cmakePath = $bundledCmake
    } else {
        throw "CMake was not found. Install CMake or use the bundled Visual Studio CMake."
    }
} else {
    $cmakePath = $cmake.Source
}

$cmakeArgs = @("-S", $sourceDir, "-B", $buildDir, "-DCMAKE_BUILD_TYPE=$Configuration")
if ($AsanUbsan) {
    if (-not $IsLinux) {
        throw "ASan/UBSan mode is intended for supported Linux host builds."
    }
    $cmakeArgs += "-DORBIT_NATIVE_ASAN_UBSAN=ON"
}
Write-Host "Configure: $cmakePath $($cmakeArgs -join ' ')"
& $cmakePath @cmakeArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Build: $cmakePath --build $buildDir --config $Configuration --parallel"
& $cmakePath --build $buildDir --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$ctestCommand = Get-Command ctest -ErrorAction SilentlyContinue
if ($null -ne $ctestCommand) {
    $ctestPath = $ctestCommand.Source
} else {
    $ctestPath = Join-Path (Split-Path -Parent $cmakePath) "ctest.exe"
    if (-not (Test-Path -LiteralPath $ctestPath)) {
        throw "CTest was not found beside CMake or on PATH."
    }
}
Write-Host "Test: $ctestPath --test-dir $buildDir -C $Configuration --output-on-failure"
& $ctestPath --test-dir $buildDir -C $Configuration --output-on-failure
exit $LASTEXITCODE
