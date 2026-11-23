# Format d'import d'une liste de jeux

Fichier JSON : soit un tableau, soit un objet `{"games": [...]}`. Seuls `name` et `launch` sont obligatoires.
Les chemins relatifs sont résolus par rapport au dossier du fichier JSON.

```json
{
  "games": [
    {
      "name": "Initial D Arcade Stage",
      "launch": "H:/Arcade/InitialD/run.bat",
      "args": "",
      "workdir": "",
      "categories": ["Course/Arcade", "Course/Drift"],
      "year": 2002,
      "publisher": "Sega",
      "genre": "Course",
      "platform": "Arcade",
      "description": "…",
      "logo": "H:/Arcade/InitialD/media/logo.png",
      "fanart": "H:/Arcade/InitialD/media/fanart.jpg",
      "video": "",
      "preCmd": "",
      "postCmd": "",
      "watchProcess": "",
      "favorite": false
    }
  ]
}
```

- `runMode` : `auto` (par défaut : console d'un .bat réduite, .exe normal), `normal`, `minimized`, `hidden`.
  `normal` est conseillé pour un .bat qui peut afficher une erreur suivie de `pause` : avec une console réduite ou masquée, la borne attendrait sans rien montrer.
- `categories` : chemins séparés par `/`. Les catégories manquantes sont créées.
- **Doublons** : un jeu dont le fichier de lancement *et* les arguments existent déjà est ignoré, puis compté dans le compte rendu.
- Les fichiers des jeux ne sont jamais copiés, déplacés ni modifiés.
