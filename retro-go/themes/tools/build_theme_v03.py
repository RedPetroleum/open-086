"""Build the Art Book v03 theme: like build_theme.py (artbook), but only the centre tile is
bright and every tab gets a large carousel_<tab>.png logo (needs the launcher patch in
retro-go/retro-go.patch).

Writes ../artbook_v03 (deleted and rebuilt on every run) and preview mockups to ./mock.
Uses the same inputs as build_theme.py (./abn, ./logos_png), see README.md.
"""
import os, json, shutil
from PIL import Image, ImageFilter
import build_theme as base

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "artbook_v03")
MOCK = base.MOCK
W, H = base.W, base.H
MAGENTA = base.MAGENTA

# Retro-Go tab name -> Art Book Next system name (in Retro-Go tab order).
# The background of each tab shows its two neighbours on either side, so the order matters.
TABS = [
    ("nes", "nes"), ("snes", "snes"), ("gb", "gb"), ("gbc", "gbc"), ("gba", "gba"),
    ("gw", "gameandwatch"), ("sms", "mastersystem"), ("gg", "gamegear"),
    ("md", "megadrive"), ("col", "colecovision"), ("pce", "pcengine"),
    ("lnx", "atarilynx"), ("a26", "atari2600"), ("doom", "doom"), ("msx", "msx"),
    ("favorite", "auto-favorites"), ("recent", "auto-lastplayed"),
]
EXTRA = base.EXTRA

DIM = 0.4                    # brightness of the outer tiles
CAROUSEL_MAX = (216, 56)     # logo fits into this box ...
CAROUSEL_PAD = 2             # ... plus this border for the outline (max. 220x60)


def quantized(img):
    """Same palette reduction as base.save(), returned as an RGB image."""
    return img.convert("RGB").quantize(colors=256, method=Image.MEDIANCUT, dither=Image.NONE).convert("RGB")


def background(seq):
    """Art Book background (quantized as in artbook), everything outside the centre tile
    darkened to DIM. seq: list of 5 abn names, index 2 = selected."""
    bg = quantized(base.background(seq))
    centre = base.strip(seq[2]).getchannel("A").point(lambda v: 255 if v > 0 else 0)
    mask = Image.new("L", (W, H), 0)
    mask.paste(centre, (W // 2 - base.STRIP_W // 2, 0))
    dark = bg.point(lambda v: round(v * DIM))
    return Image.composite(bg, dark, mask)


def carousel(name):
    """Large logo for the carousel view: anti-aliased white on black, 2px black outline,
    magenta (Retro-Go transparency) around it. Cropped to the logo."""
    src = Image.open(os.path.join(base.LOGOS, name + ".png")).convert("RGBA")
    src = src.crop(src.getchannel("A").getbbox())
    mw, mh = CAROUSEL_MAX
    scale = min(mw / src.width, mh / src.height)
    size = (max(1, round(src.width * scale)), max(1, round(src.height * scale)))
    alpha = src.getchannel("A").resize(size, Image.LANCZOS)

    p = CAROUSEL_PAD
    A = Image.new("L", (size[0] + 2 * p, size[1] + 2 * p), 0)
    A.paste(alpha, (p, p))
    outline = A.point(lambda v: 255 if v >= 128 else 0).filter(ImageFilter.MaxFilter(2 * p + 1))

    img = Image.new("RGB", A.size, MAGENTA)
    img.paste(Image.merge("RGB", (A, A, A)), mask=outline)
    return img


def carousel_view(tab):
    """What the launcher shows for a tab: background with the carousel logo centred."""
    bg = Image.open(os.path.join(OUT, f"background_{tab}.png")).convert("RGB")
    c = Image.open(os.path.join(OUT, f"carousel_{tab}.png"))
    base.overlay(bg, c, (W - c.width) // 2, (H - c.height) // 2)
    return bg


def main():
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT); os.makedirs(MOCK, exist_ok=True)
    names = [a for _, a in TABS]
    n = len(names)
    for idx, (tab, abn) in enumerate(TABS + EXTRA):
        if idx < n:
            seq = [names[(idx + k) % n] for k in range(-2, 3)]
        else:
            seq = [names[-2], names[-1], abn, names[0], names[1]]
        background(seq).save(os.path.join(OUT, f"background_{tab}.png"), optimize=True)
        # banner/logo are only used without the launcher patch (or in low memory mode)
        base.logo_banner(abn).save(os.path.join(OUT, f"banner_{tab}.png"), optimize=True)
        base.empty_logo().save(os.path.join(OUT, f"logo_{tab}.png"), optimize=True)
        carousel(abn).save(os.path.join(OUT, f"carousel_{tab}.png"), optimize=True)

    # generic fallback
    background(["gb", "snes", "_default", "nes", "gbc"]).save(os.path.join(OUT, "background.png"), optimize=True)

    theme = {"description": "Art Book v03 (Knulli-Stil)", **base.THEME}
    with open(os.path.join(OUT, "theme.json"), "w", encoding="utf-8") as f:
        json.dump(theme, f, indent=4, ensure_ascii=False)

    # preview 160x120 = carousel view of NES
    carousel_view("nes").resize((160, 120), Image.LANCZOS).save(os.path.join(OUT, "preview.png"), optimize=True)

    with open(os.path.join(OUT, "README.md"), "w", encoding="utf-8") as f:
        f.write(README)


README = """# Art Book (Knulli-Stil) fuer Retro-Go

Nachbau des Knulli-Standardthemes "Art Book Next" fuer den Retro-Go-Launcher (320x240).
Variante v03: nur die mittlere Kachel ist hell, die aeusseren Kacheln sind stark abgedunkelt.
Grosse Systemlogos im Karussell ueber `carousel_<tab>.png` (aus den Original-SVGs gerendert);
dafuer braucht der Launcher den Patch in `retro-go/retro-go.patch`. Ohne Patch erscheinen
die normalen, kleinen Banner.

Installation: Ordner nach `/retro-go/themes/artbook_v03/` auf die SD-Karte kopieren und im
Launcher als Theme waehlen.

## Credits / Lizenz
Systemartwork und Systemlogos stammen aus "Art Book Next" von Anthony Caccese
(https://github.com/anthonycaccese/art-book-next-es), lizenziert unter
Creative Commons CC BY-NC-SA 2.0 (https://creativecommons.org/licenses/by-nc-sa/2.0/).
Die meisten Systemlogos basieren auf den Neuzeichnungen von Dan Patrick
(https://archive.org/details/console-logos-professionally-redrawn-plus-official-versions).
Idee der Multi-Artwork-Systemansicht: GenoCL.
Die Bilder wurden fuer Retro-Go verkleinert, zusammengesetzt und umgefaerbt.
Dieses Theme steht unter derselben Lizenz (CC BY-NC-SA 2.0), nicht kommerziell.
"""

if __name__ == "__main__":
    main()
    # mocks
    for tab in ["nes", "lnx", "a26", "doom"]:
        carousel_view(tab).resize((640, 480), Image.NEAREST).save(os.path.join(MOCK, f"v03_{tab}.png"))
