"""Génère une bibliothèque de test pour Racecab Frontend.

    python tools/make_test_library.py [dossier] [nombre_de_jeux]

Crée dans <dossier> (par défaut testdata) :
  - fake_games/   : vrais scripts .bat de test (aucun jeu réel n'est nécessaire)
      ChildProcess/run.bat : le .bat se termine immédiatement mais lance une fenêtre enfant ;
                             le frontend doit attendre la fermeture de cette fenêtre (test du Job Object)
      Timed5s/run.bat      : console qui se ferme seule après 5 s (test du retour automatique)
  - logos/, fanart/ : logos PNG transparents et fonds générés (Pillow)
  - games.json    : liste à importer (option --import ou bouton « Importer une liste »)
  - data/config/settings.json : mode fenêtré 1280x720 pour les tests

Nécessite Pillow (pip install pillow).
"""
import json
import random
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

out = Path(sys.argv[1] if len(sys.argv) > 1 else "testdata").resolve()
count = int(sys.argv[2]) if len(sys.argv) > 2 else 500
random.seed(42)

games_dir = out / "fake_games"
logos_dir = out / "logos"
fanart_dir = out / "fanart"
for d in (games_dir / "ChildProcess", games_dir / "Timed5s", logos_dir, fanart_dir, out / "data" / "config"):
    d.mkdir(parents=True, exist_ok=True)

# --- Scripts de test --------------------------------------------------------------------------
(games_dir / "ChildProcess" / "run.bat").write_text(
    "@echo off\r\n"
    "rem Le script se termine tout de suite ; la fenetre enfant represente le vrai jeu.\r\n"
    'start "" powershell -NoProfile -WindowStyle Hidden -Command "Add-Type -AssemblyName System.Windows.Forms; '
    "[System.Windows.Forms.MessageBox]::Show('Jeu de test : fermez cette fenetre pour revenir au frontend.', 'Racecab - test')\"\r\n"
    "exit /b 0\r\n",
    encoding="ascii",
)
(games_dir / "Timed5s" / "run.bat").write_text(
    "@echo off\r\n"
    "echo Jeu de test %* : fermeture automatique dans 5 secondes...\r\n"
    "ping -n 6 127.0.0.1 >nul\r\n"
    "exit /b 0\r\n",
    encoding="ascii",
)

# --- Images ------------------------------------------------------------------------------------
FONTS = ["C:/Windows/Fonts/impact.ttf", "C:/Windows/Fonts/arialbd.ttf", "C:/Windows/Fonts/segoeuib.ttf"]
PALETTE = [(255, 170, 40), (60, 200, 255), (255, 70, 90), (120, 255, 120), (255, 230, 60), (200, 120, 255)]


def make_logo(text: str, path: Path):
    img = Image.new("RGBA", (640, 260), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    font_path = random.choice(FONTS)
    size = 120
    while size > 30:
        font = ImageFont.truetype(font_path, size)
        box = d.multiline_textbbox((0, 0), text, font=font, align="center", stroke_width=8)
        if box[2] - box[0] < 600 and box[3] - box[1] < 230:
            break
        size -= 6
    w, h = box[2] - box[0], box[3] - box[1]
    pos = ((640 - w) / 2 - box[0], (260 - h) / 2 - box[1])
    color = random.choice(PALETTE)
    d.multiline_text(pos, text, font=font, fill=color, align="center", stroke_width=8, stroke_fill=(10, 10, 20))
    img.save(path)


def make_fanart(seed: int, path: Path):
    r = random.Random(seed)
    a = tuple(r.randint(10, 90) for _ in range(3))
    b = tuple(r.randint(60, 200) for _ in range(3))
    img = Image.new("RGB", (1280, 720))
    d = ImageDraw.Draw(img)
    for y in range(720):
        t = y / 719
        d.line([(0, y), (1280, y)], fill=tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3)))
    for _ in range(25):  # « piste » stylisée
        x = r.randint(-200, 1280)
        d.polygon([(x, 720), (x + 120, 720), (640, 330), (630, 330)], fill=tuple(min(255, c + 30) for c in b))
    img.save(path, quality=85)


# --- Liste de jeux -----------------------------------------------------------------------------
CATS = ["Course/Arcade", "Course/Simulation", "Course/Drift", "Tir - GunCab", "Sport", "Flipper", "Air Hockey",
        "Combat", "Autres"]
WORDS = ["Turbo", "Night", "Drift", "Thunder", "Neon", "Road", "Blaze", "Rally", "Street", "Storm", "Velocity",
         "Apex", "Outrun", "Nitro", "Midnight", "Circuit", "Highway", "Inferno", "Rocket", "Shadow"]
SUFFIX = ["Racer", "Rush", "Fever", "Legends", "Masters", "Chase", "Kings", "Spirit", "Force", "Zone"]
PUBLISHERS = ["Studio Alpha", "Arcade Works", "Neon Bits", "RetroForge", "Pixel Garage"]

games = [
    {"name": "Test enfant (fermer la fenêtre)", "launch": "fake_games/ChildProcess/run.bat",
     "categories": ["Course/Arcade"], "year": 2026, "publisher": "Racecab", "genre": "Test",
     "description": "Le script .bat se termine immédiatement et lance une fenêtre enfant. "
                    "Le frontend ne doit revenir qu'à la fermeture de cette fenêtre."},
    {"name": "Test 5 secondes", "launch": "fake_games/Timed5s/run.bat", "args": "special",
     "categories": ["Course/Arcade"], "year": 2026, "publisher": "Racecab", "genre": "Test",
     "description": "Console qui se ferme seule après 5 secondes : le frontend doit revenir automatiquement."},
    {"name": "Test exe direct (Table des caractères)", "launch": "C:/Windows/System32/charmap.exe",
     "categories": ["Autres"], "genre": "Test", "description": "Lancement direct d'un .exe Win32."},
    {"name": "Test fichier manquant", "launch": "fake_games/Inexistant/run.bat", "categories": ["Autres"],
     "genre": "Test", "description": "Doit afficher une erreur lisible sans bloquer le frontend."},
]
for g in games[:2]:
    slug = "special_" + str(games.index(g))
    make_logo(g["name"].split(" (")[0], logos_dir / f"{slug}.png")
    g["logo"] = f"logos/{slug}.png"

used = set()
for i in range(count):
    while True:
        name = f"{random.choice(WORDS)} {random.choice(WORDS) if random.random() < 0.3 else ''} {random.choice(SUFFIX)}"
        name = " ".join(name.split())
        if random.random() < 0.3:
            name += f" {random.randint(2, 5)}"
        if name not in used:
            used.add(name)
            break
    g = {
        "name": name,
        "launch": "fake_games/Timed5s/run.bat",
        "args": f"--id {i}",  # arguments différents : pas des doublons
        "categories": random.sample(CATS, random.choice([1, 1, 2])),
        "year": random.randint(1986, 2024),
        "publisher": random.choice(PUBLISHERS),
        "genre": "Course" if i % 3 else "Arcade",
        "platform": "PC",
        "description": f"Jeu fictif n°{i + 1} généré pour tester la fluidité de la roue avec une grande bibliothèque. "
                       "Les médias manquants sont remplacés par le nom du jeu.",
    }
    if i < 80:  # une partie avec logo, le reste teste le visuel de remplacement
        make_logo(name.upper().replace(" ", "\n", 1) if len(name) > 14 else name.upper(), logos_dir / f"g{i}.png")
        g["logo"] = f"logos/g{i}.png"
    if i < 20:
        make_fanart(i, fanart_dir / f"g{i}.jpg")
        g["fanart"] = f"fanart/g{i}.jpg"
    games.append(g)

(out / "games.json").write_text(json.dumps({"games": games}, ensure_ascii=False, indent=1), encoding="utf-8")
settings = out / "data" / "config" / "settings.json"
if not settings.exists():
    settings.write_text(json.dumps({"fullscreen": False, "windowW": 1280, "windowH": 720}, indent=2), encoding="utf-8")
print(f"{len(games)} jeux -> {out / 'games.json'}")
