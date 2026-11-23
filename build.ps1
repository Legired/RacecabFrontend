# Compilation de Racecab Frontend (Windows, MSVC + vcpkg)
# Usage :  .\build.ps1            -> Release
#          .\build.ps1 -Config Debug
#          .\build.ps1 -Clean
param(
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')] [string]$Config = 'Release',
    [switch]$Clean,
    [string]$Vcpkg = 'H:\vcpkg'
)
$ErrorActionPreference = 'Continue'  # les avertissements CMake sur stderr ne doivent pas interrompre ; on teste $LASTEXITCODE
Set-Location $PSScriptRoot

# Les outils MSYS2 (cmake, rc...) présents dans le PATH cassent la chaîne MSVC : on les retire.
$env:PATH = (($env:PATH -split ';') | Where-Object { $_ -and ($_ -notmatch 'msys64|mingw|cygwin') }) -join ';'
$env:PATH = "C:\Program Files\CMake\bin;$env:PATH"
$env:VCPKG_ROOT = $Vcpkg

if ($Clean -and (Test-Path build)) { Remove-Item -Recurse -Force build }

cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
    "-DCMAKE_TOOLCHAIN_FILE=$Vcpkg\scripts\buildsystems\vcpkg.cmake" `
    -DVCPKG_TARGET_TRIPLET=x64-windows
if ($LASTEXITCODE -ne 0) { throw "Échec de la configuration CMake" }

cmake --build build --config $Config --parallel
if ($LASTEXITCODE -ne 0) { throw "Échec de la compilation" }

Write-Host "`nExécutable : $PSScriptRoot\build\$Config\RacecabFrontend.exe" -ForegroundColor Green
