# Phase 5 : types de borne, système, sauvegardes, installation

Guide pour l'utilisateur final : [Guide-utilisateur.md](Guide-utilisateur.md). Ce document décrit le fonctionnement et ce qui a été vérifié.

## Types de borne : Racecab et GunCab
Un seul programme sert pour toutes les bornes. `settings.json` > `cabMode` (`racecab` / `guncab`) est choisi à l'installation (`--mode`), ou dans Administration > Système.

| | Racecab | GunCab |
|---|---|---|
| Profil de démarrage | Racecab | GunCab |
| Profils proposés au volant / au pistolet | `mode` = racecab ou vide | `mode` = guncab ou vide |
| Commandes | volant, boutons, clavier (mapping par profil) | pistolet en mode souris, plus les mêmes commandes que Racecab |

Un profil a un champ `mode` : `racecab`, `guncab`, vide (proposé sur les deux) ou `none` (gardé mais proposé sur aucune borne). Les profils d'avant cette version sont classés d'après leur nom : Racecab → racecab, GunCab → guncab, les autres → les deux.

### Disposition pistolet (par défaut en GunCab)
La bande de logos de la Racecab est remplacée par deux vues pensées pour le tir. Elles se basculent avec le bouton Mosaïque / Grand (en haut à droite), et la dernière vue est mémorisée (`gun.view`).

| Vue | Écran | Tirs |
|---|---|---|
| Grande image | média (vidéo, fond ou logo) au centre, logo en surimpression, nom et infos dessous ; bandes flèches de chaque côté, avec l'aperçu du jeu voisin | flèche : jeu précédent / suivant ; image : lancement (délai de garde) |
| Mosaïque | grille de `mosaicCols` × `mosaicRows` cases (4 × 3 par défaut) : fond du jeu assombri + logo | flèche : page précédente / suivante (en boucle) ; case : le jeu passe en vue Grande image |

- En mosaïque, aucune vidéo n'est décodée. Valider (bouton ou touche) affiche le jeu choisi en grand.
- La démonstration utilise toujours la vue Grande image.
- Option `gun.arcadeLayout` (Administration > Système) : décochée, on revient à la roue classique, utilisable au pistolet comme décrit ci-dessous.

### Pistolets (Retro Shooter RS3 et autres pistolets en mode souris)
- La position visée devient un viseur (coordonnées converties en pixels de rendu, haute densité comprise).
- **Gâchette** (clic gauche) :
  - sur un logo de la bande : sélection, en suivant le chemin le plus court ;
  - sur le jeu sélectionné, ou sur sa zone média : lancement, seulement si la sélection date de plus de 700 ms (réglable). Deux tirs rapprochés ne lancent donc rien par erreur ;
  - sur les boutons Retour / Menu affichés en haut à droite : l'action correspondante.
- **Clic droit** (recharge / tir hors écran sur les RS3) : Retour au relâchement. Maintenu 1,2 s : menu. Maintenu 5 s : quitter (avec confirmation).
- **Clic du milieu** : menu.
- Menus, choix du profil, saut à une lettre, confirmation de sortie : tirer sur un choix le valide, tirer à côté ferme.
- La garde de 800 ms au retour d'un jeu s'applique aussi au pistolet. Pendant un jeu, les tirs ne sont jamais lus.
- Bordure blanche facultative (pistolets Sinden), désactivée par défaut : les RS3 n'en ont pas besoin.

### Configuration des bornes (données réelles, 2026-11-23)
- Profil **Racecab** : `mode` racecab, catégorie « Tir - GunCab » masquée. Seuls les jeux de course sont visibles (9 jeux).
- Profil **GunCab** : `mode` guncab, catégorie « Course » masquée, avec ses sous-catégories. Seuls les jeux de tir sont visibles (Rambo).
- Ce filtre s'applique partout : Tous les jeux, Favoris, Récemment joués et démonstration.
- Un nouveau jeu doit recevoir sa catégorie (Course ou Tir - GunCab) pour apparaître sur la bonne borne.

### Thème « neon80 » (synthwave années 80)
- Fond généré par `tools/make_neon80_background.py` : ciel étoilé, soleil rayé, montagnes, grille magenta en perspective.
- Couleurs : accent rose néon, textes secondaires cyan. Titres en Segoe UI Black Italic, textes en Bahnschrift (polices fournies avec Windows).
- Livré dans le paquet (`themes\neon80`) et copié par l'installation s'il n'existe pas encore. Choix du thème : Administration > Profils.

## Navigation ajoutée
- **Tri** par profil (menu de la borne ou Administration > Profils) : nom, année, temps de jeu, nombre de lancements. Le nom départage toujours. *Récemment joués* garde son ordre.
- **Aller à une lettre** : seules les lettres présentes dans la liste sont proposées (`#` pour chiffres et symboles).
- **Quitter** : Retour maintenu 5 secondes, puis confirmation, dans tous les modes (y compris kiosque).
- **Écran de lancement** : fond du jeu assombri, logo, étape (préparation, lancement, chargement du jeu). Il reste au premier plan jusqu'à l'apparition de la vraie fenêtre du jeu, la console des .bat est masquée (voir [Lancement.md](Lancement.md)).

## Historique et statistiques
- Schéma de base v2 : table `plays` (jeu, début, durée, profil), une ligne par partie. La migration est automatique.
- Administration > Statistiques : totaux, 10 jeux les plus joués (temps) et les plus lancés, 100 dernières parties, remise à zéro (avec confirmation).

## Sauvegarde et restauration
- Contenu de la sauvegarde :
  - `db/library.sqlite`, copié avec l'API de sauvegarde de SQLite (copie cohérente, même base ouverte) ;
  - `config/` et `themes/` ;
  - `media/` en option.
- Jamais inclus : cache LaunchBox, journaux, sauvegardes.
- Format : ZIP sans compression, lisible par l'Explorateur Windows. Écriture dans un fichier `.part` puis renommage.
- **Automatique** : au démarrage, si la dernière date de plus de 20 h. Les 10 dernières sont gardées (réglable).
- **Restauration** en deux temps :
  1. la sauvegarde choisie est copiée en `restore-pending.zip`, et le frontend redémarre ;
  2. au démarrage, avant toute ouverture de fichier, l'état actuel est sauvegardé (`avant-restauration-…`), puis les fichiers sont remis en place un par un, de façon atomique.
- Toute entrée d'archive qui sortirait des dossiers `config/`, `db/`, `themes/` ou `media/` (`..`, chemin absolu, autre dossier) est ignorée.
- Limite connue : un fichier créé après la sauvegarde (nouveau profil, par exemple) n'est pas supprimé par la restauration.

## Ajout automatique de jeux (scan d'un dossier)
Code : `src/library/Scanner.cpp` (détection) et `AdminUI::WindowScan` (liste). Le disque est seulement lu.

- **Détection** (profondeur 6) :
  - jeu LinuxLoader :
    - dossier avec un ELF `game` et un `linuxloader.ini` : arguments `-g "<dossier>" -c "linuxloader.ini"` ;
    - ou un fichier `.elf` : l'ELF en argument ;
    - dans les deux cas, `linuxloader.exe` du dossier, sinon d'un parent, sinon le plus récent trouvé sous le dossier scanné ; console masquée ;
  - dossier « de lanceurs », avec au moins deux `.bat` : chaque `.bat` est un jeu ;
  - sinon un `.bat` principal (`run`, `launch`, `start`, `play`…), ou le plus gros `.exe` (au moins 512 Ko).
- **Ignoré** : installateurs, désinstalleurs, redistribuables, gestionnaires de plantage, outils, chargeurs (TeknoParrot, linuxloader seul), `.bat` de compilation ou de débogage, dossiers de compilation, de dépendances, de sauvegarde (`backup*`, `*-bak*`, `*-orig`, `*-old`), de langues et de journaux.
- **Nom** : celui du dossier, nettoyé (sans `(…)`, `[…]` ni numéro de version). Pour un dossier générique (`pm`, `disk0`, `bin`…), c'est le dossier parent qui donne le nom. Les abréviations courantes sont développées (`id4` → Initial D Arcade Stage 4).
- **Reconnaissance** : index LaunchBox local (nom ou nom alternatif identique), qui donne le titre officiel, l'année, l'éditeur, le genre et la description. Genre course → catégorie visible seulement sur la Racecab ; tir → seulement sur la GunCab. La colonne *Borne* se calcule à partir des profils.
- **Doublons** :
  - déjà dans le frontend : même fichier et mêmes arguments, ou un des noms connus (dossier, abréviation, titre LaunchBox) égal au nom ou au nom alternatif d'un jeu présent. La case est grisée ;
  - dans la liste : un seul résultat par jeu, dans l'ordre de préférence LinuxLoader > `.exe` > `.bat` sans pause > `.bat` avec pause ;
  - à l'ajout, une dernière vérification ignore tout jeu déjà présent ou ajouté deux fois.
- Un `.bat` qui contient `pause` (hors commentaires) est lancé avec sa console visible, sinon la borne attendrait une touche invisible.
- Essai réel : `H:\LinuxLoader` donne 7 résultats en moins de 5 s. Initial D 4 et 5 sont signalés « déjà dans le frontend », la copie de Cruis'n Blast (`.bat`) est un doublon. Hummer, Cruis'n Blast et The Walking Dead sont cochés, rangés en Course › Arcade (Racecab) et Tir - GunCab (GunCab). `H:\windowsloader` : 10 jeux, tous déjà présents.
- Test automatique (`system_tests`, 9 vérifications) : arborescence factice avec jeu TeknoParrot, désinstalleur, dossier de lanceurs, `pause` en commentaire et réelle, sauvegarde, deux jeux LinuxLoader.

## Mises à jour (releases GitHub)
Code : `src/core/Updater.cpp`. Réglages : `updateRepo` (`Legired/RacecabFrontend` par défaut, dépôt public, API sans compte) et `updateAutoCheck`.

- **Vérification** au démarrage, en tâche de fond : une requête à `api.github.com/repos/<dépôt>/releases/latest`. La version (tag `v0.6.1`) est comparée à celle du programme, numéro par numéro. Le paquet retenu est l'asset `RacecabFrontend-*.zip` (pas celui des sources). S'il y a une version plus récente, un message s'affiche.
- **Téléchargement**, seulement sur demande : vers `<données>\updates`, avec contrôle de la taille. Si la release contient `<zip>.sha256`, l'empreinte doit correspondre exactement, sinon la mise à jour est refusée. Le paquet est ensuite extrait (entrées `..`, `:` ou absolues ignorées) et doit contenir `install.ps1` et `RacecabFrontend.exe`.
- **Installation** : le frontend écrit `updates\appliquer-mise-a-jour.ps1`, le lance (console masquée, hors du Job parent) et se ferme avec le code 0, ce qui arrête aussi la surveillance. Le script :
  1. attend que plus aucun `RacecabFrontend.exe` du dossier d'installation ne tourne ;
  2. exécute `install.ps1 -Quiet -NoShortcut -InstallDir <dossier>`, avec `-AutoStart` et `-Kiosk` selon l'état actuel. Le type de borne et le dossier de données sont repris de l'installation existante ;
  3. relance le frontend (`--watchdog` si le démarrage avec Windows est actif).
  Journal : `updates\mise-a-jour.log`.
- **Copie de développement** (pas de `datadir.txt` à côté du programme) : la vérification marche, l'installation est désactivée.
- **Publier une version** : mettre la version dans `kVersion` (`src/app/App.cpp`) et dans `package.ps1`, ajouter la section `## <version>` à `docs\Nouveautes.md`, puis lancer `tools\release.ps1 -Version <version>`. Le script compile, crée le paquet et son `.sha256`, et publie la release avec ces notes (`gh release create`). `-DryRun` fait tout sauf la publication.
- **Essai réel** (serveur local qui imite l'API GitHub, release 0.6.1 de test) sur une installation 0.6.0 GunCab : vérification, téléchargement de 34 Mo, contrôle SHA-256, extraction, fermeture (code 0), installation (code 0), relance automatique ; fichier témoin de la 0.6.1 présent, type de borne GunCab conservé.

## Partage de la configuration (export / import)
Code : `src/core/ConfigShare.cpp`. Administration > Système > *Partager la configuration*.

- **Export** : un ZIP sans compression qui contient :
  - `config.json` (format 1) : jeux avec chemins, options de lancement, métadonnées, catégories (chemins `Course/Drift`), lien YouTube, début, durée ; liste des catégories ;
  - `profiles/*.json`, où les catégories masquées sont aussi écrites en chemins (`hiddenCategoryPaths`), parce que les numéros diffèrent d'une borne à l'autre ;
  - `themes/` ;
  - `media/<n>/` : images, et vidéos en option.
  Ne sont jamais exportés : l'historique, les statistiques, `settings.json` (type de borne, kiosque…), les commandes et `scraper.json` (identifiants).
- **Aperçu avant import** : nombre de jeux nouveaux et déjà présents, liens vidéo, médias, profils. Le dossier commun des jeux est détecté (ex. `D:\Jeux`), avec la liste des fichiers de lancement introuvables ici.
- **Import** :
  - Une sauvegarde `avant-import` est faite d'abord.
  - Les catégories absentes sont créées.
  - Le début des chemins de lancement et de dossier de travail est remplacé si demandé.
  - Un jeu déjà présent (même nom, sans tenir compte des majuscules, ou même fichier) est seulement complété : métadonnées vides, lien vidéo, catégories, médias absents. Son chemin n'est jamais modifié.
  - Un profil du même nom est remplacé.
  - Les thèmes déjà présents ne sont jamais écrasés.
  - Les entrées d'archive avec `..`, `:` ou un chemin absolu sont ignorées.
  - Les vidéos YouTube manquantes sont mises en file de téléchargement.
- Test automatique (`system_tests`, 17 vérifications) : deux bibliothèques séparées, remplacement de chemin, jeu déjà présent, catégorie masquée retrouvée par son nom, thème, image.
- Essai réel sur une copie des données : 10 jeux, 23 images, 5 profils, 7,8 Mo.

## Démarrage avec Windows, surveillance, kiosque
- **Démarrage** : valeur `RacecabFrontend` dans `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`. Elle vaut pour cet utilisateur seulement, sans droits administrateur. Commande enregistrée : `RacecabFrontend.exe --watchdog --data <dossier>`.
- **Surveillance** (`--watchdog`) : processus sans fenêtre.
  - Fermeture normale (code 0) : la surveillance s'arrête.
  - Code 3 : redémarrage demandé (restauration).
  - Autre code (plantage) : relance après 2 s. Au-delà de 5 plantages en 10 minutes, abandon.
  - Journal : `watchdog.log` à côté du programme.
- **Redémarrage** sans surveillance : le programme se relance avec `--restart`, qui attend la fermeture de l'instance précédente (instance unique). Les options de test (`--actions`, `--screenshot`) ne sont jamais transmises.
- **Kiosque** : plein écran et curseur masqué imposés, « Quitter » retiré du menu de la borne.

## Installation
`package.ps1` produit `dist\RacecabFrontend-0.6.0.zip` (11,7 Mo), qui contient :
- le programme et ses DLL ;
- le runtime Visual C++ en déploiement local, pour qu'aucune installation supplémentaire ne soit nécessaire sur la borne ;
- `Installer.bat` et `Desinstaller.bat` ;
- le guide utilisateur, les nouveautés (`Nouveautes.md`) et les licences ;
- le thème neon80 ;
- `yt-dlp.exe` (vidéos YouTube), pris dans `third_party\yt-dlp\`.

`install.ps1` :
- pose les questions (type de borne, dossiers, démarrage, kiosque) ou fonctionne sans question (`-Quiet`) ;
- copie le programme et écrit `datadir.txt` ;
- appelle `RacecabFrontend.exe --setup-only`, qui écrit lui-même ses réglages ;
- crée les raccourcis ;
- en cas de mise à jour, reprend le dossier de données et le type de borne existants.

`uninstall.ps1` supprime uniquement les fichiers installés, les raccourcis et le démarrage automatique. Les données sont conservées.

## Vérifié (2026-11-23)
| Test | Résultat |
|---|---|
| Pistolet simulé (événements souris injectés dans SDL) : tir sur une catégorie de la bande | sélectionnée |
| Second tir sur la sélection | catégorie ouverte |
| Tir sur le bouton Menu | menu ouvert, avec texte d'aide pour le pistolet |
| Défaut trouvé et corrigé | avec peu d'éléments (bande sans bouclage), passer du premier au dernier laissait le centre vide. Le problème existait aussi au clavier |
| Vue Grande image : tir sur la flèche droite | jeu suivant, aperçus des voisins dans les bandes |
| Bouton Mosaïque, puis tir sur une case (Rambo) | grille de 10 jeux (page 1/1), puis Rambo affiché en grand |
| Saut à une lettre, nouvelle mise en page (description deux fois moins large, média élargi) | captures OK |
| Historique : partie de test de 5 s | enregistrée (date, durée, profil Racecab), affichée dans Statistiques |
| `system_tests.exe` (sauvegarde, restauration, archive piégée, sauvegardes automatiques, ZIP UTF-8 / vide / 3 Mo) | 25 vérifications, 0 échec |
| Restauration réelle avec redémarrage automatique | type de borne GunCab → Racecab restauré, sauvegarde « avant-restauration » créée |
| Surveillance : frontend tué de force | relancé en 2 s, puis surveillance arrêtée à la fermeture normale |
| Démarrage avec Windows | entrée créée puis retirée (test) |
| Paquet installé dans un dossier avec espaces et accents (mode GunCab, sans question) | réglages et profil GunCab corrects, la copie installée démarre |
| Désinstallation | programme retiré, données conservées |
| **Test d'endurance** (504 jeux, démonstration accélérée : un jeu toutes les 3 s, 126 vidéos) | **fuite trouvée et corrigée**, voir ci-dessous |

### Test d'endurance : fuite mémoire corrigée
Premier essai : la mémoire privée montait d'environ 170 Mo par minute (616 Mo → 2,4 Go en 8 min). La cause était dans le cache d'images, pas dans la vidéo :
- une image décodée en attente d'envoi à la carte graphique (au plus 4 envois par frame) pouvait être redemandée et décodée une seconde fois ;
- la seconde texture remplaçait alors la première sans la libérer.

Le même essai, vidéos coupées, fuyait autant, ce qui a permis d'écarter la vidéo. Correction faite, plus une sécurité : jamais deux textures pour un même fichier. Par ailleurs, le périphérique de décodage D3D11VA est désormais créé une seule fois au lieu d'une fois par vidéo.

| Après correction (6 min, vidéos D3D11VA) | Valeur |
|---|---|
| Mémoire privée | 290 Mo au départ, puis stable entre 440 et 500 Mo une fois le cache d'images rempli (plafond 256 Mo) |
| Working set | 118 à 131 Mo |
| CPU | 0,8 à 1,2 % (12 cœurs) |
| Handles / threads | stables (705-721 / 69-76) |

Le test d'une heure a été écourté à la demande. Les relevés après correction ne montrent plus de croissance. À refaire sur la borne pendant une longue démonstration : `tools\endurance.ps1`.
