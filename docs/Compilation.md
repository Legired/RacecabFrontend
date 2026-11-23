# Compilation et options

## Prérequis (déjà présents sur la machine de développement)
- Visual Studio 2022 (charge de travail « Développement Desktop en C++ »)
- CMake ≥ 3.25 (`C:\Program Files\CMake`)
- vcpkg cloné dans `H:\vcpkg` (`git clone https://github.com/microsoft/vcpkg H:\vcpkg` puis `H:\vcpkg\bootstrap-vcpkg.bat`)

Les dépendances (SDL3, SDL3_image, SDL3_ttf, SQLite, nlohmann-json, Dear ImGui, FFmpeg : avcodec, avformat, swresample et swscale, LGPL) sont déclarées dans `vcpkg.json`. Elles sont installées automatiquement à la première compilation : environ 5 minutes, plus environ 30 minutes pour FFmpeg la première fois. Le réseau (WinHTTP) et l'encodage de la vidéo de test (Media Foundation) viennent de Windows.

## Compiler
```powershell
.\build.ps1                 # Release
.\build.ps1 -Config Debug
.\build.ps1 -Clean          # repart de zéro
```
Résultat : `build\Release\RacecabFrontend.exe` + DLL. Le dossier `build\Release` est autonome : on peut le copier tel quel sur la borne.

> Le script retire MSYS2/MinGW du PATH. Le `cmake.exe` de `C:\msys64\mingw32` casse la compilation MSVC (erreur RC1107).

## Dossier de données
Par ordre de priorité :
1. option `--data <chemin>` ;
2. fichier `datadir.txt` à côté de l'exécutable (une ligne : le chemin) ;
3. `H:\RacecabData`.

Contenu : `config\settings.json`, `config\input.json`, `config\state.json`, `themes\<nom>\theme.json`,
`db\library.sqlite`, `media\<id du jeu>\logo.png|fanart.jpg`, `logs\frontend.log`.

## Options de ligne de commande
| Option | Effet |
|---|---|
| `--data <chemin>` | dossier de données |
| `--import <fichier.json>` | importe une liste de jeux au démarrage (doublons ignorés) |
| `--import-only <fichier.json>` | importe puis quitte, sans ouvrir de fenêtre |
| `--actions "Next,Next,wait:500,Select,F1"` | test : injecte des actions (200 ms d'écart). Aussi : `F1` (administration), `HoldBack` (appui long sur Retour), `key:Return` et `joybutton:5` (événements bruts injectés dans SDL, qui passent par le même chemin qu'une vraie entrée) |
| `--screenshot <fichier.png>` | test : capture l'écran 1,2 s après la dernière action, puis quitte |
| `--mode racecab\|guncab` | type de borne (enregistré) |
| `--kiosk on\|off`, `--autostart on\|off` | mode kiosque, démarrage avec Windows (enregistrés) |
| `--setup-only` | écrit les réglages (profils, thèmes, options ci-dessus) puis quitte, sans fenêtre : utilisé par l'installation |
| `--watchdog` | surveillance : lance le frontend et le relance après un plantage (utilisé par le démarrage avec Windows) |
| `--record <fichier.mp4>` | démonstration : filme l'écran du frontend (H.264, 30 images/s, encodeur de Windows) jusqu'à la fin des `--actions`, puis quitte |
| `--no-single-instance` | test : autorise une deuxième instance (test d'endurance pendant une utilisation normale) |

Actions de test supplémentaires (`--actions`) : `shoot:0.5/0.85` (tir du pistolet à 50 % de la largeur, 85 % de la hauteur), `aim:x/y`, `rightclick`, `adminTab:Système`, `launch:<id>`, `cabmode:guncab`, `backupNow`, `restoreOldest`, `lbUpdate`, `scrapeMissing`, `attract`, `profile:<nom>`.

## Tests automatisés des entrées
```powershell
build\Release\input_tests.exe
```
Ces tests simulent des événements SDL (clavier, joystick vJoy, manette Xbox, pédale), sans matériel. Ils vérifient la répétition et l'accélération, l'appui long, l'anti-rebond, l'anti double validation, le filtrage par périphérique, l'hystérésis des axes, la capture, le remplacement des conflits, la garde après un jeu et l'enregistrement du mapping.

## Autres tests automatisés
```powershell
build\Release\system_tests.exe          # sauvegarde / restauration, archives ZIP, archive piégée
build\Release\media_tests.exe 8765      # avec python tools\mock_media_server.py 8765 testdata	est_video.mp4
python -I tools\make_endurance_data.py testdata\data testdata\endurance testdata	est_video.mp4
powershell -File tools\endurance.ps1 -DataDir testdata\endurance -Minutes 60
```

## Paquet d'installation
```powershell
.\package.ps1            # compile puis crée dist\RacecabFrontend-<version>.zip
```
Le paquet contient le programme, les DLL, le runtime Visual C++ (déploiement local), `Installer.bat` / `install.ps1`, `Desinstaller.bat` / `uninstall.ps1`, le guide utilisateur et les licences. Voir [Systeme-Bornes.md](Systeme-Bornes.md).

## Bibliothèque de test
```powershell
python tools\make_test_library.py testdata 500
build\Release\RacecabFrontend.exe --data H:\frontend\testdata\data --import H:\frontend\testdata\games.json
```
Le script crée de vrais `.bat` de test (fenêtre enfant, console de 5 s), des logos et des fonds, ainsi qu'un `settings.json` en mode fenêtré.
