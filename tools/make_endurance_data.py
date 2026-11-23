"""Prépare un dossier de données pour le test d'endurance : 504 jeux, logos, fonds et vidéos de test,
démonstration accélérée (5 s d'inactivité, 3 s par jeu, vidéo activée).

    python -I tools/make_endurance_data.py testdata/data testdata/endurance testdata/test_video.mp4
"""
import json
import os
import shutil
import sqlite3
import sys

src, dst, video = sys.argv[1], sys.argv[2], sys.argv[3]
if os.path.exists(dst):
    sys.exit(f"{dst} existe déjà : choisissez un autre dossier")
for d in ("config", "db", "themes"):
    if os.path.exists(os.path.join(src, d)):
        shutil.copytree(os.path.join(src, d), os.path.join(dst, d))
fanarts = sorted(os.listdir(os.path.join(os.path.dirname(src), "fanart")))
db = sqlite3.connect(os.path.join(dst, "db", "library.sqlite"))
ids = [r[0] for r in db.execute("SELECT id FROM games ORDER BY id")]
db.close()
for n, gid in enumerate(ids):
    m = os.path.join(dst, "media", str(gid))
    if n % 4 == 0:  # un jeu sur quatre : vidéo
        os.makedirs(m, exist_ok=True)
        shutil.copyfile(video, os.path.join(m, "video.mp4"))
    if fanarts and n % 3 == 0:  # un sur trois : fond
        os.makedirs(m, exist_ok=True)
        f = fanarts[n % len(fanarts)]
        shutil.copyfile(os.path.join(os.path.dirname(src), "fanart", f), os.path.join(m, "fanart" + os.path.splitext(f)[1]))
os.makedirs(os.path.join(dst, "config", "profiles"), exist_ok=True)
profile = {"theme": "default", "mode": "racecab",
           "video": {"enabled": True, "loop": True, "volume": 0.0, "mute": True, "delayMs": 400, "hwDecode": True},
           "attract": {"enabled": True, "idleSeconds": 5, "displaySeconds": 3, "random": True, "video": True,
                       "volume": 0.0, "mute": True, "speed": 0.6}}
json.dump(profile, open(os.path.join(dst, "config", "profiles", "Racecab.json"), "w"), indent=2)
s = json.load(open(os.path.join(dst, "config", "settings.json"), encoding="utf-8"))
s.update({"fullscreen": False, "windowW": 1280, "windowH": 720, "profile": "Racecab", "cabMode": "racecab",
          "autoBackup": False, "askProfileAtStartup": False})
json.dump(s, open(os.path.join(dst, "config", "settings.json"), "w"), indent=2)
print(f"{len(ids)} jeux, {len(ids[::4])} vidéos, fonds pour {len(ids[::3])} jeux -> {dst}")
