# Commandes, volant et BackForceFeeder

## Comment le frontend lit les commandes
Via SDL3, sans pilote spécifique ni dialogue avec le firmware :
- **clavier** (toujours actif : c'est la commande de secours) ;
- **manettes reconnues** (XInput / Xbox, etc.) : boutons « manette » ;
- **joysticks génériques** (DirectInput / HID) : boutons, axes, chapeaux. BackForceFeeder présente les commandes de la borne à Windows sous la forme d'un joystick virtuel **vJoy**. Le frontend le lit donc comme n'importe quel joystick, sous le nom « vJoy Device ».

Pendant un jeu, le frontend ne lit aucune commande : SDL est configuré pour ignorer les manettes quand la fenêtre n'a pas le focus, et la navigation est coupée. Après le retour, les commandes sont ignorées pendant 800 ms (réglable) pour qu'un bouton encore enfoncé ne relance rien.

## Actions
| Action | Clavier par défaut | Remarque |
|---|---|---|
| Jeu précédent / suivant | ← / → | répétition puis accélération si maintenu |
| Catégorie précédente / suivante | ↑ / ↓ | |
| Valider / lancer | Entrée | anti double validation (700 ms) |
| Retour | Échap | appui court : remonter. **Appui long (1,2 s) : menu de la borne** |
| Favori | F | |
| Menu de la borne | Tab | |
| Quitter le frontend | F10 | avec confirmation |
| Administration | F1 | clavier uniquement (souris / clavier) |

Par défaut, sur un joystick générique : chapeau et axe 0 (volant) pour naviguer, bouton 0 = valider, 1 = retour, 2 = favori, 3 = menu.

## Configurer au volant (sans clavier ni souris)
1. Ouvrir le **menu de la borne** : bouton *Menu*, ou **maintenir Retour**.
2. Choisir **Configurer les commandes (assistant)**.
3. Pour chaque action affichée, appuyer sur le bouton, tourner le volant ou enfoncer la pédale voulus.
   - Sans appui pendant 10 s, la commande actuelle est conservée (action suivante).
   - Une même commande ne peut pas servir à deux actions : un message demande d'en choisir une autre.
   - Pour un axe, la position de repos est mesurée au début (ex. une pédale au repos à -1 n'est pas prise pour un appui).
4. Le nouveau mapping est appliqué à la fin, en une seule fois, et enregistré dans `config\input.json`.
   **Échap** au clavier annule tout et garde l'ancien mapping.

Une commande détectée remplace uniquement les associations de la même famille pour cette action : clavier, manette reconnue ou joystick générique. Elle est liée au périphérique détecté (ex. « vJoy Device »). Le clavier reste donc toujours utilisable en secours.

## Configurer à la souris
Administration (F1), onglet **Commandes** : liste des périphériques et dernière entrée détectée, réglages (répétition, accélération, anti-rebond, appui long, garde après un jeu, seuil des axes), et bouton **Ajouter par détection** pour chaque action. La croix retire une association. La case *Lier au périphérique détecté* limite la nouvelle association au périphérique qui l'a produite.

## Ce qui reste à vérifier sur la borne
- Le nom exact sous lequel vJoy / BackForceFeeder apparaît, et la numérotation de ses boutons (onglet Commandes, ligne « Dernière entrée détectée »).
- Le volant comme commande gauche/droite : selon la sensibilité, un léger décentrage au repos pourrait faire défiler. Monter alors le seuil des axes, ou réserver la navigation aux boutons.
