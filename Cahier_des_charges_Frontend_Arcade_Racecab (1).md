# Cahier des charges --- Frontend Arcade natif pour Racecab

**Document de spécifications à transmettre à l'agent IA local**\
**Plateforme cible :** Windows 10/11\
**Type :** application de bureau native, plein écran, hors ligne après
configuration\
**Priorités :** légèreté, réactivité, fiabilité, simplicité de
configuration et pilotage sans clavier ni souris.

------------------------------------------------------------------------

## 1. Mission et résultat attendu

Développer un véritable frontend Windows pour organiser et lancer une
bibliothèque de jeux d'arcade, avec une expérience visuelle inspirée
d'HyperSpin et de BigBox.

L'interface doit notamment proposer une roue ou une bande horizontale de
logos en bas de l'écran. La sélection se déplace de gauche à droite et
inversement. Le jeu sélectionné est mis en évidence et ses informations
ainsi que ses médias sont présentés dans le reste de l'écran.

Le logiciel est destiné en priorité à une borne de course (Racecab),
mais son architecture doit permettre de l'utiliser également avec
d'autres types de bornes : GunCab, flipper, air hockey et arcade
classique.

**Ce projet ne doit en aucun cas devenir une page web.** Le livrable
attendu est un logiciel Windows natif, compilable, exécutable et
maintenable.

## 2. Principes non négociables

1.  Application native Windows : pas de site web, pas d'interface dans
    un navigateur et pas d'Electron.
2.  Faible consommation CPU et RAM.
3.  Navigation extrêmement réactive, sans latence perceptible.
4.  Animations fluides, sans ralentir les commandes.
5.  Utilisation complète au volant et aux boutons, sans clavier ni
    souris au quotidien.
6.  Prise en charge des jeux lancés par fichiers `.bat` et exécutables
    `.exe`.
7.  Retour automatique au frontend à la fermeture d'un jeu.
8.  Gestion locale des jeux, paramètres et médias ; Internet ne doit pas
    être nécessaire pour utiliser la bibliothèque une fois les assets
    récupérés.
9.  Architecture modulaire : ne pas tout coder dans un seul fichier.
10. Chaque fonctionnalité doit être réellement opérationnelle et testée
    ; pas de boutons factices ni de compatibilités supposées.

## 3. Étude technique préalable

Avant de coder, comparer les solutions natives adaptées à Windows et
présenter un choix argumenté.

Pistes à évaluer : - **C++ avec SDL2/SDL3** : intéressant pour une
interface 2D légère, une boucle graphique maîtrisée et une faible
empreinte. - **C++ avec Qt** : riche en composants, mais évaluer le
poids du runtime et la complexité. - **C# avec .NET / WPF** :
développement et maintenance accessibles, mais mesurer la consommation
et la fluidité. - Autres solutions natives pertinentes, si elles
apportent un avantage démontré.

Ne pas choisir une technologie uniquement parce qu'elle est familière.
Le choix doit être justifié par des mesures ou des arguments techniques
concrets : démarrage, mémoire, CPU, rendu, lecture vidéo, gestion des
entrées et facilité de distribution.

### Architecture souhaitée

Séparer au minimum : - moteur graphique et navigation ; - bibliothèque
et base de données des jeux ; - catégories et profils ; - gestionnaire
des médias et du cache ; - lecteur vidéo ; - gestionnaire de
périphériques et mapping des commandes ; - lancement et supervision des
processus de jeux ; - configuration et administration ; - mode
démonstration / attract mode.

Utiliser une base locale SQLite pour les données structurées si cela est
pertinent, et des fichiers JSON lisibles pour les paramètres et thèmes.
Les médias doivent être stockés localement dans une arborescence claire.

## 4. Interface principale et navigation

### 4.1 Écran principal

Prévoir une interface plein écran, conçue pour être lisible à distance
sur une borne.

Composition indicative : - **Bas de l'écran :** roue ou bande
horizontale de jeux, avec logos. - **Zone centrale :** vidéo de gameplay
ou image de présentation du jeu sélectionné. - **Zone supérieure ou
latérale :** titre, année, éditeur, genre, plateforme et description. -
**Indicateurs facultatifs :** favoris, nombre de lancements, temps de
jeu.

La disposition doit être configurable par thème.

### 4.2 Roue horizontale

Fonctionnalité centrale du frontend : - logos transparents des jeux
affichés en bas de l'écran ; - défilement horizontal gauche/droite ; -
jeu sélectionné agrandi ou mis en évidence ; - jeux précédents et
suivants visibles ; - transitions fluides et rapides ; - nombre de logos
visibles configurable ; - vitesse, taille, espacement, position et
effets configurables ; - mode alternatif en bande horizontale simple,
sans effet de roue.

La navigation doit rester instantanée avec plusieurs centaines, voire
milliers de jeux. Les médias doivent être préchargés intelligemment et
les ressources inutilisées libérées.

### 4.3 Catégories et sous-catégories

Permettre de créer librement des catégories et sous-catégories, sans
structure imposée.

Exemple : - Tous les jeux - Course - Arcade - Simulation - Drift - Tir /
GunCab - Sport - Flipper - Air Hockey - Combat - Autres

Fonctions nécessaires : - créer, renommer, déplacer et supprimer des
catégories ; - créer plusieurs niveaux de sous-catégories ; - affecter
un jeu à plusieurs catégories ; - créer des catégories automatiques :
Favoris, Récemment joués, Tous les jeux ; - naviguer entre catégories et
jeux avec les mêmes commandes physiques.

## 5. Gestion de la bibliothèque de jeux

Chaque entrée de jeu doit pouvoir contenir : - nom et nom alternatif ; -
chemin du jeu ou du script de lancement ; - arguments éventuels ; -
répertoire de travail ; - catégorie(s) ; - logo ; - vidéo ; - image de
fond / fanart ; - captures d'écran ; - description ; - année ; - éditeur
; - genre ; - plateforme ; - informations facultatives de lancement.

Les champs de métadonnées et les médias doivent être facultatifs. Le nom
et une commande de lancement valide sont les seules informations
indispensables pour pouvoir lancer un jeu.

### Ajout et importation

Prévoir : - ajout manuel d'un jeu ; - scan d'un ou plusieurs répertoires
; - import d'une liste de jeux ; - détection des doublons ; -
association manuelle ou automatique des médias ; - modification,
désactivation et suppression d'une entrée ; - possibilité de déplacer
les jeux sans perdre leurs paramètres, avec vérification des chemins.

Un jeu ne doit pas être supprimé du disque lorsque l'utilisateur le
retire de la bibliothèque, sauf action explicite distincte.

## 6. Lancement des jeux et retour au frontend

La prise en charge des fichiers `.bat` est obligatoire.

Exemples de commandes : - `H:\Arcade\InitialD\run.bat` -
`H:\Arcade\CruisnBlast\launch.bat` - lancement direct d'un `.exe`

Pour chaque jeu, configurer : - fichier `.bat` ou `.exe` ; - arguments
; - répertoire de travail ; - mode d'exécution ; - délai de lancement
facultatif ; - scripts ou commandes avant et après le jeu, facultatifs.

Le frontend doit : 1. sauvegarder l'état de navigation avant le
lancement ; 2. lancer le script ou le programme dans le bon répertoire ;
3. éviter de rester au premier plan pendant le jeu ; 4. suivre
correctement le processus, y compris si un fichier BAT lance un autre
processus ; 5. détecter la fin réelle du jeu sans se baser uniquement
sur la fermeture du premier processus ; 6. reprendre automatiquement le
frontend ; 7. restaurer la catégorie et le jeu sélectionné ; 8. gérer
proprement les erreurs de lancement.

Prévoir des commandes avant/après lancement pour les besoins spécifiques
des bornes : changer un profil de volant, lancer un outil auxiliaire,
arrêter un service, appliquer une configuration ou restaurer un réglage.
Ces commandes doivent être facultatives et configurables par jeu.

Ne jamais tuer arbitrairement tous les processus d'un jeu ou du système.
La stratégie de supervision et d'arrêt doit être documentée.

## 7. Médias et récupération automatique des assets

Le logiciel doit pouvoir rechercher et télécharger automatiquement les
médias et métadonnées des jeux à partir de leur nom, avec possibilité de
corriger le résultat manuellement.

### Sources à étudier

-   ScreenScraper : https://www.screenscraper.fr/
-   SteamGridDB : https://www.steamgriddb.com/
-   API et documentation SteamGridDB :
    https://www.steamgriddb.com/api/v2
-   Documentation ScreenScraper : consulter la documentation officielle
    et les conditions d'accès/API sur le site.

L'agent doit vérifier les conditions d'utilisation, les limites, les
éventuels comptes requis et les modalités d'accès aux API avant de
développer l'intégration. Ne pas contourner les protections ou les
limites des services.

### Médias recherchés

Pour chaque jeu, essayer de récupérer : - **logo transparent / wheel
art** : priorité absolue pour la roue du bas ; - vidéo de gameplay ou
vidéo de démonstration ; - fanart / image de fond ; - capture d'écran
; - jaquette ou visuel alternatif ; - titre, année, éditeur, genre et
description.

Le choix des médias doit être pertinent pour une interface de borne. Un
logo transparent adapté à la roue est plus important qu'une jaquette
verticale.

### Cache et gestion locale

-   télécharger les médias à l'ajout du jeu ou sur demande ;
-   conserver les fichiers localement ;
-   ne jamais interroger Internet à chaque changement de sélection ;
-   détecter les médias déjà téléchargés ;
-   permettre de relancer une recherche ;
-   permettre de remplacer manuellement un fichier ;
-   afficher un visuel de remplacement si un média manque ;
-   permettre de désactiver la récupération automatique ;
-   ne jamais bloquer l'interface pendant une recherche ou un
    téléchargement.

Prévoir une interface de sélection lorsqu'un jeu possède plusieurs
résultats possibles. Ne pas associer silencieusement un mauvais jeu sur
la seule base d'un nom approximatif.

## 8. Lecture vidéo

Intégrer un lecteur adapté à une application native et léger, en
privilégiant l'accélération matérielle lorsqu'elle est disponible.

Fonctions : - lecture automatique de la vidéo du jeu sélectionné ; -
boucle facultative ; - volume et mode muet ; - activation/désactivation
globale ; - image de remplacement pendant le chargement ; -
préchargement limité et intelligent ; - arrêt de la vidéo précédente
lors du changement de jeu ; - gestion des fichiers manquants ou
illisibles.

Formats à prendre en charge selon la bibliothèque retenue : MP4/H.264 en
priorité, puis autres formats courants si cela ne pénalise pas le poids
et la fiabilité.

Ne pas décoder plusieurs vidéos simultanément sans nécessité. La vidéo
ne doit jamais bloquer la roue ou la réception des commandes.

## 9. Périphériques et compatibilité BackForceFeeder

La borne utilise BackForceFeeder et des périphériques de type volant,
boutons et contrôleurs USB. La compatibilité avec les entrées utilisées
par BackForceFeeder est une exigence importante.

Projet de référence : - GitHub BackForceFeeder :
https://github.com/Ultrawipf/OpenFFBoard - Site / documentation du
projet OpenFFBoard : https://github.com/Ultrawipf/OpenFFBoard

**Attention :** vérifier que le dépôt et les composants ciblés
correspondent bien à la version de BackForceFeeder utilisée. Ne pas
confondre le firmware, l'outil de configuration, les pilotes et les
interfaces d'entrée. L'agent doit examiner le dépôt réellement utilisé
par l'utilisateur avant de conclure à une compatibilité.

### Entrées à envisager

-   DirectInput ;
-   XInput ;
-   boutons USB ;
-   boutons du volant ;
-   contrôleurs supplémentaires ;
-   autres périphériques détectés par Windows.

L'agent doit identifier précisément comment BackForceFeeder expose les
commandes à Windows et choisir une méthode de lecture compatible. Ne pas
supposer qu'il faut communiquer directement avec le firmware si les
commandes sont déjà exposées comme contrôleur standard.

### Configuration des commandes

Permettre d'assigner : - jeu suivant / précédent ; - catégorie suivante
/ précédente ; - valider / lancer ; - retour ; - favoris ; - ouvrir le
menu ; - quitter le frontend ; - commandes facultatives de navigation.

Prévoir : - détection d'un bouton lors de l'assignation ; - affichage du
périphérique et de l'entrée détectée ; - gestion des appuis prolongés
; - délai avant répétition ; - vitesse de répétition ; - anti-rebond et
prévention des doubles validations ; - choix du périphérique à utiliser
; - possibilité de réinitialiser le mapping.

Pendant l'exécution d'un jeu, le frontend ne doit pas intercepter ou
perturber les commandes destinées au jeu. Après la fermeture du jeu, la
navigation doit redevenir disponible immédiatement.

## 10. Attract Mode / mode démonstration

Après une durée d'inactivité configurable, le frontend doit lancer
automatiquement un mode démonstration.

Comportements possibles : - défilement automatique des jeux ; -
sélection successive ou aléatoire ; - affichage du logo, des
informations et de la vidéo ; - transitions visuelles ; - lecture en
boucle des vidéos de présentation.

Paramètres : - délai d'inactivité avant activation ; - durée d'affichage
par jeu ; - ordre séquentiel ou aléatoire ; - catégories incluses ou
exclues ; - lecture vidéo activée ou non ; - vitesse de défilement ; -
volume ; - activation/désactivation globale.

**Interruption prioritaire :** à la moindre entrée utilisateur, le mode
démonstration s'arrête immédiatement et la navigation normale reprend.
Aucune animation ou transition ne doit retarder cette reprise.

L'Attract Mode ne doit pas lancer automatiquement les jeux réels sans
option explicite et avertissement.

## 11. Profils de borne et thèmes

Prévoir des profils indépendants, par exemple : - Racecab ; - GunCab ; -
Arcade classique ; - Flipper ; - Air Hockey.

Chaque profil peut définir : - catégories visibles ; - bibliothèque ou
sélection de jeux ; - mapping des commandes ; - thème ; - disposition de
l'interface ; - réglages de l'Attract Mode ; - paramètres vidéo.

Le profil actif doit pouvoir être choisi au démarrage ou dans les
paramètres.

Les thèmes doivent permettre de modifier l'apparence sans modifier le
code : - arrière-plan ; - couleurs ; - polices ; - disposition ; -
position et taille de la roue ; - nombre de logos visibles ; -
animations ; - affichage des informations.

## 12. Fonctions complémentaires recommandées

À intégrer après le socle principal : - favoris ; - jeux récemment
lancés ; - historique ; - compteur de lancements ; - temps de jeu ; -
recherche ; - tri et filtres ; - reprise sur le dernier jeu sélectionné
; - écran de lancement personnalisé ; - écran de retour au frontend ; -
démarrage automatique avec Windows ; - masquage du curseur ; - mode
plein écran/kiosque ; - combinaison de touches ou de boutons pour
quitter ; - sauvegarde et restauration de la configuration.

## 13. Configuration et administration

L'administration doit être accessible sans devoir éditer manuellement
des fichiers.

Prévoir un menu de configuration utilisable à la souris/clavier, même si
l'utilisation quotidienne se fait au volant et aux boutons.

Sections recommandées : 1. Bibliothèque et chemins des jeux ; 2.
catégories et profils ; 3. récupération des médias ; 4. configuration
des périphériques ; 5. commandes ; 6. affichage et thèmes ; 7. lecture
vidéo ; 8. Attract Mode ; 9. lancement et scripts ; 10.
sauvegarde/restauration ; 11. diagnostic et journaux.

Les paramètres doivent être stockés dans des fichiers lisibles et/ou une
base locale. Les chemins et médias doivent rester indépendants du
dossier d'installation du programme afin de faciliter les mises à jour.

## 14. Fiabilité, performances et mode hors ligne

Objectifs : - démarrage rapide ; - navigation sans latence ; - animation
fluide ; - consommation CPU/RAM limitée ; - aucun téléchargement en
arrière-plan non maîtrisé ; - aucune connexion Internet nécessaire pour
naviguer ou lancer les jeux ; - récupération propre après une erreur ; -
fonctionnement prolongé sans fuite mémoire.

Prévoir : - chargement asynchrone ; - cache d'images ; - préchargement
des logos voisins ; - libération des ressources inutilisées ; - limites
de mémoire pour les médias ; - journaux de diagnostic ; - gestion des
chemins invalides ; - sauvegarde automatique de la configuration ; -
tests de stabilité prolongés.

Ne pas fixer arbitrairement des objectifs chiffrés de consommation sans
les mesurer. Fournir des mesures réelles sur la machine cible et
identifier les postes les plus coûteux.

## 15. Plan de développement obligatoire

Développer par étapes, avec une version exécutable et testable à chaque
jalon.

### Phase 0 --- Étude et architecture

-   comparer les technologies ;
-   choisir la pile technique ;
-   proposer l'architecture ;
-   vérifier BackForceFeeder et les méthodes d'entrée ;
-   vérifier les API d'assets et leurs conditions ;
-   établir les dépendances et la stratégie de packaging.

**Livrable :** note d'architecture et plan de réalisation. Ne pas
commencer par générer toute l'application d'un seul coup.

### Phase 1 --- Prototype fonctionnel

-   fenêtre native plein écran ;
-   roue horizontale ;
-   logos locaux de test ;
-   navigation clavier et contrôleur ;
-   ajout manuel de jeux ;
-   catégories ;
-   lancement BAT/EXE ;
-   retour automatique au frontend.

**Livrable :** prototype réellement utilisable.

### Phase 2 --- Entrées et usage borne

-   détection des périphériques ;
-   configuration des commandes ;
-   tests avec BackForceFeeder ;
-   répétition et anti-rebond ;
-   absence d'interférence avec les jeux.

**Livrable :** utilisation complète sans clavier ni souris.

### Phase 3 --- Médias

-   métadonnées ;
-   téléchargement des logos ;
-   images de fond ;
-   vidéos ;
-   cache local ;
-   gestion des erreurs et des résultats ambigus.

**Livrable :** bibliothèque enrichie automatiquement.

### Phase 4 --- Attract Mode et profils

-   détection d'inactivité ;
-   navigation automatique ;
-   vidéos ;
-   profils de borne ;
-   thèmes.

**Livrable :** expérience de borne complète.

### Phase 5 --- Finitions et stabilité

-   favoris et historique ;
-   statistiques ;
-   configuration avancée ;
-   packaging ;
-   démarrage Windows ;
-   tests prolongés ;
-   documentation utilisateur.

## 16. Tests et critères d'acceptation

Le projet n'est accepté que si : - il s'agit bien d'un logiciel natif
Windows ; - la roue est fluide et réactive ; - les logos sont affichés
dans la roue ; - les catégories et sous-catégories fonctionnent ; -
plusieurs centaines de jeux peuvent être parcourus sans ralentissement
notable ; - les jeux BAT et EXE se lancent correctement ; - le frontend
revient après la fermeture réelle du jeu ; - les médias peuvent être
récupérés et conservés localement ; - l'application fonctionne hors
ligne après configuration ; - les commandes de la borne et de
BackForceFeeder sont testées réellement ; - l'Attract Mode s'interrompt
immédiatement à la moindre commande ; - les menus quotidiens sont
utilisables sans clavier ni souris ; - les erreurs sont journalisées et
compréhensibles ; - la consommation et la fluidité ont été mesurées.

## 17. Règles de travail pour l'agent IA

-   Examiner le projet et l'environnement avant toute modification.
-   Ne pas inventer de fichiers, d'API, de compatibilités ou de
    résultats de test.
-   Vérifier les documentations officielles et les dépôts avant
    d'intégrer une dépendance.
-   Justifier les choix techniques.
-   Procéder par petits changements compilables.
-   Compiler et tester après chaque étape importante.
-   Ne pas remplacer une fonctionnalité opérationnelle sans raison.
-   Garder une architecture modulaire.
-   Documenter les paramètres et les commandes de compilation.
-   Fournir régulièrement un exécutable testable.
-   Signaler clairement ce qui est terminé, partiel ou non testé.
-   Ne jamais annoncer une fonctionnalité comme terminée si elle n'a pas
    été testée.

------------------------------------------------------------------------

# 18. Références et liens utiles

## Sources de médias et métadonnées

-   ScreenScraper --- https://www.screenscraper.fr/
-   SteamGridDB --- https://www.steamgriddb.com/
-   API SteamGridDB --- https://www.steamgriddb.com/api/v2

## Périphériques / volant

-   OpenFFBoard / BackForceFeeder ---
    https://github.com/Ultrawipf/OpenFFBoard

## Technologies à évaluer

-   SDL --- https://www.libsdl.org/
-   SDL GitHub --- https://github.com/libsdl-org/SDL
-   Qt --- https://www.qt.io/
-   .NET --- https://dotnet.microsoft.com/
-   WPF --- https://learn.microsoft.com/dotnet/desktop/wpf/
-   SQLite --- https://www.sqlite.org/
-   FFmpeg --- https://ffmpeg.org/

Ces liens sont des pistes de travail, pas une obligation d'utiliser
toutes ces technologies. L'agent doit sélectionner uniquement les
dépendances nécessaires et vérifier leur licence, leur compatibilité
Windows, leur maintenance et leur coût en ressources.

------------------------------------------------------------------------

# 19. Première demande à exécuter par l'agent

Commence par analyser ce cahier des charges et produire une proposition
technique concrète.

Ta première réponse doit contenir : 1. la technologie recommandée et les
raisons du choix ; 2. les alternatives étudiées et leurs inconvénients ;
3. l'architecture logicielle proposée ; 4. la méthode précise de lecture
des commandes BackForceFeeder sous Windows ; 5. les sources d'assets et
les API réellement utilisables ; 6. la méthode de lancement/supervision
des BAT et EXE ; 7. l'organisation des dossiers et des configurations ;
8. les étapes de développement et les tests associés ; 9. les risques
techniques identifiés.

Ne commence pas à coder avant d'avoir présenté cette analyse. Le but est
de construire un frontend arcade natif, durable, léger et agréable à
utiliser, pas de produire rapidement une maquette visuelle.
