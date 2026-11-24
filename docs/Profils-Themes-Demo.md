# Profils de borne, thèmes et mode démonstration

## Profils
Un profil adapte le frontend à un type de borne. Profils créés au premier lancement : **Racecab** (thème *default*), **GunCab** (thème *neon*), **Arcade classique**, **Flipper**, **Air Hockey**.

Un profil définit :
| Réglage | Où il est enregistré |
|---|---|
| thème | `config\profiles\<profil>.json` |
| catégories visibles (une catégorie masquée masque ses sous-catégories) | idem |
| sélection de jeux : seulement ceux rangés dans une catégorie visible (option) | idem |
| Favoris / Récemment joués affichés ou non | idem |
| réglages vidéo (activation, volume, muet, délai, boucle, décodage matériel) | idem |
| mode démonstration | idem |
| mapping des commandes | `config\input\<profil>.json` |
| dernière position de navigation | `config\state-<profil>.json` |

La bibliothèque (jeux, catégories, médias) est **commune** à tous les profils. Un profil ne fait que filtrer ce qu'il montre.

Changer de profil :
- au volant, dans le menu de la borne (*Menu* ou appui long sur *Retour*) : **Profil : … (changer)** ;
- au démarrage, si *Choisir le profil au démarrage* est coché (Administration > Profils) ; sinon c'est le dernier profil utilisé ;
- dans Administration > Profils, où l'on peut aussi créer un profil (copie du profil actif, mapping compris) ou en supprimer un.

**Migration** : le mapping et l'état de navigation des versions précédentes (`config\input.json`, `config\state.json`) sont repris automatiquement par le profil Racecab. Les anciens fichiers ne sont pas supprimés.

## Thèmes
Un thème est un dossier `themes\<nom>\` contenant `theme.json` (plus d'éventuelles images ou polices). Il se modifie sans toucher au programme : dans le fichier, ou dans Administration > Affichage (les changements sont visibles immédiatement, puis *Enregistrer le thème*).

| Élément | Clés de `theme.json` |
|---|---|
| Fond | `backgroundTop`, `backgroundBottom` (dégradé), `backgroundImage` (image, chemin relatif au dossier du thème ou absolu), `fanartBackgroundAlpha` |
| Couleurs | `panel`, `accent`, `text`, `textDim`, `wheel.bandColor` (RVBA 0-255) |
| Polices | `fontTitle`, `fontBody` (fichiers .ttf), `titleSize`, `bodySize`, `smallSize` (fractions de la hauteur) |
| Disposition | `mediaRect`, `infoRect`, `headerRect` : [x, y, largeur, hauteur] en fractions de l'écran |
| Roue | `wheel.mode` (`wheel` / `strip`), `y`, `visible`, `itemW`, `itemH`, `spacing`, `selectedScale`, `sideScale`, `falloff`, `curvature`, `tilt`, `sideAlpha`, `bandHeight` |
| Animations | `wheel.animSpeed` |
| Informations affichées | `showHeader`, `showMeta`, `showDescription`, `showStats` |

Deux thèmes sont fournis :
- **default** : média à gauche, infos à droite, roue en arc ;
- **neon** : média centré, infos dessous, bande simple de 9 logos, police Bahnschrift.

*Dupliquer le thème* (Administration > Affichage) crée une copie modifiable et l'applique au profil actif.

## Mode démonstration (attract mode)
- Démarre après une **inactivité** réglable (120 s par défaut), uniquement sur l'écran de navigation : pas dans un menu, l'administration ou pendant un jeu.
- Fait défiler les jeux du profil : **aléatoire** (jamais deux fois le même de suite) ou dans l'ordre, une durée réglable par jeu (12 s par défaut), en se limitant éventuellement à certaines catégories.
- Montre logo, informations et vidéo (si activée), au même volume que les vidéos du menu (un seul réglage : Affichage > Vidéo > Volume), ou en silence avec « Couper le son pendant la démo ». La vitesse de défilement est réglable.
- **Interruption prioritaire** : la moindre action (touche, bouton même non associé, chapeau, volant ou pédale sortis de leur position de repos) arrête la démo **immédiatement**, sans animation. La liste et le jeu sélectionnés avant la démo sont restaurés. Cette première action n'a pas d'autre effet : appuyer sur *Valider* pour réveiller la borne ne lance pas de jeu.
- **Ne lance jamais de jeu.** Cette fonction n'existe pas, volontairement.

### Vérifié (2026-11-23, tests automatisés dans l'application)
| Test | Résultat |
|---|---|
| Démarrage après 5 s d'inactivité (réglage de test) | démarre à 5,0 s |
| Changement de jeu toutes les 3 s, ordre aléatoire | OK (journal) |
| Interruption par *Valider* | 16 ms, aucun jeu lancé, sélection d'origine restaurée |
| Interruption par un bouton joystick non associé | 16 ms |
| Profil GunCab avec « Course » masqué | seul Rambo visible, thème neon appliqué |
| Défaut trouvé puis corrigé | la démo démarrait à tort juste après un changement de profil (calcul de durée qui débordait). Le même risque, corrigé aussi, pouvait faire démarrer une vidéo pendant un défilement |
