# Lancement et supervision des jeux

Code : `src/launcher/Launcher.cpp`. Toute la séquence tourne dans un thread dédié : l'interface ne bloque jamais.

## Séquence
1. Sauvegarde de l'état de navigation (`config\state.json`).
2. Vérification du fichier et du répertoire de travail. En cas d'erreur, un message s'affiche à l'écran et une ligne est écrite dans le journal.
3. **Commande avant** (facultative) via `cmd.exe /d /c`, fenêtre masquée, attendue jusqu'à 60 s.
4. **Délai** facultatif.
5. Création d'un **Job Object** relié à un port de complétion.
6. `CreateProcess` **suspendu** :
   - `.bat` / `.cmd` → `cmd.exe /d /c ""script" arguments"` ;
   - `.exe` → lancement direct.
   Le répertoire de travail est celui du fichier lancé, sauf réglage contraire.
   Mode d'exécution : *Automatique* (console du .bat masquée, .exe normal), *Normal*, *Réduit*, *Masqué*. La console masquée existe toujours : les programmes console lancés par le script fonctionnent. Un script qui attend une saisie (`pause`) doit passer en *Normal*.
7. Le processus est attaché au Job, puis relancé. Tous ses descendants héritent du Job.
8. **Écran de chargement** : le frontend passe au-dessus de toutes les fenêtres (« Chargement du jeu… ») tant que la vraie fenêtre du jeu n'est pas là. Une console ou un lanceur intermédiaire ne se voient donc jamais. Vraie fenêtre du jeu : fenêtre visible d'au moins 320 × 240, ni console (conhost, Windows Terminal) ni fenêtre outil, appartenant :
   - à un processus du Job ou au *processus à surveiller* ;
   - à défaut, à n'importe quel programme, si elle passe au premier plan après le lancement.
   Fond de l'écran : la vidéo du jeu si elle existe (elle continue sans redémarrer si elle tournait déjà, son baissé de moitié), sinon son fond.
   Dès qu'elle est repérée (la vidéo s'arrête aussitôt) (contrôle toutes les 150 ms), le frontend lui cède le premier plan, se réduit, arrête son rendu et ignore toutes les commandes. Sans fenêtre repérée, il s'efface après 60 s (Administration > Système > *Écran de chargement : durée maximale*). Retour pendant le chargement l'efface tout de suite. SDL est configuré pour ne lire aucune manette quand la fenêtre n'a pas le focus.
9. **Fin réelle** = notification `JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO` : plus aucun processus du jeu n'est en vie.
10. **Processus à surveiller** (facultatif) : pour un lanceur qui démarre le jeu hors de son arborescence (service, broker), le frontend attend en plus que ce processus apparaisse (30 s maximum), puis qu'il disparaisse.
11. **Commande après** (facultative), attendue jusqu'à 60 s.
12. Restauration de la fenêtre au premier plan, de la catégorie et du jeu sélectionné, puis mise à jour des statistiques.

## Politique d'arrêt
- Le frontend **ne termine jamais aucun processus**, ni du jeu ni du système.
- Le Job est créé **sans** `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` : même si le frontend plante ou est fermé, le jeu continue.
- Une commande « quitter le jeu » (fermeture propre, puis arrêt limité au Job de ce jeu) pourra être ajoutée plus tard sur demande explicite. Elle n'existe pas aujourd'hui.

## Limites connues
- Un programme lancé par un service Windows, ou par une application déjà ouverte avant le jeu (Steam, par exemple), ne fait pas partie du Job. Dans ce cas, utilisez l'option *Processus à surveiller*.
- Les applications du Microsoft Store (par exemple le Bloc-notes de Windows 11) délèguent leur lancement à un autre processus. Ce sont des cas typiques pour *Processus à surveiller*.
