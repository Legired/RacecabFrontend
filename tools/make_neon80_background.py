"""Génère le fond du thème « neon80 » (style synthwave années 80) : ciel violet étoilé, soleil rayé,
montagnes et grille en perspective. Usage : python tools/make_neon80_background.py <fichier.png> [largeur hauteur]"""
import random
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

out = sys.argv[1]
W = int(sys.argv[2]) if len(sys.argv) > 2 else 1920
H = int(sys.argv[3]) if len(sys.argv) > 3 else 1080
HOR = int(H * 0.64)  # ligne d'horizon
rnd = random.Random(1984)


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def vgrad(img, y0, y1, stops):
    d = ImageDraw.Draw(img)
    for y in range(y0, y1):
        t = (y - y0) / max(1, y1 - y0 - 1)
        for k in range(len(stops) - 1):
            (p0, c0), (p1, c1) = stops[k], stops[k + 1]
            if p0 <= t <= p1:
                d.line([(0, y), (W, y)], fill=lerp(c0, c1, (t - p0) / max(1e-6, p1 - p0)))
                break


img = Image.new("RGB", (W, H))
# Ciel : nuit violette vers un horizon rose
vgrad(img, 0, HOR, [(0.0, (6, 2, 20)), (0.55, (34, 6, 62)), (0.85, (92, 14, 96)), (1.0, (190, 30, 120))])
# Sol : violet très sombre
vgrad(img, HOR, H, [(0.0, (30, 4, 48)), (0.25, (14, 2, 28)), (1.0, (4, 0, 10))])

# Étoiles
d = ImageDraw.Draw(img)
for _ in range(int(W * H / 4500)):
    x, y = rnd.randrange(W), rnd.randrange(int(HOR * 0.85))
    b = rnd.randint(90, 255)
    r = 1 if rnd.random() < 0.9 else 2
    d.ellipse([x - r / 2, y - r / 2, x + r / 2, y + r / 2], fill=(b, b, min(255, b + 30)))

# Soleil rayé (dégradé jaune -> rose -> magenta), bandes découpées dans la moitié basse
R = int(H * 0.26)
cx, cy = W // 2, HOR - int(R * 0.18)
sun = Image.new("RGB", (W, H))
vgrad(sun, cy - R, cy + R, [(0.0, (255, 225, 60)), (0.45, (255, 110, 90)), (0.75, (255, 40, 140)), (1.0, (200, 20, 170))])
mask = Image.new("L", (W, H), 0)
md = ImageDraw.Draw(mask)
md.ellipse([cx - R, cy - R, cx + R, cy + R], fill=255)
y, gap, band = cy - int(R * 0.5), 3.0, 22.0
while y < cy + R:
    md.rectangle([0, int(y), W, int(y + gap)], fill=0)
    y += gap + band
    gap *= 1.3
    band *= 0.85
md.rectangle([0, HOR, W, H], fill=0)  # le soleil se couche derrière l'horizon
glow = mask.filter(ImageFilter.GaussianBlur(R * 0.25))
img = Image.composite(Image.new("RGB", (W, H), (255, 60, 150)), img, glow.point(lambda v: int(v * 0.55)))
img = Image.composite(sun, img, mask)

# Montagnes en silhouette, crêtes cyan
d = ImageDraw.Draw(img)
for layer, (col, edge, hmax) in enumerate([((40, 6, 70), (90, 40, 160), 0.16), ((20, 2, 40), (0, 220, 255), 0.10)]):
    pts, x = [(0, HOR)], 0
    while x < W:
        x += rnd.randint(int(W * 0.03), int(W * 0.08))
        dist = abs(x - cx) / (W / 2)  # vallée au centre, sous le soleil
        h = H * hmax * (0.25 + 0.75 * dist) * rnd.uniform(0.5, 1.0)
        pts.append((min(x, W), HOR - h))
    pts.append((W, HOR))
    d.polygon(pts, fill=col)
    d.line(pts[1:-1], fill=edge, width=2)

# Grille en perspective (magenta), avec halo
grid = Image.new("RGB", (W, H))
g = ImageDraw.Draw(grid)
col = (255, 40, 210)
for i in range(-40, 41):  # lignes fuyantes vers le point de fuite
    xb = cx + i * W * 0.09
    g.line([(cx + i * W * 0.004, HOR), (xb, H)], fill=col, width=2)
z, last = 1.0, H * 2
while True:  # lignes horizontales, de plus en plus serrées vers l'horizon (pas plus serrées que 6 px)
    yy = HOR + (H - HOR) * (1 / z) * 0.9 - (H - HOR) * 0.02
    if yy <= HOR + 6 or last - yy < 6:
        break
    g.line([(0, yy), (W, yy)], fill=col, width=2)
    last = yy
    z += 0.55
g.line([(0, HOR), (W, HOR)], fill=(255, 120, 230), width=3)
gmask = Image.new("L", (W, H), 0)  # la grille s'estompe vers l'horizon
vm = ImageDraw.Draw(gmask)
for yy in range(HOR, H):
    vm.line([(0, yy), (W, yy)], fill=int(255 * min(1.0, 0.25 + (yy - HOR) / (H - HOR) * 1.2)))
grid = ImageChops.multiply(grid, Image.merge("RGB", [gmask] * 3))
halo = grid.filter(ImageFilter.GaussianBlur(6))
img = ImageChops.add(img, halo)
img = ImageChops.add(img, grid)

# Lignes de balayage discrètes (écran cathodique)
sl = ImageDraw.Draw(img, "RGBA")
for yy in range(0, H, 4):
    sl.line([(0, yy), (W, yy)], fill=(0, 0, 0, 38))

img.save(out, optimize=True)
print(out, img.size)
