# Racecab Frontend : guide d'utilisation

Racecab Frontend est le menu de tes bornes d'arcade. Il affiche tes jeux, les lance, puis revient tout seul quand tu quittes le jeu. Il fonctionne sans Internet, sauf pour récupérer les images et vidéos des jeux.

Un même programme sert pour deux types de borne :

| Type de borne | Commandes |
|---|---|
| **Racecab** | volant, pédales, boutons (BackForceFeeder / vJoy, manette, clavier) |
| **GunCab** | pistolets (Retro Shooter RS3, Sinden, Gun4IR, AimTrak…) en mode souris |

---

## 1. Installation

1. Décompresse `RacecabFrontend-0.6.0.zip` n'importe où (clé USB, bureau…).
2. Double-clique sur **Installer.bat** et réponds aux questions :
   - **Type de borne** : 1 = Racecab, 2 = GunCab ;
   - **Dossier du programme** (par défaut `C:\RacecabFrontend`) ;
   - **Dossier des données** (par défaut `C:\RacecabData`) : bibliothèque, images, vidéos, réglages ;
   - **Démarrage avec Windows** : la borne arrive directement sur le frontend ;
   - **Mode kiosque** : pour une borne utilisée par d'autres (pas de « Quitter » dans le menu).
3. Un raccourci « Racecab Frontend » est créé sur le bureau et dans le menu Démarrer.

Aucun droit administrateur n'est nécessaire.

- **Mettre à jour** : le frontend vérifie tout seul au démarrage si une nouvelle version est publiée, et l'annonce par un message. Ensuite, dans Administration > **Système** > *Mises à jour* : *Télécharger la mise à jour*, puis *Installer et redémarrer le frontend*. Tes jeux, images et réglages sont conservés, ainsi que le type de borne, le démarrage avec Windows et le mode kiosque. Tu peux aussi relancer `Installer.bat` d'une version plus récente, avec le même dossier.
- **Désinstaller** : `Desinstaller.bat` dans le dossier du programme. Tes données ne sont pas supprimées.

---

## 2. Premier lancement : ajouter ses jeux

Tout se fait dans l'**administration** : touche **F1** au clavier, ou menu de la borne > *Administration*. Elle s'utilise à la souris.

1. **Onglet Jeux** : *Scanner un dossier* pour ajouter beaucoup de jeux d'un coup, ou *Ajouter un jeu* pour un seul jeu (nom, fichier à lancer `.exe` ou `.bat`, catégories).
   - **Scanner un dossier** : choisis le dossier qui contient tes jeux (par exemple `H:\LinuxLoader`). Le frontend trouve les jeux (`.exe`, `.bat`, jeux LinuxLoader) et les reconnaît avec la base LaunchBox (vrai nom, année, éditeur). Il propose une catégorie selon le genre : course vers la Racecab, tir vers la GunCab. La colonne *Borne* indique où chaque jeu apparaîtra.
   - Les jeux **déjà dans le frontend** sont signalés et ne peuvent pas être cochés. Les **doublons dans la liste** (le même jeu trouvé deux fois) sont signalés aussi : le meilleur lanceur reste coché.
   - Coche les jeux voulus, ajuste leur nom ou leur catégorie (un par un, ou *Appliquer aux jeux cochés*), puis clique sur *Ajouter*. Les logos et images sont ensuite récupérés automatiquement.
   On peut aussi importer une liste toute prête (voir `docs\Import.md`).
2. **Onglet Médias** :
   - *Télécharger / mettre à jour la base LaunchBox* (110 Mo, une seule fois, sans compte) ;
   - puis *Compléter les médias des jeux incomplets* : logos, fonds, captures et infos sont récupérés automatiquement ;
   - pour les jeux à confirmer, déplie la ligne et clique sur *Utiliser* devant le bon résultat.
   - **Vidéos** : dans la fiche du jeu (onglet Jeux > Médias), colle le **lien YouTube** d'une vidéo de gameplay le **début** de l'extrait (ex. `1:30`) et sa **durée** (ex. `1:51`, 30 s si vide, ou *Jusqu'à la fin*), puis clique sur *Télécharger l'extrait*. Usage personnel uniquement : YouTube interdit le téléchargement dans ses conditions.
   Avec une clé SteamGridDB (gratuite), d'autres images sont récupérées en plus.
   Pour un jeu sans vidéo, choisis un fichier `.mp4` à la main dans sa fiche (onglet Jeux, champ *Vidéo*).
3. **F1** pour revenir à la borne.

---

## 3. Utilisation sur une borne Racecab (volant et boutons)

Avant toute chose : menu de la borne > **Configurer les commandes (assistant)**. Le frontend demande chaque commande une par une : tourne le volant ou appuie sur le bouton voulu. Sans appui pendant 10 secondes, la commande actuelle est gardée. Échap au clavier annule.

| Action | Commande (par défaut au clavier) |
|---|---|
| Jeu précédent / suivant | volant gauche / droite (← →) |
| Catégorie précédente / suivante | haut / bas (↑ ↓) |
| Ouvrir une catégorie, lancer un jeu | Valider (Entrée) |
| Revenir en arrière | Retour (Échap) |
| Menu de la borne | bouton Menu (Tab) ou **Retour maintenu 1 seconde** |
| Ajouter / retirer des favoris | bouton Favori (F) |
| Quitter le frontend | **Retour maintenu 5 secondes**, puis Valider pour confirmer |

Maintenir gauche ou droite fait défiler de plus en plus vite.

Au lancement d'un jeu, un écran « Chargement du jeu… », avec la vidéo du jeu en fond s'il en a une, reste affiché jusqu'à ce que la fenêtre du jeu apparaisse : la fenêtre noire (cmd) des jeux lancés par un `.bat` est masquée. Quand on quitte le jeu, le frontend revient tout seul.

---

## 4. Utilisation sur une borne GunCab (pistolets)

Les pistolets **Retro Shooter RS3** se présentent à Windows comme une souris et un clavier. C'est leur mode par défaut, celui que le frontend utilise. Aucune bordure blanche n'est nécessaire : les RS3 utilisent des LED infrarouges.

Un viseur suit le pistolet. Il y a deux vues, qu'on change avec le bouton **Mosaïque / Grand** en haut à droite.

**Vue Grande image** :
- la vidéo ou l'image du jeu occupe le centre, avec son logo, son nom et ses infos ;
- **bande flèche gauche / droite** sur les côtés : tirer dessus passe au jeu précédent / suivant. Le logo du jeu voisin est affiché sous la flèche ;
- **tirer sur l'image** lance le jeu. Un court délai évite de lancer un jeu par deux tirs rapprochés.

**Vue Mosaïque** :
- plusieurs jeux à la fois (4 × 3 par défaut, réglable) ;
- les **bandes flèches** passent à la page précédente / suivante ;
- **tirer sur un jeu** l'affiche en grand, puis **tirer sur l'image** le lance.

Le frontend se souvient de la dernière vue utilisée.

Commandes communes aux deux vues :
- **Boutons à l'écran**, en haut à droite : **Retour**, **Mosaïque / Grand** et **Menu**.
- **Recharge, ou tir hors de l'écran** (clic droit) : retour. Maintenu 1 seconde : menu de la borne. Maintenu 5 secondes : quitter, avec confirmation.
- **Dans les menus**, tirer sur un choix le valide. Tirer à côté ferme le menu.

Les autres boutons du pistolet (Start, crédit, pompe…) envoient des touches. On peut les associer avec l'assistant de configuration, par exemple Start = Valider.

Réglages (Administration > **Système**) :
- disposition « grande image + mosaïque », ou la roue classique ;
- colonnes et lignes de la mosaïque ;
- viseur affiché ou non, sa couleur (blanc par défaut, jaune, vert, rouge ou couleur du thème) et sa taille. Il a toujours un contour noir, pour rester visible sur tous les fonds ;
- délai avant lancement ;
- bordure blanche (pour les pistolets Sinden seulement).

---

## 5. Le menu de la borne

| Entrée | Effet |
|---|---|
| Ajouter aux favoris | le jeu apparaît dans la catégorie *Favoris* |
| Aller à une lettre | saute directement aux jeux qui commencent par une lettre |
| Tri : Nom / Année / Temps de jeu / Lancements | change l'ordre des jeux |
| Profil | change de profil, s'il y en a plusieurs pour ce type de borne |
| Configurer les commandes | assistant (volant, boutons, touches du pistolet) |
| Affichage : roue / bande | change la présentation des logos |
| Administration | réglages complets (clavier / souris) |
| Quitter le frontend | absent en mode kiosque |

---

## 6. Démonstration (attract mode)

Après 2 minutes sans action, la borne présente les jeux un par un, avec leur vidéo. La moindre action (bouton, volant, tir) arrête aussitôt la démonstration, **sans lancer de jeu**. Réglages : Administration > Profils.

---

## 7. Statistiques et historique

Administration > **Statistiques** :
- nombre de parties et temps de jeu total ;
- jeux les plus joués et les plus lancés ;
- les 100 dernières parties (date, durée, profil).

Un bouton permet de tout remettre à zéro.

---

## 8. Sauvegardes

Administration > **Système** > *Sauvegarde de la configuration*.

- **Automatique** : une fois par jour au démarrage. Les 10 plus récentes sont gardées.
- **Manuelle** : *Créer une sauvegarde maintenant*, éventuellement avec les médias.
- **Restaurer** : choisir une sauvegarde dans la liste. Le frontend sauvegarde d'abord l'état actuel, puis redémarre sur la configuration restaurée.

Les sauvegardes sont dans `<dossier des données>\backups`. Ce sont des fichiers `.zip` à copier sur une clé USB pour les garder ailleurs.

---

## 9. Partager sa configuration avec une autre borne

Administration > **Système** > *Partager la configuration*.

- **Exporter la configuration** : crée un fichier `.zip` contenant :
  - les jeux (chemins, options de lancement, catégories, infos) ;
  - les liens YouTube des vidéos (lien, début, durée) ;
  - les profils et les thèmes ;
  - les images si la case est cochée, et les vidéos aussi si tu le veux (fichier plus lourd).
- **Importer une configuration** sur l'autre borne :
  1. choisis le fichier : le frontend indique combien de jeux sont nouveaux ou déjà présents ;
  2. si les jeux sont rangés ailleurs sur cette borne, corrige *Dossier des jeux ici* (par exemple `D:\Jeux` → `E:\Arcade`), puis clique sur *Vérifier les chemins* ;
  3. clique sur *Importer*. Les vidéos YouTube sont téléchargées ensuite, en tâche de fond.
- Un jeu déjà présent (même nom ou même fichier) n'est jamais écrasé : il reçoit seulement ce qui lui manque. Une sauvegarde est faite juste avant l'import (*Restaurer* pour revenir en arrière).
- Ne sont jamais exportés : l'historique, les statistiques, le type de borne, les commandes et les identifiants.

---

## 10. Démarrage avec Windows et mode kiosque

Les deux se règlent dans Administration > **Système**, ou à l'installation.

- **Démarrage avec Windows** : le frontend se lance à l'ouverture de la session. S'il se ferme anormalement (plantage), il est relancé automatiquement en 2 secondes. S'il plante plus de 5 fois en 10 minutes, il n'est plus relancé, pour éviter une boucle.
- **Mode kiosque** : plein écran imposé, curseur masqué, pas de « Quitter » dans le menu. Pour quitter quand même : **Retour maintenu 5 secondes**, ou Administration (F1) > Système > *Quitter le frontend*.

Pour que la borne démarre sans mot de passe, active l'ouverture de session automatique de Windows (réglage Windows, hors du frontend).

---

## 11. En cas de problème

| Problème | Que faire |
|---|---|
| Le volant ou les boutons ne font rien | menu de la borne (clavier : Tab) > Configurer les commandes |
| L'écran « Chargement du jeu… » reste affiché alors que le jeu tourne | Retour l'efface. Pour un jeu lancé par un autre programme (Steam…), renseigne le *processus à surveiller* dans sa fiche |
| Une vidéo YouTube ne se télécharge plus | Administration > Médias > *Mettre à jour yt-dlp* |
| Un jeu ne se lance pas | un message rouge s'affiche. Vérifie le chemin du jeu dans l'onglet Jeux. Le détail est dans le journal |
| Pas d'images pour un jeu | onglet Médias > *Toutes les sources*, ou choisis un fichier à la main dans la fiche du jeu |
| Le frontend ne revient pas après un jeu | il attend que toutes les fenêtres du jeu soient fermées. Voir l'option *processus à surveiller* de la fiche du jeu (`docs\Lancement.md`) |
| Mauvaise manipulation dans les réglages | Système > Sauvegardes > *Restaurer* une sauvegarde de la veille |

**Journal** : `<dossier des données>\logs\frontend.log`. **Surveillance** : `watchdog.log` dans le dossier du programme.
