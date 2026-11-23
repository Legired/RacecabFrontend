# Publication d'une nouvelle version sur GitHub (les frontends installés la proposeront ensuite eux-mêmes).
#
#   powershell -ExecutionPolicy Bypass -File tools\release.ps1 -Version 0.6.1
#
# Étapes :
#   1. vérifie que la version du programme (kVersion dans src/app/App.cpp) et celle de package.ps1 sont identiques ;
#   2. compile et fabrique dist\RacecabFrontend-<version>.zip (package.ps1) ;
#   3. calcule l'empreinte SHA-256 (fichier .sha256 publié avec le paquet : contrôlée avant toute installation) ;
#   4. reprend la section « ## <version> » de docs\Nouveautes.md comme notes de version ;
#   5. crée la release v<version> sur le dépôt GitHub (gh release create) avec le paquet et son empreinte.
# Il faut GitHub CLI (gh) connecté au compte propriétaire du dépôt. -DryRun : tout sauf la publication.
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$Repo = 'Legired/RacecabFrontend',
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

$app = Get-Content src\app\App.cpp -Raw -Encoding UTF8
if ($app -notmatch 'kVersion = "([^"]+)"') { throw 'kVersion introuvable dans src/app/App.cpp' }
if ($Matches[1] -ne $Version) { throw "kVersion vaut $($Matches[1]) : mettre `"$Version`" dans src/app/App.cpp avant de publier" }
$pkg = Get-Content package.ps1 -Raw -Encoding UTF8
if ($pkg -notmatch "\`$Version = '$([regex]::Escape($Version))'") { throw "package.ps1 : mettre la version par défaut à '$Version'" }

& .\package.ps1 -Version $Version
$zip = Join-Path $root "dist\RacecabFrontend-$Version.zip"
if (-not (Test-Path $zip)) { throw "Paquet absent : $zip" }
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
$sha = "$zip.sha256"
[IO.File]::WriteAllText($sha, "$hash  RacecabFrontend-$Version.zip`n")

# Notes : section « ## <version> » de Nouveautes.md (jusqu'à la section suivante)
$notesAll = Get-Content docs\Nouveautes.md -Raw -Encoding UTF8
$m = [regex]::Match($notesAll, "(?ms)^## $([regex]::Escape($Version))\b.*?\r?\n(.*?)(?=^## |\z)")
$notes = if ($m.Success) { $m.Groups[1].Value.Trim() } else { "Racecab Frontend $Version" }
$notesFile = Join-Path $env:TEMP "racecab-notes-$Version.md"
[IO.File]::WriteAllText($notesFile, $notes, (New-Object Text.UTF8Encoding $false))

Write-Host "`nVersion   : $Version"
Write-Host "Paquet    : $zip ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) Mo)"
Write-Host "SHA-256   : $hash"
Write-Host "Dépôt     : $Repo"
if ($DryRun) { Write-Host "`n-DryRun : rien n'a été publié." -ForegroundColor Yellow; return }

gh release create "v$Version" $zip $sha --repo $Repo --title "Racecab Frontend $Version" --notes-file $notesFile
if ($LASTEXITCODE -ne 0) { throw 'Publication refusée par GitHub (gh release create)' }
Write-Host "`nPubliée : https://github.com/$Repo/releases/tag/v$Version" -ForegroundColor Green
