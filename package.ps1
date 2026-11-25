# Fabrication du paquet d'installation : dist\RacecabFrontend-<version>.zip
# Contenu : programme, bibliothèques (FFmpeg, SDL...), runtime Visual C++ (déploiement local autorisé par Microsoft),
# scripts d'installation / désinstallation, guide utilisateur, licences.
param([string]$Version = '0.7.1', [switch]$NoBuild)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

if (-not $NoBuild) { & .\build.ps1 -Config Release }
$rel = Join-Path $PSScriptRoot 'build\Release'
$name = "RacecabFrontend-$Version"
$dist = Join-Path $PSScriptRoot "dist\$name"
if (Test-Path $dist) { Remove-Item -LiteralPath $dist -Recurse -Force }
New-Item -ItemType Directory -Force -Path $dist | Out-Null

Copy-Item (Join-Path $rel 'RacecabFrontend.exe') $dist
Copy-Item (Join-Path $rel '*.dll') $dist

# Runtime Visual C++ x64 (le plus récent installé avec Visual Studio)
$crt = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\2022\*\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT' -ErrorAction SilentlyContinue |
    Sort-Object { [version]($_.Parent.Parent.Name) } -Descending | Select-Object -First 1
if (-not $crt) { throw 'Runtime Visual C++ (Microsoft.VC143.CRT) introuvable' }
Copy-Item (Join-Path $crt.FullName 'msvcp140*.dll'), (Join-Path $crt.FullName 'vcruntime140*.dll') $dist

Copy-Item installer\install.ps1, installer\uninstall.ps1, installer\Installer.bat, installer\Desinstaller.bat, installer\LICENCES.txt $dist
Copy-Item docs\Guide-utilisateur.md, docs\Nouveautes.md $dist
Copy-Item themes (Join-Path $dist 'themes') -Recurse   # thèmes supplémentaires (neon80)
# Outils fournis : yt-dlp (vidéos YouTube), installé à côté du programme
$ytdlp = Join-Path $PSScriptRoot 'third_party\yt-dlp\yt-dlp.exe'
if (Test-Path $ytdlp) { Copy-Item $ytdlp $dist } else { Write-Warning "yt-dlp.exe absent de third_party\yt-dlp : paquet sans yt-dlp" }

$zip = Join-Path $PSScriptRoot "dist\$name.zip"
if (Test-Path $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -Path "$dist\*" -DestinationPath $zip
$size = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host "`nPaquet : $zip ($size Mo)" -ForegroundColor Green
