# Désinstallation de Racecab Frontend : retire le programme, les raccourcis et le démarrage automatique.
# Les données (bibliothèque, médias, réglages, sauvegardes) ne sont PAS supprimées.
$ErrorActionPreference = 'Continue'
$installDir = $PSScriptRoot
$dataDir = ''
if (Test-Path (Join-Path $installDir 'datadir.txt')) { $dataDir = (Get-Content (Join-Path $installDir 'datadir.txt') -Raw).Trim() }

Write-Host '=== Désinstallation de Racecab Frontend ===' -ForegroundColor Cyan
if (Get-Process RacecabFrontend -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($installDir, 'OrdinalIgnoreCase') }) {
    Write-Host 'Racecab Frontend est en cours d''exécution : fermez-le puis relancez la désinstallation.' -ForegroundColor Red
    exit 1
}
$r = Read-Host "Supprimer le programme de $installDir ? (les données restent dans $dataDir) [o/N]"
if (-not $r.Trim().ToLower().StartsWith('o')) { exit 0 }

Remove-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name 'RacecabFrontend' -ErrorAction SilentlyContinue
Remove-Item (Join-Path ([Environment]::GetFolderPath('Desktop')) 'Racecab Frontend.lnk') -ErrorAction SilentlyContinue
Remove-Item (Join-Path ([Environment]::GetFolderPath('Programs')) 'Racecab Frontend') -Recurse -ErrorAction SilentlyContinue

Set-Location $env:TEMP
# Seuls les fichiers installés sont supprimés (pas de suppression récursive aveugle du dossier).
$files = @('RacecabFrontend.exe', 'datadir.txt', 'watchdog.log', 'uninstall.ps1', 'Desinstaller.bat', 'Guide-utilisateur.md', 'LICENCES.txt')
Get-ChildItem $installDir -File | Where-Object { $_.Extension -eq '.dll' -or $files -contains $_.Name } | Remove-Item -Force
if (-not (Get-ChildItem $installDir -Force)) { Remove-Item $installDir -Force }
Write-Host "Racecab Frontend est désinstallé. Données conservées : $dataDir" -ForegroundColor Green
