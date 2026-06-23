# setup_toolchain.ps1
# Downloads portable CMake + MinGW into lsm-engine\tools\ if not already installed.
# Run this script from the lsm-engine directory.
$ErrorActionPreference = "Stop"

$toolsDir = "$PSScriptRoot\tools"
New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null

# ---- CMake ----
$cmakeVersion = "3.29.6"
$cmakeZip     = "$toolsDir\cmake.zip"
$cmakeDir     = "$toolsDir\cmake-$cmakeVersion-windows-x86_64"
$cmakeBin     = "$cmakeDir\bin\cmake.exe"

if (-not (Test-Path $cmakeBin)) {
    Write-Host "Downloading portable CMake $cmakeVersion..."
    $cmakeUrl = "https://github.com/Kitware/CMake/releases/download/v$cmakeVersion/cmake-$cmakeVersion-windows-x86_64.zip"
    Invoke-WebRequest -Uri $cmakeUrl -OutFile $cmakeZip -UseBasicParsing
    Write-Host "Extracting CMake..."
    Expand-Archive -Path $cmakeZip -DestinationPath $toolsDir -Force
    Remove-Item $cmakeZip
} else {
    Write-Host "CMake already present at $cmakeBin"
}

# ---- MinGW (winlibs) ----
$mingwVersion = "13.2.0"
$mingwZip     = "$toolsDir\mingw.zip"
$mingwDir     = "$toolsDir\mingw64"
$gppBin       = "$mingwDir\bin\g++.exe"

if (-not (Test-Path $gppBin)) {
    Write-Host "Downloading portable MinGW $mingwVersion..."
    # winlibs UCRT64 distribution — no installation needed, just unzip
    $mingwUrl = "https://github.com/brechtsanders/winlibs_mingw/releases/download/13.2.0posix-17.0.6-11.0.1-ucrt-r5/winlibs-x86_64-posix-seh-gcc-13.2.0-llvm-17.0.6-mingw-w64ucrt-11.0.1-r5.zip"
    Invoke-WebRequest -Uri $mingwUrl -OutFile $mingwZip -UseBasicParsing
    Write-Host "Extracting MinGW (this may take a minute)..."
    Expand-Archive -Path $mingwZip -DestinationPath $toolsDir -Force
    Remove-Item $mingwZip
} else {
    Write-Host "MinGW already present at $gppBin"
}

# ---- Configure & Build ----
Write-Host ""
Write-Host "=== Configuring LSM-Tree build with portable toolchain ==="
$env:PATH = "$cmakeDir\bin;$mingwDir\bin;$env:PATH"

& $cmakeBin -B build `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_C_COMPILER="$mingwDir\bin\gcc.exe" `
    -DCMAKE_CXX_COMPILER="$mingwDir\bin\g++.exe" `
    -G "MinGW Makefiles"

if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

Write-Host ""
Write-Host "=== Building ==="
& $cmakeBin --build build --parallel

if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Write-Host ""
Write-Host "=== Running Tests ==="
Set-Location build
& $cmakeBin --build . --target test -- ARGS="--output-on-failure"
Set-Location ..

Write-Host ""
Write-Host "Done! Run .\build\lsm_benchmark.exe --small for a quick benchmark."
