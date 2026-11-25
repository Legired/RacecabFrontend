# Synchronisation des bornes avec une base sur le PC

Mise en place locale (un PC, plusieurs bornes) : la « base » `H:\Bornes` a la même arborescence que le `C:\`
des bornes (`Racecab\emulateur`, `Racecab\jeux`…, idem pour `GunCab`). Sur une borne, `Synchroniser.bat`
lance `\<PC>\Bornes\outils\Synchroniser.ps1` :

1. **Fichiers** : robocopy en deux passes, sans jamais rien supprimer.
   - passe 1 : nouveaux fichiers et fichiers modifiés (`/XO` : jamais un fichier plus récent sur la borne),
     **hors fichiers protégés** (`outils\proteges.txt` : `*.ini`, `controls*`, sauvegardes, cartes, AMFS…) ;
   - passe 2 : fichiers manquants seulement (`/XC /XN /XO`), protégés compris.
2. **Programme du frontend** : depuis `outilsrontend`, jamais vers une version plus ancienne, données exclues.
3. **Bibliothèque** : copie locale de `<base>rontend\data` (db + media), puis
   `RacecabFrontend.exe --sync-from <copie> --sync-root H:\Bornes\Racecab --sync-to C:` (`share::SyncFrom` :
   export de la base au format de partage puis import, profils exclus, chemins et arguments remappés).

Le type de borne vient de `cabMode` (réglages du frontend de la borne). Mode essai : `Synchroniser.bat /essai`.
Mise en place réseau : `outils\Partager la base (admin).ps1` sur le PC (compte local « borne », partage en
lecture seule, pare-feu réseau privé), puis `Memoriser l'acces (sur la borne).bat` sur chaque borne.
Testé sur une base et une borne de test : mises à jour, réglages protégés gardés, sauvegardes gardées,
fichiers propres à la borne intacts, frontend mis à jour, jeu ajouté avec ses chemins en C: et son logo,
pas de doublon à la seconde synchro. Pas encore testé à travers le réseau, depuis une vraie borne.
