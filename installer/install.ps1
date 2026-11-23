# Installation de Racecab Frontend sur une borne.
#
#   Double-cliquer sur Installer.bat (questions posées à l'écran), ou :
#   powershell -ExecutionPolicy Bypass -File install.ps1 -Mode guncab -AutoStart -Kiosk -Quiet
#
# Le programme va dans -InstallDir, les données (bibliothèque, médias, réglages) dans -DataDir.
# Une mise à jour (même dossier) conserve les données et les réglages.
param(
    [ValidateSet('', 'racecab', 'guncab')] [string]$Mode = '',
    [string]$InstallDir = 'C:\RacecabFrontend',
    [string]$DataDir = 'C:\RacecabData',
    [switch]$AutoStart,
    [switch]$Kiosk,
    [switch]$NoShortcut,
    [switch]$Quiet
)
$ErrorActionPreference = 'Stop'
$src = $PSScriptRoot

function Ask([string]$question, [bool]$default) {
    $d = if ($default) { 'O/n' } else { 'o/N' }
    $r = Read-Host "$question [$d]"
    if ([string]::IsNullOrWhiteSpace($r)) { return $default }
    return $r.Trim().ToLower().StartsWith('o')
}

Write-Host ''
Write-Host '=== Installation de Racecab Frontend ===' -ForegroundColor Cyan
if (-not (Test-Path (Join-Path $src 'RacecabFrontend.exe'))) { throw "RacecabFrontend.exe introuvable à côté de ce script." }

# Mise à jour : dossier de données et type de borne déjà configurés
$existingData = Join-Path $InstallDir 'datadir.txt'
if ((Test-Path $existingData) -and -not $PSBoundParameters.ContainsKey('DataDir')) {
    $DataDir = (Get-Content $existingData -Raw).Trim()
}
$currentMode = ''
$settingsFile = Join-Path $DataDir 'config\settings.json'
if (Test-Path $settingsFile) {
    try { $currentMode = (Get-Content $settingsFile -Raw -Encoding UTF8 | ConvertFrom-Json).cabMode } catch { }
}

if (-not $Quiet) {
    if (-not $Mode) {
        Write-Host ''
        Write-Host 'Type de borne :'
        Write-Host '  1. Racecab  (volant, pédales, boutons)'
        Write-Host '  2. GunCab   (pistolets : Retro Shooter RS3, Sinden, Gun4IR...)'
        $def = if ($currentMode -eq 'guncab') { '2' } else { '1' }
        $r = Read-Host "Votre choix [$def]"
        if ([string]::IsNullOrWhiteSpace($r)) { $r = $def }
        $Mode = if ($r.Trim() -eq '2') { 'guncab' } else { 'racecab' }
    }
    $r = Read-Host "Dossier du programme [$InstallDir]"
    if ($r) { $InstallDir = $r.Trim() }
    $r = Read-Host "Dossier des données (bibliothèque, médias) [$DataDir]"
    if ($r) { $DataDir = $r.Trim() }
    if (-not $PSBoundParameters.ContainsKey('AutoStart')) { $AutoStart = Ask 'Lancer le frontend au démarrage de Windows ?' $true }
    if (-not $PSBoundParameters.ContainsKey('Kiosk')) { $Kiosk = Ask 'Mode kiosque (borne publique : pas de « Quitter » dans le menu) ?' $false }
}
if (-not $Mode) { $Mode = if ($currentMode) { $currentMode } else { 'racecab' } }

# Le programme ne doit pas tourner pendant la copie
$running = Get-Process RacecabFrontend -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($InstallDir, 'OrdinalIgnoreCase') }
if ($running) { throw "Racecab Frontend est en cours d'exécution depuis $InstallDir : fermez-le puis relancez l'installation." }

Write-Host ''
Write-Host "Copie du programme dans $InstallDir ..."
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
Get-ChildItem $src -File | Where-Object { $_.Name -notin @('install.ps1', 'Installer.bat') } |
    Copy-Item -Destination $InstallDir -Force
Set-Content -Path (Join-Path $InstallDir 'datadir.txt') -Value $DataDir -Encoding UTF8
New-Item -ItemType Directory -Force -Path $DataDir | Out-Null

# Thèmes supplémentaires fournis avec le paquet (neon80...) : copiés s'ils n'existent pas encore,
# pour ne jamais écraser un thème modifié par l'utilisateur.
$themesSrc = Join-Path $src 'themes'
if (Test-Path $themesSrc) {
    New-Item -ItemType Directory -Force -Path (Join-Path $DataDir 'themes') | Out-Null
    foreach ($t in Get-ChildItem $themesSrc -Directory) {
        $dst = Join-Path $DataDir "themes\$($t.Name)"
        if (-not (Test-Path $dst)) { Copy-Item $t.FullName $dst -Recurse }
    }
}

# Réglages initiaux écrits par le programme lui-même (profils, thèmes, type de borne, démarrage)
$exe = Join-Path $InstallDir 'RacecabFrontend.exe'
$kioskArg = if ($Kiosk) { 'on' } else { 'off' }
$autoArg = if ($AutoStart) { 'on' } else { 'off' }
$p = Start-Process -FilePath $exe -ArgumentList '--data', "`"$DataDir`"", '--mode', $Mode, '--kiosk', $kioskArg, '--autostart', $autoArg, '--setup-only' -Wait -PassThru
if ($p.ExitCode -ne 0) { throw "Le programme n'a pas pu écrire ses réglages (code $($p.ExitCode)). Voir $DataDir\logs\frontend.log" }

if (-not $NoShortcut) {
    $shell = New-Object -ComObject WScript.Shell
    foreach ($dir in @([Environment]::GetFolderPath('Desktop'), (Join-Path ([Environment]::GetFolderPath('Programs')) 'Racecab Frontend'))) {
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
        $lnk = $shell.CreateShortcut((Join-Path $dir 'Racecab Frontend.lnk'))
        $lnk.TargetPath = $exe
        $lnk.WorkingDirectory = $InstallDir
        $lnk.Save()
    }
}

Write-Host ''
Write-Host 'Installation terminée.' -ForegroundColor Green
Write-Host "  Type de borne : $(if ($Mode -eq 'guncab') { 'GunCab (pistolets)' } else { 'Racecab (volant)' })"
Write-Host "  Programme     : $InstallDir"
Write-Host "  Données       : $DataDir"
Write-Host "  Démarrage avec Windows : $(if ($AutoStart) { 'oui (relance automatique en cas de plantage)' } else { 'non' })"
Write-Host "  Mode kiosque  : $(if ($Kiosk) { 'oui' } else { 'non' })"
Write-Host ''
Write-Host 'Premier lancement : F1 pour l''administration (ajouter les jeux, récupérer les médias),'
Write-Host 'puis menu de la borne > Configurer les commandes pour le volant ou les pistolets.'
