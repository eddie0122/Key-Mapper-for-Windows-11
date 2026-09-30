<#
.SYNOPSIS
  Builds the Keymapper release and stages it in dist\.

.DESCRIPTION
  Uses Visual Studio 2022 (MSVC, static runtime) when it is installed, and
  otherwise a Clang/MinGW toolchain (llvm-mingw) found on PATH.

  dist\Keymapper.exe          the single file users download
  dist\symbols\Keymapper.pdb  developer symbols, kept out of the download

.PARAMETER Toolchain
  auto (default), msvc, or mingw.

.PARAMETER SkipTests
  Skip running the unit tests after building.
#>
param(
    [ValidateSet('auto', 'msvc', 'mingw')] [string] $Toolchain = 'auto',
    [switch] $SkipTests
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

function Find-VisualStudio {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}

if ($Toolchain -eq 'auto') {
    if (Find-VisualStudio) { $Toolchain = 'msvc' }
    elseif (Get-Command clang++ -ErrorAction SilentlyContinue) { $Toolchain = 'mingw' }
    else { throw 'No compiler found. Install Visual Studio 2022 with "Desktop development with C++", or put llvm-mingw on PATH.' }
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { throw 'CMake 3.21 or newer must be on PATH.' }

if ($Toolchain -eq 'msvc') {
    $build = Join-Path $root 'build-msvc'
    cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64
    if ($LASTEXITCODE) { throw 'CMake configure failed.' }
    cmake --build $build --config Release --parallel
    if ($LASTEXITCODE) { throw 'Build failed.' }
    $out = Join-Path $build 'Release'
} else {
    $build = Join-Path $root 'build'
    $generator = if (Get-Command ninja -ErrorAction SilentlyContinue) { 'Ninja' } else { 'MinGW Makefiles' }
    cmake -S $root -B $build -G $generator -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_RC_COMPILER=llvm-windres
    if ($LASTEXITCODE) { throw 'CMake configure failed.' }
    cmake --build $build --parallel
    if ($LASTEXITCODE) { throw 'Build failed.' }
    $out = $build
}

if (-not $SkipTests) {
    & (Join-Path $out 'keymapper_tests.exe')
    if ($LASTEXITCODE) { throw 'Unit tests failed.' }
}

$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Force (Join-Path $dist 'symbols') | Out-Null
Copy-Item (Join-Path $out 'Keymapper.exe') $dist -Force
Copy-Item (Join-Path $out 'Keymapper.pdb') (Join-Path $dist 'symbols') -Force
Write-Host "Release staged in $dist"
