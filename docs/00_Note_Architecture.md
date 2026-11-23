# Racecab Frontend — Note d'architecture (Phase 0)

> Réponse à la section 19 du cahier des charges. Aucun code n'est écrit avant validation de cette note.
> Légende : ✅ vérifié sur la machine ou dans une doc lue · ⚠️ à confirmer (non vérifié ou source inaccessible)

---

## 0. Environnement constaté sur la machine cible

| Élément | État |
|---|---|
| Windows 11 Pro (build 26200) | ✅ |
| Visual Studio 2022 Community (MSVC) | ✅ installé |
| CMake 4.2 + Ninja 1.11 | ✅ |
| .NET SDK 9.0 | ✅ (non retenu, voir §2) |
| MSYS2 g++ 16 | ✅ (non retenu : on compile avec MSVC) |
| vcpkg | ❌ absent → à installer (gestion des dépendances C++) |
| BackForceFeeder **v0.8.0.76** | ✅ `C:\Program Files\BackForceFeeder\BackForceFeeder-v0.8.0.76` |
| ViGEm Bus Driver | ✅ installé |
| Sketch Arduino `H:\FeederIOBoard\FeederIOBoard.ino` | ✅ présent |

---

## 1. Technologie recommandée : **C++20 + SDL3**

| Brique | Choix | Raison |
|---|---|---|
| Fenêtre, rendu 2D, entrées | **SDL3** (+ SDL3_image, SDL3_ttf) | Rendu GPU (D3D11) avec boucle maîtrisée, empreinte de quelques Mo, API manettes unifiée (DirectInput, XInput, RawInput, HIDAPI), licence zlib |
| Vidéo | **libmpv** (rendu dans une texture SDL) | Décodage matériel D3D11VA/DXVA2 intégré, gère MP4/H.264 et le reste « gratuitement », une seule vidéo à la fois, thread séparé. Alternative : FFmpeg direct (plus de code, moins robuste) |
| Base de données | **SQLite** (amalgamation) | Jeux, catégories (arbre), stats, favoris, historique |
| Paramètres et thèmes | **JSON** (nlohmann/json) | Lisible et éditable, sans recompiler |
| Menu d'administration | **Dear ImGui** (backend SDL3) | Natif, léger, pratique à la souris et au clavier, et navigable à la manette (`ImGuiConfigFlags_NavEnableGamepad`) |
| HTTP (assets) | **libcurl** (WinHTTP en option) | Téléchargements asynchrones dans un thread de travail |
| Logs | **spdlog** | Journaux tournants dans `data/logs` |

**Pourquoi ce choix :** une borne affiche une scène 2D animée à 60 fps et doit répondre instantanément. Une boucle de jeu SDL donne une maîtrise totale de la latence : on lit les entrées en début de frame, et le rendu ne bloque jamais. La RAM au repos dépend surtout du cache de textures, que l'on borne nous-mêmes. Le binaire se distribue sous forme d'un dossier avec quelques DLL, sans runtime à installer.

## 2. Alternatives étudiées

| Option | Inconvénients pour ce projet |
|---|---|
| **C++ / Qt (QML)** | Runtime lourd (plusieurs dizaines de Mo de DLL), licence LGPL/commerciale à gérer, QtMultimedia inégal selon les backends. Puissant, mais surdimensionné pour une roue et quelques panneaux |
| **C# / WPF** | Rendu retenu (retained) avec composition parfois saccadée lors d'animations continues et de vidéos, ramasse-miettes (pauses), pas de lecteur vidéo matériel fiable intégré (MediaElement), entrées manette à ajouter (SharpDX n'est plus maintenu). Plus simple à maintenir, mais moins adapté aux priorités « fluidité et légèreté » |
| **C# / MonoGame ou Raylib** | Viables, mais pas d'avantage sur SDL3. Raylib n'a pas de vidéo et son support manettes est plus limité |
| **Electron / web** | Exclu par le cahier des charges |

> Les chiffres de mémoire et de CPU seront **mesurés** dès le prototype de la phase 1 (Gestionnaire des tâches + compteurs internes : ms/frame, textures en cache), pas annoncés à l'avance.

## 3. Architecture logicielle

```
src/
  app/          boucle principale, machine à états (Navigation, Menu, Lancement, EnJeu, Attract)
  render/       renderer SDL, gestion des textures, polices, animations (tweens)
  ui/           Wheel (roue / bande), InfoPanel, VideoPanel, CategoryBar, Toasts
  theme/        chargement des thèmes JSON (layout, couleurs, polices, paramètres de roue)
  library/      modèle Jeu/Catégorie, accès SQLite, scan de dossiers, import, doublons
  profiles/     profils de borne (Racecab, GunCab, Flipper...)
  media/        cache média (LRU borné), préchargement des voisins, chargement asynchrone, visuels de repli
  video/        wrapper libmpv → texture, une seule instance active
  input/        périphériques SDL3, mapping d'actions, répétition, anti-rebond, mode « capture »
  launcher/     lancement BAT/EXE, Job Object, supervision, scripts avant/après
  scraper/      fournisseurs SteamGridDB / ScreenScraper, file de jobs, choix des résultats
  admin/        menus ImGui (11 sections du §13)
  attract/      détection d'inactivité, défilement automatique
  core/         config, logs, chemins, pool de threads, utilitaires
```

Les règles clés :
- **Le thread principal ne fait jamais d'I/O.** Le décodage d'images, la lecture disque, le réseau et SQLite (hors petites lectures) passent par un pool de travail. Le thread principal ne fait qu'envoyer les textures au GPU.
- **Roue virtualisée** : seuls les N logos visibles plus une marge sont chargés. Le cache LRU est borné en Mo. Avec 5 000 jeux, le coût d'un déplacement reste le même.
- **Actions abstraites** : l'UI ne connaît que `Next/Prev/CatNext/CatPrev/Select/Back/Favorite/Menu/Quit`, jamais des boutons physiques.

## 4. Lecture des commandes BackForceFeeder sous Windows

**Ce qui est établi sur votre installation (v0.8.0.76) ✅ :**
- BackForceFeeder est une application .NET. Ses DLL montrent qu'il **lit** les entrées via **DirectInput et XInput** (`SharpDX.DirectInput`, `SharpDX.XInput`), le **port série** (`System.IO.Ports`, carte Arduino `FeederIOBoard`) et le **HID USB** (`UsbHid.dll`).
- Il **réexpose** les commandes à Windows via un **joystick virtuel vJoy** (`vJoyInterface.dll`). Il peut aussi émettre des **touches clavier** (`WindowsInput.dll`). ViGEm (manette XInput virtuelle) est installé sur la machine.
- **Correction du cahier des charges :** le lien fourni (Ultrawipf/OpenFFBoard) correspond à **un autre projet**, un firmware de volant direct drive. BackForceFeeder est un projet distinct (auteur njz3). Son dépôt GitHub `njz3/BackForceFeeder` renvoie aujourd'hui une erreur 404 ⚠️. L'analyse ci-dessus repose donc sur les binaires installés.

**Méthode retenue :** **aucune communication avec le firmware.** Le frontend lit le **périphérique vJoy** (vu par Windows comme une manette HID/DirectInput standard) avec l'API joystick de SDL3. Il lit aussi le clavier et les manettes XInput, ce qui couvre tous les modes de sortie de BackForceFeeder. Dans l'écran de mapping, on choisit le périphérique (vJoy, volant physique, panneau de boutons...) et on appuie sur l'entrée voulue (bouton, axe au-delà d'un seuil, chapeau).

**Pendant un jeu :** quand la fenêtre perd le focus, la lecture des actions de navigation est suspendue (`SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS = 0`). Seule une combinaison « quitter le jeu » reste surveillée, si vous l'activez. Le frontend n'ouvre jamais les périphériques en mode exclusif, et le jeu reçoit donc toutes ses commandes.

⚠️ **À tester sur la borne** (phase 2) : quels numéros de boutons vJoy correspondent aux boutons physiques, et ce qui se passe si BackForceFeeder démarre après le frontend (SDL gère le branchement à chaud).

## 5. Sources d'assets

| Source | Accès | Points forts | Contraintes |
|---|---|---|---|
| **ScreenScraper** ✅ (doc API v2 lue) | `devid` + `devpassword` + `softname` obligatoires, compte utilisateur facultatif | Très bonne couverture de l'**arcade** (MAME, TeknoParrot, Taito...) et du PC. Médias `wheel` / `wheel-hd` (logo PNG transparent), `video` (MP4), `fanart`, `ss`, `marquee`. Recherche par nom via `jeuRecherche.php` (30 résultats classés) | Identifiants développeur à **demander via leur forum** ⚠️. Quotas et threads limités (HTTP 429/430). Données sous licence **CC BY-NC-SA** : usage non commercial avec attribution |
| **SteamGridDB** | Clé API personnelle gratuite (Bearer) ⚠️ (la doc est rendue en JavaScript et n'a pas pu être lue automatiquement) | **Logos transparents** et « heroes » (fonds) de très bonne qualité pour les jeux PC | Pas de métadonnées (année, éditeur) ni de vidéos. Conditions et limites à relire avant intégration |

**Stratégie :** ScreenScraper en source principale (métadonnées, wheel, vidéo, fanart), SteamGridDB en complément pour les logos et fonds manquants. Les médias sont téléchargés uniquement à l'ajout ou à la demande, jamais pendant la navigation. Quand plusieurs résultats sont possibles, un écran de choix s'affiche : aucune association silencieuse. Une saisie manuelle et le remplacement de fichiers sont toujours possibles. Les clés sont saisies par vous dans l'administration, et le frontend fonctionne sans aucune clé.

## 6. Lancement et supervision des BAT / EXE

1. Sauvegarde de l'état (profil, catégorie, index) dans `state.json`.
2. Exécution facultative du **script « avant »** (attente de sa fin, avec délai maximal).
3. Création d'un **Job Object Windows**, puis `CreateProcess` **suspendu** :
   - `.bat` → `cmd.exe /c "chemin.bat" args`, avec le répertoire de travail du jeu (dossier du .bat par défaut) ;
   - `.exe` → lancement direct avec les arguments.
   Le processus est attaché au Job, puis relancé. **Tous les processus enfants** (le .bat qui lance l'exe, qui lance un loader...) héritent automatiquement du Job.
4. Le Job est relié à un **port de complétion**. Le message `JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO` signale la **fin réelle du jeu**, c'est-à-dire la fin du dernier processus de l'arbre, et pas seulement celle du premier.
5. Pendant le jeu, le frontend se minimise, suspend le rendu, la vidéo et l'audio (CPU quasi nul) et ignore la navigation.
6. Au retour : script « après », restauration de la fenêtre au premier plan, de la catégorie et du jeu sélectionné, puis mise à jour des statistiques (lancements, temps de jeu).

**Cas limites gérés :**
- un programme qui sort du Job (lanceur qui délègue à un service, Steam, certains loaders) : option par jeu **« surveiller le processus nommé X.exe »** ;
- échec de lancement (chemin absent, code d'erreur) : message lisible et entrée dans le journal.

**Politique d'arrêt documentée :** le frontend **ne tue jamais rien automatiquement**. Le Job est créé **sans** `KILL_ON_JOB_CLOSE`. La seule exception est un arrêt demandé explicitement (combinaison « quitter le jeu » configurée par jeu, désactivée par défaut). Cet arrêt cible **uniquement les processus du Job de ce jeu** : d'abord une fermeture propre (`WM_CLOSE`), puis `TerminateJobObject` après un délai configurable.

## 7. Organisation des dossiers

```
RacecabFrontend\            (programme : remplaçable lors d'une mise à jour)
  RacecabFrontend.exe, *.dll
  default_themes\
<DossierDonnées>\           (choisi au 1er lancement, ex. H:\RacecabData : indépendant du programme)
  config\settings.json      paramètres globaux
  config\profiles\*.json    Racecab.json, GunCab.json...
  config\input\*.json       mappings par profil
  themes\<nom>\theme.json   + images et polices du thème
  db\library.sqlite         jeux, catégories, liens jeu↔catégorie, stats
  media\<id_jeu>\           logo.png, video.mp4, fanart.jpg, screens\, box.png
  logs\                     journaux tournants
  backups\                  sauvegardes datées (config + base)
```

Dans la base, les chemins des jeux sont stockés en absolu, avec une **vérification au démarrage** (jeu marqué « introuvable » au lieu d'être supprimé). Un outil « remplacer le préfixe de chemin » permet de déplacer une collection d'un disque à l'autre.

## 8. Étapes de développement et tests

| Phase | Contenu | Tests d'acceptation |
|---|---|---|
| **1 — Prototype** | Fenêtre plein écran SDL3, roue et bande, logos de test, clavier et manette, ajout manuel (ImGui), catégories, lancement BAT/EXE via Job, retour automatique | 500 jeux factices à 60 fps stables ; un .bat qui lance un exe enfant → retour seulement après la fermeture de l'exe ; restauration de la sélection |
| **2 — Entrées borne** | Liste des périphériques, écran d'assignation, répétition, anti-rebond, appui long | Navigation complète au volant via vJoy/BackForceFeeder **sur votre borne** ; aucune entrée « fantôme » pendant un jeu |
| **3 — Médias** | Scrapers, file de téléchargement, écran de choix, cache, vidéo libmpv | Interface fluide pendant un scraping de 100 jeux ; coupure réseau gérée ; fichier vidéo corrompu → image de repli |
| **4 — Attract et profils** | Inactivité, défilement automatique, profils, thèmes JSON | Interruption à la première entrée en moins d'une frame ; changement de thème sans recompiler |
| **5 — Finitions** | Favoris, historique, stats, recherche, sauvegarde et restauration, démarrage Windows, mode kiosque, installateur | Test d'endurance de 12 h (RAM stable), mesures CPU/RAM publiées, documentation utilisateur |

Un exécutable testable est livré à chaque phase, accompagné d'une liste « terminé / partiel / non testé ».

## 9. Risques identifiés

1. **Lanceurs qui sortent du Job** (TeknoParrot, Steam, services) : le retour au frontend se ferait trop tôt. Mitigation : surveillance par nom de processus, au cas par cas.
2. **Vol du focus** : certains jeux ou outils (y compris l'interface de BackForceFeeder) peuvent passer devant le frontend au retour. Mitigation : `AllowSetForegroundWindow` et restauration active, à tester sur la borne.
3. **Mapping vJoy** : l'ordre des boutons dépend de la config BackForceFeeder et un changement de config casse le mapping. Mitigation : identification du périphérique par GUID et nom, avec un écran de remappage accessible à la manette.
4. **Accès ScreenScraper** : il faut des identifiants développeur, et la licence NC-SA restreint la redistribution. Mitigation : SteamGridDB et l'ajout manuel restent fonctionnels sans ScreenScraper.
5. **Licence libmpv** (LGPL/GPL selon le build) : utiliser un build LGPL en DLL dynamique.
6. **Correspondance par nom approximatif** : risque de mauvais jeu. Mitigation : écran de confirmation obligatoire sous un seuil de confiance.
7. **Changement de résolution par les jeux** : au retour, le frontend doit recréer sa swapchain proprement. À tester avec vos jeux réels.

---

### Décisions attendues de votre part avant la phase 1
- Valider la pile **C++20 / SDL3 / libmpv / SQLite / ImGui** (ou demander WPF si vous privilégiez la simplicité de maintenance).
- Indiquer le dossier de données souhaité (proposition : `H:\RacecabData`) et le dossier des sources (proposition : `H:\frontend`).
- Autoriser l'installation de **vcpkg** pour récupérer les dépendances.
