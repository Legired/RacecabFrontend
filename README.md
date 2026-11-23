# Racecab Frontend

Frontend arcade natif Windows (C++20 / SDL3), conçu pour une borne de course pilotée au volant et aux boutons.
Voir [docs/00_Note_Architecture.md](docs/00_Note_Architecture.md) pour les choix techniques.

## État : phase 5 (finitions, stabilité, types de borne)

| Fonction | État | Testé comment |
|---|---|---|
| Deux types de borne avec le même programme : **Racecab** (volant) et **GunCab** (pistolets en mode souris, Retro Shooter RS3...) | ✅ | Tirs simulés : sélection, ouverture, boutons Retour / Menu à l'écran, menus au pistolet |
| GunCab : vue **Grande image** (bandes flèches gauche / droite) et vue **Mosaïque** (pages, tir sur un jeu = en grand, tir sur l'image = jouer) | ✅ | Tirs simulés et captures |
| Historique des parties et statistiques (plus joués, plus lancés, 100 dernières parties) | ✅ | Partie de test de 5 s enregistrée et affichée |
| Tri (nom, année, temps de jeu, lancements), saut à une lettre, Retour maintenu 5 s pour quitter | ✅ | Captures |
| Sauvegarde automatique / manuelle, restauration avec redémarrage | ✅ | 25 vérifications (`system_tests.exe`) + restauration réelle |
| Démarrage avec Windows, relance après plantage, mode kiosque | ✅ | Frontend tué de force : relancé en 2 s |
| Paquet d'installation (`package.ps1`, `Installer.bat`), désinstallation qui garde les données | ✅ | Installation GunCab sans question dans un dossier avec accents, puis désinstallation |
| Test d'endurance | ✅ après correction | **Fuite mémoire trouvée et corrigée** (cache d'images) : mémoire stable vers 480 Mo avec vidéos |
| Mise en page : description deux fois moins large, image / vidéo élargie (thème default) | ✅ | Capture |
| Essais sur la borne (volant, pistolets RS3, vrais jeux, plein écran) | ❌ **à faire par toi** | Aucun matériel pendant le développement |

Détails : [docs/Systeme-Bornes.md](docs/Systeme-Bornes.md). Guide d'utilisation : [docs/Guide-utilisateur.md](docs/Guide-utilisateur.md).

## Phase 4 (démonstration, profils, thèmes)

| Fonction | État | Testé comment |
|---|---|---|
| Mode démonstration : inactivité, durée par jeu, aléatoire ou dans l'ordre, catégories, vidéo, volume, vitesse | ✅ | Démarre à 5,0 s (réglage de test), change de jeu toutes les 3 s |
| Interruption immédiate à la moindre entrée, sans effet secondaire | ✅ | Valider ou bouton non associé : 16 ms, aucun jeu lancé, sélection restaurée |
| Aucun lancement de jeu par la démo | ✅ | Fonction volontairement absente |
| 5 profils (Racecab, GunCab, Arcade classique, Flipper, Air Hockey) : thème, catégories, sélection de jeux, mapping, vidéo, démo | ✅ | GunCab avec « Course » masqué : seul Rambo visible |
| Choix du profil au démarrage ou dans le menu de la borne (au volant) | ✅ | Capture de l'écran de choix |
| Thèmes sans recompiler : fond (dégradé, image), couleurs, polices, disposition, roue, informations affichées | ✅ | Thème neon fourni (autre disposition), modifiable dans l'administration |
| Reprise automatique du mapping et de l'état existants (profil Racecab) | ✅ | Testé sur une copie de données |

Détails : [docs/Profils-Themes-Demo.md](docs/Profils-Themes-Demo.md).

### Ajout : sources de médias inspirées de RetroBat et LaunchBox
| Fonction | État | Testé comment |
|---|---|---|
| Base LaunchBox (sans compte, hors ligne après téléchargement) : métadonnées, logo transparent, fond, capture | ✅ | Base réelle : 9 de tes 10 jeux associés automatiquement en 4 s (GTI Club 3 absent de LaunchBox) |
| Noms alternatifs reconnus (« Initial D Arcade Stage 4 » = « Initial D4 ») | ✅ | Base réelle et tests automatisés |
| Recherche ScreenScraper limitée à TeknoParrot (269) et Arcade (75), comme RetroBat | ✅ codé | Serveur de simulation ; appel réel toujours sans identifiants |
| Cascade : toutes les sources dans l'ordre, chacune ne complète que ce qui manque | ✅ | 13 vérifications avec simulation ; réel avec LaunchBox seul |

Détails : [docs/Medias.md](docs/Medias.md).

## Phase 3 (médias)

| Fonction | État | Testé comment |
|---|---|---|
| Lecture vidéo (FFmpeg, thread dédié, boucle, volume / muet, son) | ✅ | Vidéo H.264 + AAC de test : décodage **D3D11VA** confirmé, environ 1 % de CPU |
| Aucune vidéo décodée pendant le défilement ; arrêt immédiat, reprise 400 ms après | ✅ | Journal : reprise 0,5 s après le dernier mouvement |
| Fichier vidéo absent ou illisible → fond ou logo, erreur notée dans le journal | ✅ codé | Chemin d'erreur non provoqué volontairement |
| Récupération ScreenScraper (métadonnées, logo, fond, capture, vidéo) | ⚠️ | Validée sur un serveur de simulation ; **appel réel authentifié non testé** (identifiants requis) |
| Récupération SteamGridDB (logo, fond) | ⚠️ | Idem ; le refus du service réel sans clé est bien géré |
| Choix parmi plusieurs résultats, aucune association approximative | ✅ | Série sur tes 10 jeux : 2 automatiques, 5 à confirmer, 3 sans résultat |
| Cache local, médias existants conservés, champs remplis seulement s'ils sont vides | ✅ | Tests automatisés (`media_tests.exe`, 54 vérifications avec LaunchBox et la cascade) |
| Interface jamais bloquée pendant les recherches et téléchargements | ✅ | Une seule file de travail en arrière-plan |

Détails : [docs/Medias.md](docs/Medias.md).

## Phase 2 (commandes et utilisation de la borne)

| Fonction | État | Testé comment |
|---|---|---|
| Menu de la borne (favori, assistant, roue/bande, administration, quitter) | ✅ | Capture ; ouvert par *Menu* ou appui long sur *Retour* |
| Assistant de configuration au volant (9 actions, détection, doublons refusés, Échap = annuler) | ✅ | Parcours complet avec boutons joystick simulés, mapping enregistré puis utilisé pour naviguer |
| Assignation par détection dans l'administration | ⚠️ codée, non vérifiée à l'écran | Même mécanisme de capture que l'assistant (testé) |
| Choix du périphérique (association liée à « vJoy Device », etc.) | ✅ | Tests automatisés |
| Appui long, répétition + accélération, anti-rebond par entrée, anti double validation | ✅ | 39 vérifications automatiques (`input_tests.exe`) |
| Pédales / axes : position de repos mesurée | ✅ | Test automatisé (pédale à -1) |
| Aucune interférence avec les jeux : lecture coupée pendant le jeu + garde de 800 ms au retour | ✅ | Entrée envoyée au retour ignorée, celle envoyée 1,7 s après prise en compte |
| Tests réels avec BackForceFeeder / vJoy | ❌ **à faire sur la borne** | Aucun matériel pendant le développement |

Jeux de `H:\windowsloader` importés dans `H:\RacecabData` : les 10 `.bat` (Initial D 4, 5, 6, 7, 8 et Zero, Mario Kart DX, SWDC, GTI Club 3, Rambo). `swdc-debug.bat` n'a pas été importé. Liste : [imports/windowsloader.json](imports/windowsloader.json).
Ces jeux n'ont pas encore été lancés depuis le frontend.

Détails : [docs/Commandes.md](docs/Commandes.md).

## Phase 1 (prototype fonctionnel)

| Fonction | État | Testé comment |
|---|---|---|
| Fenêtre native plein écran / fenêtrée (SDL3, rendu Direct3D 11) | ✅ | Captures automatiques |
| Roue horizontale (mode roue ou bande simple), bouclage, animation | ✅ | 504 jeux, 60 actions à la suite |
| Logos PNG transparents + visuel de remplacement (nom) si absent | ✅ | 82 logos de test + 422 jeux sans logo |
| Chargement asynchrone + préchargement des voisins + cache LRU borné | ✅ | Mémoire stable une fois les logos chargés |
| Catégories et sous-catégories libres + Tous / Favoris / Récemment joués | ✅ | Navigation racine → catégorie → jeu → retour |
| Un jeu dans plusieurs catégories | ✅ | Import de test |
| Navigation clavier | ✅ | Actions injectées (même chemin de code que les touches) |
| Navigation manette / joystick (XInput, DirectInput, vJoy) | ⚠️ codée, **non testée** | Aucune manette branchée pendant les tests : à valider sur la borne (phase 2) |
| Ajout / modification / retrait manuel de jeux (administration) | ✅ | Interface vérifiée par capture ; enregistrement couvert par l'import |
| Import d'une liste JSON avec détection des doublons | ✅ | 504 jeux importés, 0 doublon |
| Lancement .bat et .exe, Job Object, retour automatique | ✅ | Voir ci-dessous |
| Restauration de la catégorie et du jeu sélectionné | ✅ | Après un jeu et après redémarrage |
| Statistiques (lancements, temps de jeu), favoris | ✅ | « Lancé 1 fois · 5 s » affiché après le test |
| Erreurs de lancement lisibles et journalisées | ✅ | Fichier manquant |

### Tests de lancement réalisés (2026-11-23)
- `.bat` qui se termine immédiatement en laissant une fenêtre enfant : le frontend a attendu la fermeture de la fenêtre (6 s), pas la fin du `.bat`.
- `.bat` console de 5 s : retour automatique après 5 s, 3 processus suivis (cmd, conhost, ping).
- `.exe` direct (charmap.exe) : lancement puis retour à la fermeture.
- Fichier inexistant : message rouge à l'écran + ligne `[ERROR]` dans le journal, frontend toujours utilisable.

### Mesures (machine de développement, 12 cœurs logiques, fenêtre 1280×720, VSync 60 Hz, 504 jeux)
| Situation | CPU (tous cœurs) | Working set | Mémoire privée |
|---|---|---|---|
| Défilement continu (5 jeux/s) | 0,8 à 1,6 % | 74 → 83 Mo | 110 → 194 Mo (chargement progressif des logos) |
| Au repos | 0,8 % | 83 Mo | 194 Mo, stable |
| Pendant un jeu | rendu arrêté, réveil toutes les 250 ms | | |

La mémoire privée comprend les allocations du pilote graphique pour les textures. Il faut encore la mesurer en plein écran sur la borne et faire un test d'endurance (phase 5).

### Pas encore fait
Scan automatique de dossiers de jeux.

## Utilisation rapide
- **F1** : administration (souris / clavier). **Tab**, bouton *Menu* ou **appui long sur Retour** : menu de la borne.
- Gauche/droite : jeu précédent/suivant. Haut/bas : catégorie précédente/suivante.
- Entrée : ouvrir / lancer. Échap : retour. **F** : favori. **F10** : quitter (avec confirmation).
- Commandes de la borne : menu de la borne, puis *Configurer les commandes (assistant)*.
- Dossier de données : `H:\RacecabData` par défaut (modifiable, voir [docs/Compilation.md](docs/Compilation.md)).

Documentation : [Guide d'utilisation](docs/Guide-utilisateur.md) · [Compilation et options](docs/Compilation.md) · [Format d'import](docs/Import.md) · [Lancement et supervision](docs/Lancement.md)
