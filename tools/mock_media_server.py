"""Serveur local qui simule SteamGridDB et ScreenScraper pour tester la récupération des médias sans compte.

    python tools/mock_media_server.py [port] [video.mp4]

Écoute uniquement sur 127.0.0.1. Formats de réponse :
  - SteamGridDB : d'après le client officiel node-steamgriddb (success/data, url, score, style...).
  - ScreenScraper : d'après Skyscraper (response.jeux / response.jeu, noms[{region,text}], medias[{type,region,url,format}]).
  - LaunchBox : /launchbox/Metadata.zip (même structure que le vrai fichier : Metadata.xml avec <Game>,
    <GameAlternateName>, <GameImage>) et images sous /lbimages/<fichier>.
Identifiants acceptés : clé SteamGridDB « test-key », devid/devpassword ScreenScraper « test » / « test ».
ScreenScraper : le paramètre systemeid filtre les résultats (Rambo existe en Arcade 75 et sur Master System 2).
"""
import io
import json
import sys
import urllib.parse
import zipfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from PIL import Image, ImageDraw, ImageFont

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
VIDEO = sys.argv[2] if len(sys.argv) > 2 else None
BASE = f"http://127.0.0.1:{PORT}"

GAMES = {  # id -> (nom, année, système)
    "101": ("Initial D Arcade Stage 8 Infinity", 2014, "75"),
    "102": ("Initial D Arcade Stage 8", 2014, "75"),
    "201": ("Mario Kart Arcade GP DX", 2013, "75"),
    "202": ("Mario Kart Arcade GP 2", 2007, "75"),
    "301": ("Rambo", 1987, "2"),
    "302": ("Rambo", 2008, "75"),
}
SYSTEMS = {"75": "Arcade", "2": "Master System", "269": "TeknoParrot"}

LB_XML = """<?xml version="1.0" standalone="yes"?>
<LaunchBox>
  <Game>
    <Name>Initial D4</Name>
    <VideoURL>https://www.youtube.com/watch?v=test</VideoURL>
    <ReleaseDate>2007-02-21</ReleaseDate>
    <DatabaseID>1</DatabaseID>
    <Platform>Arcade</Platform>
    <Genres>Racing</Genres>
    <Developer>Sega</Developer>
    <Publisher>Sega</Publisher>
  </Game>
  <Game>
    <Name>Rambo</Name>
    <ReleaseYear>2008</ReleaseYear>
    <Overview>Overview &amp; test &#233;t&#xE9;</Overview>
    <DatabaseID>2</DatabaseID>
    <Platform>Arcade</Platform>
    <Genres>Shooter; Light Gun</Genres>
    <Genres2 />
    <Publisher>Sega</Publisher>
  </Game>
  <Game>
    <Name>Rambo</Name>
    <ReleaseYear>1987</ReleaseYear>
    <DatabaseID>3</DatabaseID>
    <Platform>Sega Master System</Platform>
  </Game>
  <Game>
    <Name>Rambo III</Name>
    <DatabaseID>4</DatabaseID>
    <Platform>Arcade</Platform>
  </Game>
  <GameAlternateName>
    <AlternateName>Initial D Arcade Stage 4</AlternateName>
    <DatabaseID>1</DatabaseID>
    <Region>Japan</Region>
  </GameAlternateName>
  <GameAlternateName>
    <AlternateName>Rambo Master</AlternateName>
    <DatabaseID>3</DatabaseID>
  </GameAlternateName>
  <GameImage>
    <DatabaseID>1</DatabaseID>
    <FileName>lb_logo_1.png</FileName>
    <Type>Clear Logo</Type>
    <Region>Japan</Region>
  </GameImage>
  <GameImage>
    <DatabaseID>1</DatabaseID>
    <FileName>lb_box_1.png</FileName>
    <Type>Box - Front</Type>
  </GameImage>
  <GameImage>
    <DatabaseID>1</DatabaseID>
    <FileName>lb_fan_1.jpg</FileName>
    <Type>Fanart - Background</Type>
  </GameImage>
  <GameImage>
    <DatabaseID>1</DatabaseID>
    <FileName>lb_ss_1.jpg</FileName>
    <Type>Screenshot - Gameplay</Type>
  </GameImage>
  <GameImage>
    <DatabaseID>2</DatabaseID>
    <FileName>lb_logo_2.png</FileName>
    <Type>Clear Logo</Type>
  </GameImage>
  <GameImage>
    <DatabaseID>3</DatabaseID>
    <FileName>lb_logo_3.png</FileName>
    <Type>Clear Logo</Type>
  </GameImage>
</LaunchBox>
"""


def lb_zip():
    b = io.BytesIO()
    with zipfile.ZipFile(b, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("Metadata.xml", LB_XML)
        z.writestr("Platforms.xml", "<LaunchBox />")
    return b.getvalue()


def png_logo(text):
    img = Image.new("RGBA", (600, 220), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    f = ImageFont.truetype("C:/Windows/Fonts/impact.ttf", 64)
    d.text((300, 110), text, font=f, fill=(255, 210, 40), anchor="mm", stroke_width=6, stroke_fill=(20, 20, 20))
    b = io.BytesIO()
    img.save(b, "PNG")
    return b.getvalue()


def jpg_fanart(seed):
    img = Image.new("RGB", (1280, 720), (20 + seed * 10 % 80, 30, 70))
    d = ImageDraw.Draw(img)
    for i in range(0, 1280, 80):
        d.line([(i, 720), (640, 300)], fill=(200, 120, 40), width=3)
    b = io.BytesIO()
    img.save(b, "JPEG", quality=80)
    return b.getvalue()


def matches(q, system=None):
    words = q.lower().split()[:2]
    return [gid for gid, (n, _, s) in GAMES.items()
            if all(w in n.lower().split() for w in words) and (not system or s == system)]


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def send(self, code, body, ctype="application/json"):
        if isinstance(body, (dict, list)):
            body = json.dumps(body).encode()
        elif isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        qs = dict(urllib.parse.parse_qsl(u.query))
        p = urllib.parse.unquote(u.path)

        # ---------------- LaunchBox
        if p == "/launchbox/Metadata.zip":
            return self.send(200, lb_zip(), "application/zip")
        if p.startswith("/lbimages/"):
            f = p.split("/")[-1]
            if f.endswith(".png"):
                return self.send(200, png_logo("LB " + f.split("_")[-1].split(".")[0]), "image/png")
            return self.send(200, jpg_fanart(3), "image/jpeg")

        # ---------------- fichiers
        if p.startswith("/files/logo_"):
            gid = p.split("_")[1].split(".")[0]
            return self.send(200, png_logo(GAMES[gid][0].split(" Arcade")[0].upper()), "image/png")
        if p.startswith("/files/fanart_"):
            return self.send(200, jpg_fanart(int(p.split("_")[1].split(".")[0])), "image/jpeg")
        if p == "/files/video.mp4":
            if not VIDEO:
                return self.send(200, "NOMEDIA", "text/plain")
            return self.send(200, open(VIDEO, "rb").read(), "video/mp4")

        # ---------------- SteamGridDB
        if p.startswith("/api/v2/"):
            if self.headers.get("Authorization") != "Bearer test-key":
                return self.send(401, {"success": False, "errors": ["Authentication Required"]})
            if p.startswith("/api/v2/search/autocomplete/"):
                q = p.rsplit("/", 1)[1]
                data = [{"id": int(g), "name": GAMES[g][0], "types": ["steam"], "verified": True,
                         "release_date": (GAMES[g][1] - 1970) * 31557600} for g in matches(q)]
                return self.send(200, {"success": True, "data": data})
            kind, gid = p.split("/")[3], p.split("/")[5]
            if kind == "logos":
                return self.send(200, {"success": True, "data": [
                    {"id": 2, "score": 1, "style": ["white"], "url": f"{BASE}/files/logo_{gid}.png", "width": 600, "height": 220},
                    {"id": 1, "score": 0, "style": ["official"], "url": f"{BASE}/files/logo_{gid}.png", "width": 600, "height": 220}]})
            if kind == "heroes":
                return self.send(200, {"success": True, "data": [
                    {"id": 3, "score": 5, "style": ["alternate"], "url": f"{BASE}/files/fanart_{gid}.jpg", "width": 1280, "height": 720}]})
            return self.send(404, {"success": False, "errors": ["Not found"]})

        # ---------------- ScreenScraper
        if p.startswith("/api2/"):
            if qs.get("devid") != "test" or qs.get("devpassword") != "test":
                return self.send(200, "Erreur de login : Vérifier vos identifiants développeur !  ", "text/html")

            def jeu(gid, full):
                name, year, system = GAMES[gid]
                j = {"id": gid, "noms": [{"region": "ss", "text": name + " (ss)"}, {"region": "wor", "text": name}],
                     "systeme": {"id": system, "text": SYSTEMS[system]}, "dates": [{"region": "jp", "text": f"{year}-07-01"}]}
                if full:
                    j.update({
                        "editeur": {"id": "3", "text": "Sega" if gid.startswith("1") else "Namco"},
                        "synopsis": [{"langue": "en", "text": "English synopsis."}, {"langue": "fr", "text": f"Synopsis de {name}."}],
                        "genres": [{"id": "28", "noms": [{"langue": "en", "text": "Race"}, {"langue": "fr", "text": "Course"}]}],
                        "medias": [
                            {"type": "wheel", "region": "jp", "url": f"{BASE}/files/logo_{gid}.png", "format": "png"},
                            {"type": "wheel-hd", "region": "wor", "url": f"{BASE}/files/logo_{gid}.png", "format": "png"},
                            {"type": "fanart", "url": f"{BASE}/files/fanart_{gid}.jpg", "format": "jpg"},
                            {"type": "ss", "region": "wor", "url": f"{BASE}/files/fanart_{gid}.jpg", "format": "jpg"},
                            {"type": "video-normalized", "url": f"{BASE}/files/video.mp4", "format": "mp4"},
                        ]})
                return j

            if p == "/api2/jeuRecherche.php":
                found = matches(qs.get("recherche", ""), qs.get("systemeid"))
                return self.send(200, {"header": {}, "response": {"jeux": [jeu(g, False) for g in found] or [{}]}})
            if p == "/api2/jeuInfos.php":
                gid = qs.get("gameid", "")
                if gid not in GAMES:
                    return self.send(404, "Erreur : Jeu non trouvée !", "text/html")
                return self.send(200, {"header": {}, "response": {"jeu": jeu(gid, True)}})
        self.send(404, "not found", "text/plain")


print(f"Serveur de test sur {BASE}", flush=True)
ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
