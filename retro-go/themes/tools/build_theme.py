"""Build a Retro-Go theme in the style of Art Book Next (Knulli's default theme).

Writes ../artbook (deleted and rebuilt on every run) and preview mockups to ./mock.
Needs ./abn (art-book-next-es clone) and ./logos_png (render_logos.py), see README.md.
"""
import os, json, shutil
from PIL import Image, ImageChops, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.join(HERE, "abn/_inc/systems/artwork-default")
LOGOS = os.path.join(HERE, "logos_png")
OUT = os.path.join(HERE, "..", "artbook")
MOCK = os.path.join(HERE, "mock")
W, H = 320, 240
MAGENTA = (255, 0, 255)

# Retro-Go tab name -> Art Book Next system name (in Retro-Go tab order)
TABS = [
    ("nes", "nes"), ("snes", "snes"), ("gb", "gb"), ("gbc", "gbc"), ("gba", "gba"),
    ("gw", "gameandwatch"), ("sms", "mastersystem"), ("gg", "gamegear"),
    ("md", "megadrive"), ("col", "colecovision"), ("pce", "pcengine"),
    ("lnx", "atarilynx"), ("doom", "doom"), ("msx", "msx"),
    ("favorite", "auto-favorites"), ("recent", "auto-lastplayed"),
]
EXTRA = [("browser", "custom-collections")]

STRIP_H = H
STRIP_W = round(454 * H / 1080)   # 101
SPACING = 86
ART_ALPHA = 0xDD / 255            # systemArtColor ffffffdd


THEME = {
    "website": "https://github.com/anthonycaccese/art-book-next-es",
    "author": "Peter Wilke, Artwork/Logos: Anthony Caccese (CC BY-NC-SA 2.0)",
    "dialog": {
        "background": "0x1082",
        "foreground": "0xFFFF",
        "border": "0x4208",
        "header": "0xFFFF",
        "scrollbar": "0xAD55",
        "shadow": "none",
        "item_standard": "0xFFFF",
        "item_disabled": "0x52AA",
        "item_message": "0xAD55"
    },
    "launcher_1": {
        "__comment": "Art Book Default: weiss auf schwarz",
        "background": "0x0000", "foreground": "0xFFFF",
        "list_standard_bg": "transparent", "list_standard_fg": "0x9CD3",
        "list_selected_bg": "transparent", "list_selected_fg": "0xFFFF"
    },
    "launcher_2": {
        "__comment": "Art Book mit Auswahlbalken",
        "background": "0x0000", "foreground": "0xFFFF",
        "list_standard_bg": "transparent", "list_standard_fg": "0x9CD3",
        "list_selected_bg": "0x4208", "list_selected_fg": "0xFFFF"
    },
    "launcher_3": {
        "__comment": "SNES-Schema (lila)",
        "background": "0x20C8", "foreground": "0xFFFF",
        "list_standard_bg": "transparent", "list_standard_fg": "0x7B72",
        "list_selected_bg": "0x3995", "list_selected_fg": "0xFFFF"
    },
    "launcher_4": {
        "__comment": "Famicom-Schema (rot/creme)",
        "background": "0x2841", "foreground": "0xDEB6",
        "list_standard_bg": "transparent", "list_standard_fg": "0x8C10",
        "list_selected_bg": "0xA8A3", "list_selected_fg": "0xDEB6"
    }
}


def strip(name):
    im = Image.open(os.path.join(ART, name + ".png")).convert("RGBA")
    return im.resize((STRIP_W, STRIP_H), Image.LANCZOS)


def background(seq):
    """seq: list of 5 abn names, index 2 = selected (centered carousel)."""
    bg = Image.new("RGBA", (W, H), (0, 0, 0, 255))
    for i, n in enumerate(seq):
        s = strip(n)
        a = s.getchannel("A").point(lambda v: int(v * ART_ALPHA))
        s.putalpha(a)
        cx = W // 2 + (i - 2) * SPACING
        bg.alpha_composite(s, (cx - STRIP_W // 2, 0))
    return bg.convert("RGB")


def logo_banner(name, max_w=190, max_h=22):
    """White logo with 1px black outline on magenta (Retro-Go transparency), 272x24,
    horizontally centred on screen (banner is drawn at x=47)."""
    src = Image.open(os.path.join(LOGOS, name + ".png")).convert("RGBA")
    src = src.crop(src.getchannel("A").getbbox())
    scale = min(max_w / src.width, max_h / src.height)
    size = (max(1, round(src.width * scale)), max(1, round(src.height * scale)))
    alpha = src.getchannel("A").resize(size, Image.LANCZOS)

    bw, bh = 272, 24
    ox = (W // 2 - 47) - size[0] // 2
    oy = (bh - size[1]) // 2
    A = Image.new("L", (bw, bh), 0)
    A.paste(alpha, (ox, oy))
    outline = A.point(lambda v: 255 if v > 40 else 0).filter(ImageFilter.MaxFilter(3))

    img = Image.new("RGB", (bw, bh), MAGENTA)
    black = Image.new("RGB", (bw, bh), (0, 0, 0))
    img.paste(black, mask=outline)
    white = Image.new("RGB", (bw, bh), (255, 255, 255))
    A = ImageChops.multiply(A, outline)   # no white/magenta blending outside the outline
    img.paste(white, mask=A)   # anti-aliased white over black outline
    return img


def empty_logo():
    return Image.new("RGB", (46, 50), MAGENTA)


def save(img, path):
    # quantize to keep files small (Retro-Go docs recommend low bit depth)
    img.convert("RGB").quantize(colors=256, method=Image.MEDIANCUT, dither=Image.NONE).save(path, optimize=True)


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
        bg = background(seq)
        save(bg, os.path.join(OUT, f"background_{tab}.png"))
        # Retro-Go writes magenta-free banner pixels; keep exact colours (no quantize issues with magenta)
        logo_banner(abn).save(os.path.join(OUT, f"banner_{tab}.png"), optimize=True)
        empty_logo().save(os.path.join(OUT, f"logo_{tab}.png"), optimize=True)

    # generic fallback
    save(background(["gb", "snes", "_default", "nes", "gbc"]), os.path.join(OUT, "background.png"))

    theme = {"description": "Art Book (Knulli-Stil)", **THEME}
    with open(os.path.join(OUT, "theme.json"), "w", encoding="utf-8") as f:
        json.dump(theme, f, indent=4, ensure_ascii=False)

    # preview 160x120 = tab view of NES
    pv = composite_tab_view("nes").resize((160, 120), Image.LANCZOS)
    save(pv, os.path.join(OUT, "preview.png"))

    with open(os.path.join(OUT, "README.md"), "w", encoding="utf-8") as f:
        f.write(README)


def overlay(dst, img, x, y):
    """Mimic Retro-Go: skip exact magenta pixels."""
    img = img.convert("RGB")
    px = img.load(); m = Image.new("L", img.size, 255); mp = m.load()
    for j in range(img.height):
        for i in range(img.width):
            if px[i, j] == MAGENTA:
                mp[i, j] = 0
    dst.paste(img, (x, y), m)


def composite_tab_view(tab):
    bg = Image.open(os.path.join(OUT, f"background_{tab}.png")).convert("RGB")
    off = (H - 50) // 2
    overlay(bg, Image.open(os.path.join(OUT, f"logo_{tab}.png")), 0, off)
    overlay(bg, Image.open(os.path.join(OUT, f"banner_{tab}.png")), 47, off + 8)
    return bg


README = """# Art Book (Knulli-Stil) fuer Retro-Go

Nachbau des Knulli-Standardthemes "Art Book Next" fuer den Retro-Go-Launcher (320x240).

Installation: Ordner nach `/retro-go/themes/artbook/` auf die SD-Karte kopieren und im
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
    for tab in ["nes", "gb", "md", "favorite"]:
        composite_tab_view(tab).resize((640, 480), Image.NEAREST).save(os.path.join(MOCK, f"tab_{tab}.png"))
