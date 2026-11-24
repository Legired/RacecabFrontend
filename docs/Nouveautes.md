# Racecab Frontend : nouveautés

## 0.7.0 (2026-11-24)

**Mise à jour** : Administration > Système > *Mises à jour* > *Télécharger*, puis *Installer et redémarrer*. Tes jeux et réglages sont conservés.

### Ajout de jeux : émulateurs et ROMs
- **Onglet Jeux > *Émulateurs et ROMs*** : choisis le dossier de tes émulateurs et celui de tes ROMs. Le frontend reconnaît sm2-emu, Model 2 Emulator, Supermodel, Flycast, Demul, MAME et TeknoParrot. Il nomme chaque ROM (« Daytona USA », « Scud Race ») et propose l'émulateur qui convient, qu'on peut changer dans une liste. Les jeux configurés dans TeknoParrot sont proposés aussi.
- **Lanceur « Custom »** pour chaque jeu trouvé : on choisit soi-même le programme, les arguments et le dossier (ex. un launcher maison qui règle des options avant de lancer le jeu).

### Borne
- **Éteindre / Redémarrer la borne** depuis le menu de la borne, avec confirmation. **Extinction automatique** possible à heure fixe ou après un temps sans joueur, avec une minute pour annuler (Administration > Système).
- **Sons de navigation et musique de fond** : bruitages intégrés (remplaçables), musique aléatoire depuis le dossier `music`, qui laisse la place au son des vidéos et se coupe pendant les jeux (Administration > Affichage > Sons).
- **Test des commandes** dans le menu de la borne : volant, pédales, boutons et pistolets affichés en direct.

### ScreenScraper
- **Testé avec de vrais identifiants** : logo, fond, capture, vidéo et description en français arrivent bien.
- **Meilleure reconnaissance des noms** : *Initial D Arcade Stage 7 AAX*, *The Walking Dead* ou *Rambo* sont maintenant trouvés et associés tout seuls. Un jeu au nom différent chez ScreenScraper (ex. *GTI Club 3*) est proposé à confirmer.

## 0.6.2 (2026-11-24)

**Mise à jour** : Administration > Système > *Mises à jour* > *Télécharger*, puis *Installer et redémarrer*. Tes jeux et réglages sont conservés.

### Volume des vidéos
- **Un seul volume** pour toutes les vidéos : Administration > Affichage > Vidéo > *Volume* (menu, démonstration et écran de chargement). La démonstration n'a plus de curseur à part, seulement une case *Couper le son pendant la démo*.
- **Le réglage est enregistré tout seul** : avant, il fallait cliquer sur « Enregistrer », sinon il était perdu au démarrage suivant. C'est la même chose pour les autres réglages vidéo et ceux de la fenêtre.
- **Le curseur suit l'oreille** et s'affiche en pourcentage : 50 % donne maintenant un son nettement plus bas. Avant, on n'entendait presque aucune différence entre 30 % et 100 %. Ton volume actuel est converti pour garder le même niveau sonore.
- **Muet agit tout de suite**, même sur une vidéo déjà lancée.

## 0.6.1 (2026-11-23)

**Mise à jour** : Administration > Système > *Mises à jour* > *Télécharger*, puis *Installer et redémarrer*. Tes jeux et réglages sont conservés.

### Corrections
- **Vidéos YouTube : le début demandé est respecté à l'image près.** Avant, l'extrait pouvait commencer quelques secondes trop tôt, à l'image clé précédente. Et si la durée dépassait la fin de la vidéo, le début était avancé pour garder la durée. Maintenant, l'extrait commence exactement au début choisi et s'arrête à la fin de la vidéo si besoin. Le message indique la durée réellement obtenue.
- Retélécharge la vidéo d'un jeu depuis sa fiche pour profiter de la correction : le lien, le début et la durée sont gardés.

## 0.6.0 (2026-11-23)

**Mise à jour depuis la 0.5.0** : relance `Installer.bat` de cette version et choisis le même dossier. Tes jeux, images, vidéos et réglages sont conservés.

### Nouveau look
- **Thème « neon80 »**, style synthwave années 80 : soleil rayé, grille néon, couleurs rose et cyan. Il est installé avec le frontend. Pour le choisir : Administration > Profils > Thème.
- **Fond flou autour des vidéos** : quand une vidéo n'a pas la forme du cadre, les bandes noires sont remplacées par la vidéo elle-même, floutée et assombrie. L'image n'est ni déformée ni coupée. Réglage : Administration > Affichage > Vidéo.
- **Bandes noires des vidéos retirées automatiquement** : quand une vidéo a des bandes noires dans l'image elle-même (vidéo YouTube filmée avec des bordures, par exemple), elles sont détectées une fois par vidéo puis masquées. Le fichier n'est pas modifié.

### GunCab (pistolets)
- Nouveau design de la vue au pistolet :
  - grosses flèches rondes animées, avec l'aperçu du jeu précédent ou suivant ;
  - pastille « Tirez pour jouer » avec une jauge qui se remplit ;
  - boutons Retour / Mosaïque / Menu avec des icônes.
- **Viseur blanc avec un contour noir**, visible sur tous les fonds. Sa couleur (blanc, jaune, vert, rouge ou couleur du thème) et sa taille se règlent dans Administration > Système.

### Lancement des jeux
- **Écran de chargement** : il reste affiché jusqu'à ce que la vraie fenêtre du jeu apparaisse. La fenêtre noire (cmd) des jeux lancés par un `.bat` est masquée. Retour l'efface tout de suite si besoin.
- Pendant le chargement, la **vidéo du jeu** passe en fond, son baissé.

### Vidéos des jeux
- **Lien YouTube par jeu** : dans la fiche du jeu (onglet Jeux > Médias), colle un lien et indique le début (ex. `1:30`) et la durée (ex. `1:51`, ou *Jusqu'à la fin*). L'extrait est téléchargé et remplace la vidéo du jeu. Un message indique quand le téléchargement est terminé.
- **yt-dlp**, l'outil qui télécharge les vidéos, est fourni et installé avec le frontend. Si YouTube change et que les téléchargements échouent : Administration > Médias > *Mettre à jour yt-dlp*.
- Le lien, le début et la durée sont mémorisés dans la fiche du jeu.
- Option facultative, désactivée par défaut : télécharger automatiquement la vidéo que propose LaunchBox. Leur qualité est très variable.
- Usage personnel uniquement : les conditions d'utilisation de YouTube interdisent le téléchargement.

### Ajout automatique de jeux
- Administration > Jeux > **Scanner un dossier** : le frontend trouve les jeux du dossier (`.exe`, `.bat`, jeux LinuxLoader lancés directement), les reconnaît avec la base LaunchBox et propose la catégorie, donc la borne. Coche, ajuste, puis ajoute tout d'un coup.
- **Aucun doublon** : les jeux déjà présents sont signalés et ne peuvent pas être cochés, et le même jeu trouvé deux fois n'est proposé qu'une fois. Les abréviations courantes (`id4`, `twd`, `swdc`, `mkdx`…) sont reconnues.
- Les installateurs, désinstalleurs, outils et dossiers de sauvegarde sont ignorés. Un `.bat` qui se termine par `pause` garde sa console visible, pour qu'on puisse la fermer.

### Mises à jour automatiques
- Au démarrage, le frontend regarde si une nouvelle version est publiée sur GitHub et l'annonce. Administration > Système > *Mises à jour* : *Télécharger*, puis *Installer et redémarrer*. Il n'y a rien à refaire à la main, et tes jeux et réglages sont conservés.
- Le paquet est vérifié (empreinte SHA-256) avant l'installation.

### Partager sa configuration
- Administration > Système > **Exporter la configuration** : un fichier `.zip` avec tes jeux (chemins, catégories, infos), les liens YouTube des vidéos, les profils et les thèmes, et les images si tu le souhaites.
- Sur l'autre borne : **Importer une configuration**. Si les jeux n'y sont pas rangés au même endroit, indique le dossier des jeux : le début des chemins est remplacé. Les vidéos YouTube sont téléchargées automatiquement.
- Un jeu déjà présent n'est jamais écrasé, seulement complété. Une sauvegarde est faite avant chaque import.

### Profils
- Un profil peut être proposé sur la Racecab, sur la GunCab, sur les deux, ou sur **aucune borne** (profil gardé mais masqué).
- Les catégories masquées dans un profil disparaissent partout : Tous les jeux, Favoris, Récemment joués et démonstration.

### Corrections
- Une nouvelle vidéo remplace bien l'ancienne, même si celle-ci était en cours de lecture.
- Les vidéos YouTube très longues sont téléchargées en qualité réduite au lieu d'être ignorées sans message.

### À savoir
- Au premier lancement, Windows peut afficher « Windows a protégé votre ordinateur » : le programme n'est pas signé. Clique sur *Informations complémentaires*, puis *Exécuter quand même*.
