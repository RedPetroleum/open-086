"""Rendert die Systemlogos (SVG) aus Art Book Next als PNG nach ./logos_png.
Voraussetzung: art-book-next-es als ./abn geklont, pip install playwright && playwright install chromium
"""
import os, base64
from playwright.sync_api import sync_playwright

HERE = os.path.dirname(os.path.abspath(__file__))
ABN = os.path.join(HERE, "abn/_inc/systems/logos")
OUT = os.path.join(HERE, "logos_png")
os.makedirs(OUT, exist_ok=True)

NAMES = ["nes", "snes", "gb", "gbc", "gba", "gameandwatch", "mastersystem", "gamegear",
         "megadrive", "colecovision", "pcengine", "atarilynx", "atari2600", "doom", "msx",
         "auto-favorites", "auto-lastplayed", "custom-collections"]

with sync_playwright() as p:
    exe = os.environ.get("CHROMIUM_PATH")  # optional, sonst Playwright-Standard-Chromium
    b = p.chromium.launch(executable_path=exe) if exe else p.chromium.launch()
    page = b.new_page(viewport={"width": 1600, "height": 400})
    for n in NAMES:
        svg = open(os.path.join(ABN, n + ".svg"), encoding="utf-8").read()
        html = f"""<html><body style="margin:0;background:transparent">
        <div id="c" style="width:1600px;height:400px;display:flex;align-items:center;justify-content:center">
        <img style="max-width:1560px;max-height:360px;width:1560px;height:360px;object-fit:contain"
             src="data:image/svg+xml;base64,{base64.b64encode(svg.encode()).decode()}"></div></body></html>"""
        page.set_content(html)
        page.wait_for_timeout(50)
        page.locator("#c").screenshot(path=os.path.join(OUT, n + ".png"), omit_background=True)
        print("ok", n)
    b.close()
