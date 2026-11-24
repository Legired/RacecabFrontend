# Médias : vidéos, logos, fonds et métadonnées

## Où sont les médias
Dans `<données>\media\<id du jeu>\` :

| Fichier | Usage |
|---|---|
| `logo.png` | logo transparent de la roue (prioritaire) |
| `fanart.jpg` ou `.png` | fond d'écran atténué et zone média en l'absence de vidéo |
| `screenshot.jpg` | utilisé comme fond si aucun fanart n'existe |
| `video.mp4` (ou `.mkv`, `.webm`, `.avi`) | vidéo de la zone média |

Un chemin choisi à la main dans la fiche du jeu (onglet Jeux, section Médias) est toujours prioritaire.
Un média absent est remplacé par le nom du jeu. Une vidéo absente ou illisible laisse la place au fond ou au logo, et l'erreur est notée dans le journal.

## Lecture vidéo
- FFmpeg (bibliothèques LGPL compilées par vcpkg). Décodage matériel **D3D11VA** quand le format le permet (H.264 vérifié), sinon décodage logiciel automatique.
- Une seule vidéo décodée à la fois, dans un thread séparé : la roue et les commandes ne sont jamais bloquées.
- La vidéo démarre quand la sélection reste immobile pendant 400 ms (réglable). Pendant un défilement, rien n'est décodé.
- Elle s'arrête au changement de jeu (sans bloquer) et au lancement d'un jeu.
- Réglages (Administration > Affichage > Vidéo) : activation, boucle, volume, muet, délai, décodage matériel, fond flou.
- **Bandes noires de la vidéo** (activé par défaut, *Retirer les bandes noires des vidéos*). À la première lecture d'une vidéo, un thread à part décode 5 images réparties dans la vidéo, en décodage logiciel. Il mesure les bordures sombres (luminance inférieure à 40 sur plus de 99 % de la ligne ou de la colonne) et garde la plus petite zone qui contient le contenu des 5 images. Une bande n'est retirée que si elle fait au moins 2 % de l'image. Le résultat va dans `<données>\cache\videocrop.json`, avec comme clé le chemin, la taille et la date du fichier : une vidéo remplacée est réanalysée. L'affichage n'utilise ensuite que la zone utile, pour le cadre, le fond flou et l'écran de chargement. Le fichier vidéo n'est jamais modifié.
- **Fond flou** (activé par défaut) : quand la vidéo n'a pas la forme du cadre (écart de plus de 3 %), les bandes sont remplies par la vidéo elle-même, agrandie, floue et assombrie. Il n'y a ni déformation ni coupe. Le flou est calculé sur la carte graphique par deux réductions successives (1/10, puis 1/40) avec filtrage linéaire : son coût est négligeable.

Mesure (vidéo de test H.264 640×360 avec son, fenêtre 1280×720) : environ 1 % de CPU (12 cœurs), 90 à 100 Mo de mémoire, stable.

## Récupération en ligne
Administration > onglet **Médias**.

### Services
| Service | Ce qu'il apporte | Accès |
|---|---|---|
| **LaunchBox Games Database** (source par défaut) | métadonnées (année, éditeur, genre, description en anglais), logo transparent (« Clear Logo »), fond, capture, **vidéo** (lien YouTube, avec yt-dlp, voir plus bas) | **aucun compte**. La base complète (`Metadata.zip`, ~110 Mo, mise à jour chaque jour par LaunchBox) est téléchargée une fois, puis la recherche se fait hors ligne. Les images viennent de images.launchbox-app.com, une par une |
| **ScreenScraper** | métadonnées (année, éditeur, genre, description), logo (`wheel-hd` / `wheel`), fond, capture, vidéo | identifiants **développeur** à demander sur le forum de ScreenScraper. Compte utilisateur facultatif (plus de requêtes autorisées). API réservée aux logiciels gratuits. Données sous licence CC BY-NC-SA |
| **SteamGridDB** | logo transparent, fond (« hero ») | clé API gratuite : compte steamgriddb.com, puis Préférences > API |

Les identifiants sont enregistrés dans `config\scraper.json`, sur la machine. Ils ne sont envoyés qu'au service concerné.
Si la source préférée n'est pas utilisable (base non téléchargée, identifiants absents), la première source disponible est prise.

### Cascade de sources (le plus complet possible)
Aucune source ne couvre tout pour des jeux PC d'arcade. Le bouton **Compléter les médias des jeux incomplets** (et **Toutes les sources** pour un seul jeu) interroge donc les sources disponibles l'une après l'autre :

1. la source préférée (LaunchBox par défaut) ;
2. puis LaunchBox, ScreenScraper, SteamGridDB (celles qui sont configurées).

- Chaque source ne télécharge **que ce qui manque encore** : logo, fond, capture, vidéo (ScreenScraper, ou LaunchBox avec yt-dlp), métadonnées. Une source qui ne peut rien apporter de manquant n'est pas interrogée. Exemple : SteamGridDB est ignoré une fois logo et fond obtenus.
- Toujours la même règle prudente, **source par source** : téléchargement uniquement si un seul résultat porte exactement le nom du jeu. Sinon, les résultats de toutes les sources sont réunis dans *À confirmer*, avec le nom de la source.
- Métadonnées : la première source remplit les champs, les suivantes complètent les champs vides. Une **description en français** (ScreenScraper) remplace une description en anglais (LaunchBox) au sein d'une même récupération. Les champs déjà remplis dans la fiche du jeu ne sont jamais écrasés.
- Une source dont les identifiants sont refusés, ou dont le quota est atteint, est écartée pour la suite de la série. Elle revient dès qu'on enregistre les réglages.
- Jeu « incomplet » : sans logo ou sans fond. La vidéo compte aussi quand une source peut en fournir : ScreenScraper configuré, ou base LaunchBox avec yt-dlp installé.

### Base LaunchBox
- *Télécharger / mettre à jour la base LaunchBox* : télécharge `Metadata.zip` dans `<données>\cache\launchbox\`, puis construit l'index `launchbox.db` (environ 1 s, 8 Mo). Le zip est gardé pour pouvoir reconstruire l'index sans Internet.
- Seules les plateformes choisies sont indexées : **Arcade** par défaut (c'est là que LaunchBox range les jeux TeknoParrot et les bornes PC). Pour en ajouter (ex. `Arcade, Windows`) : modifier le champ, *Enregistrer*, puis *Reconstruire l'index*.
- Les **noms alternatifs** de LaunchBox comptent comme noms exacts. Exemple : ton « Initial D Arcade Stage 4 » s'appelle « Initial D4 » chez LaunchBox, avec « Initial D Arcade Stage 4 » en nom alternatif. Il est donc associé automatiquement.
- Une mise à jour ratée (coupure, fichier invalide) laisse l'ancien index en place.
- Conditions d'utilisation : LaunchBox ne publie pas de conditions explicites pour cette base (vérifié le 2026-11-23). Elle est utilisée ici comme le fait LaunchBox : un téléchargement de la base, puis des images à l'unité.

### Vidéos LaunchBox (liens YouTube, avec yt-dlp)
La base LaunchBox donne pour environ 82 000 jeux (toutes plateformes) un lien vers une vidéo YouTube (`<VideoURL>`). L'index garde ces liens pour les plateformes choisies. Un index construit avant cette version est reconstruit automatiquement au démarrage, depuis le `Metadata.zip` déjà présent, sans téléchargement.

- Le téléchargement passe par **yt-dlp** (Unlicense, domaine public). Il est fourni dans le paquet et copié à côté du programme par l'installation. Le bouton *Mettre à jour yt-dlp* (onglet Médias) lance `yt-dlp -U`. yt-dlp est cherché dans cet ordre :
  1. le chemin indiqué dans Administration > Médias ;
  2. à côté de `RacecabFrontend.exe` ;
  3. dans `<données>	ools\` ;
  4. dans le PATH.
- Le lien, le début et la durée sont mémorisés dans la fiche du jeu (base v3 : `video_url`, `video_start`, `video_duration`). Ils servent à l'export de la configuration et sont repris à l'import.
- **Lien choisi à la main** (recommandé) : fiche du jeu > Médias > *Lien YouTube*, *Début* (`1:30`, `90`, ou `t=` du lien), *Durée* (ex. `1:51`, vide = 30 s) ou *Jusqu'à la fin*, puis *Télécharger l'extrait*. L'extrait remplace la vidéo du jeu.
- **Vidéo proposée par LaunchBox** : téléchargement automatique pendant *Compléter les médias*, seulement si l'option *Télécharger automatiquement la vidéo proposée par LaunchBox* est cochée. Elle est **décochée par défaut**, parce que ces vidéos sont de qualité très variable : menus, création de compte, vidéos de 30 minutes. Vérifié sur tes jeux, 2026-11-23.
- Vidéo H.264 et son AAC sont téléchargés séparément : **ffmpeg n'est pas nécessaire**. Qualité : 720p 30 i/s de préférence, puis 480p ou 360p pour une vidéo longue (plus de 400 Mo). Au-delà de 1,5 Go, la vidéo est refusée avec un message clair. Exemple vérifié : vidéo de 34 min (739 Mo en 720p60), extrait de 30 s à 5:00 obtenu en 90 s, 480p, 3,3 Mo.
- Le frontend garde un **extrait** (30 s à partir de 10 s par défaut, réglable), assemblé sans réencodage avec FFmpeg intégré, puis écrit `video.mp4` (index en tête du fichier, lecture immédiate). Les fichiers complets sont supprimés.
- yt-dlp est lancé fenêtre masquée, avec `--ignore-config`. Il est arrêté en cas d'annulation, ou après 15 minutes.
- Sans yt-dlp, la récupération LaunchBox indique « vidéo : yt-dlp non installé ». Pour un jeu sans lien, elle indique « pas de lien vidéo dans LaunchBox ».
- **Usage personnel uniquement** : les conditions d'utilisation de YouTube interdisent le téléchargement, et les vidéos appartiennent à leurs auteurs.

### Filtre de systèmes ScreenScraper (comme RetroBat)
RetroBat ne cherche pas un jeu par son seul titre : il précise le système. Le frontend fait de même : les recherches ScreenScraper sont limitées aux systèmes **269 (TeknoParrot)** et **75 (Arcade)**, réglables dans *ScreenScraper : systèmes recherchés*. Cela évite d'associer la version console d'un jeu (ex. *Rambo* sur Master System). Sans résultat dans ces systèmes, les autres sont proposés, mais **jamais associés automatiquement**.

Correspondance des noms (vérifiée avec de vrais identifiants) :
- ScreenScraper donne plusieurs noms par jeu : le nom est jugé identique si **l'un d'eux** correspond (*Initial D Arcade Stage 7 AAX* = « Initial D Arcade Stage 7 AA X »). Un « The » initial est ignoré (*The Walking Dead* = « Walking Dead »).
- Sa recherche ne trouve rien si un mot diffère : le frontend réessaie alors **sans le dernier mot**, jusqu'à deux mots. Ce qui est trouvé ainsi n'est associé automatiquement que si le nom est identique, sinon c'est proposé (ex. *GTI Club 3* → « GTI Club 3 - Supermini Festa! »).
- ScreenScraper renvoie parfois d'autres systèmes malgré le filtre : seul un jeu d'un système demandé est associé automatiquement, et les fiches « non Jeu » sont écartées.

### Fonctionnement
- **Rien ne se fait pendant la navigation** : les recherches et téléchargements ne partent qu'à l'ajout d'un jeu (option, désactivée par défaut) ou sur demande.
- Une seule requête à la fois, dans un thread de travail : l'interface n'est jamais bloquée, et la limite de 1 requête simultanée des comptes gratuits est respectée.
- **Jeu par jeu** : lancer une recherche (le nom est modifiable), puis cliquer sur *Utiliser* devant le bon résultat.
- **Tous les jeux sans logo** : le téléchargement est automatique **uniquement si un seul résultat porte exactement le même nom ou nom alternatif** (majuscules, espaces et ponctuation ignorés). Sinon, le jeu apparaît dans la liste *À confirmer*, avec les résultats proposés. Aucune association silencieuse sur un nom approximatif.
- Les médias déjà présents sont conservés, sauf si *Remplacer les médias existants* est coché.
- Les métadonnées téléchargées ne remplissent que les **champs vides**. Les saisies manuelles ne sont jamais écrasées.
- Une série s'interrompt d'elle-même si les identifiants sont refusés ou si le quota est atteint (HTTP 429/430).
- Les fichiers sont vérifiés avant enregistrement : signature PNG/JPEG/MP4. Les réponses texte comme « NOMEDIA » sont rejetées. L'écriture est atomique, donc jamais de fichier à moitié écrit.
- *Supprimer les médias téléchargés de ce jeu* vide le dossier `media\<id>` après confirmation.

### Ce qui a été vérifié, et comment
| Vérification | Résultat |
|---|---|
| SteamGridDB réel sans clé valide | message « identifiants ou clé refusés (HTTP 401) » |
| ScreenScraper réel sans identifiants développeur | message « Erreur de login : Vérifier vos identifiants développeur ! » |
| Parcours complet avec un serveur de simulation local (`tools/mock_media_server.py`), formats repris du client officiel SteamGridDB et du code de Skyscraper | recherche, choix, logo, fond, capture, vidéo et métadonnées OK |
| Série sur tes 10 jeux (avec simulation) | 2 correspondances exactes téléchargées, 5 à confirmer, 3 sans résultat |
| **Appel réel authentifié** ScreenScraper (2026-11-24) | OK : connexion, recherche, fiche détaillée et médias conformes au format attendu |
| Cascade réelle ScreenScraper sur une copie sans médias | Initial D 7 AAX, Rambo, Mario Kart : logo, fond, capture, vidéo. The Walking Dead : logo et fond (pas de capture ni de vidéo chez ScreenScraper). GTI Club 3 et Hummer Extreme : proposés à confirmer |
| SteamGridDB réel | non testé |
| **LaunchBox réel** (2026-11-23) : téléchargement de `Metadata.zip` (110 Mo), index | 7 213 jeux Arcade, 2 401 noms alternatifs, 26 665 images, index construit en 1,2 s |
| **LaunchBox réel : série sur tes 10 jeux** (copie des données) | **9 associations automatiques** en 4 s, 1 à confirmer (GTI Club 3, absent de LaunchBox : 4 « GTI Club » proposés). Logos obtenus pour 8 jeux ; Initial D 6 AA n'a pas de logo chez LaunchBox (fond seulement). Fonds pour 5 jeux, captures pour 6 |
| LaunchBox avec la simulation : zip, nom alternatif, plateforme écartée, entités XML, mise à jour ratée | OK (`media_tests.exe`) |
| Filtre ScreenScraper 269/75 avec la simulation : Rambo console écarté ; sans résultat, propositions jamais automatiques | OK (`media_tests.exe`) |
| Cascade avec la simulation : LaunchBox puis ScreenScraper, jeu déjà complet, aucun nom exact, identifiants refusés | OK |
| Cascade réelle sur la copie de tes jeux (LaunchBox seul disponible) | 4 jeux incomplets : rien de plus chez LaunchBox ; GTI Club 3 à confirmer. Le reste viendra de ScreenScraper / SteamGridDB une fois tes identifiants saisis |
| Total `media_tests.exe` | 54 vérifications, 0 échec |

## Tests
```powershell
python tools\mock_media_server.py 8765 testdata\test_video.mp4    # dans un terminal à part
build\Release\media_tests.exe 8765
build\Release\make_test_video.exe sortie.mp4 6                     # vidéo H.264 + AAC de test (Media Foundation)
```
