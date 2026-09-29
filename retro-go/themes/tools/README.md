# Theme-Werkzeuge

Skripte, mit denen die Themes `../artbook` und `../artbook_v03` aus
[Art Book Next](https://github.com/anthonycaccese/art-book-next-es) erzeugt werden.
`artbook_blur` wird von diesen Skripten **nicht** erzeugt.

## Einrichten

```sh
cd retro-go/themes/tools
git clone --depth 1 --filter=blob:none --sparse https://github.com/anthonycaccese/art-book-next-es.git abn
git -C abn sparse-checkout set _inc/systems/artwork-default _inc/systems/logos
python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt
playwright install chromium
```

## Ausführen

```sh
python render_logos.py     # abn/_inc/systems/logos/*.svg -> logos_png/*.png
python build_theme.py      # -> ../artbook
python build_theme_v03.py  # -> ../artbook_v03 (nutzt die Funktionen aus build_theme.py)
```

**Achtung:** Beide Skripte löschen ihren Zielordner vor dem Neuaufbau komplett.
Vorschauen der Launcher-Ansicht landen in `mock/`.

### v03

- Hintergrund: wie `artbook` (gleiche Palettenreduktion), alles außerhalb der mittleren
  Kachel mit `DIM` (0.4) abgedunkelt.
- `carousel_<tab>.png`: Logo weiß auf schwarz, in `CAROUSEL_MAX` (216×56) eingepasst,
  2 px schwarze Kontur, drumherum Magenta (in Retro-Go transparent).
- `banner_`/`logo_` wie `artbook`; der Launcher nutzt sie nur ohne Karussell-Patch.
- `preview.png`: Karussell-Ansicht von NES, 160×120.

### Neues System hinzufügen

1. Logoname in `NAMES` in `render_logos.py` eintragen, `render_logos.py` ausführen.
2. In `TABS` in `build_theme_v03.py` (bzw. `build_theme.py`) an der Stelle eintragen, an
   der der Tab im Launcher steht. Die Reihenfolge zählt: Jeder Hintergrund zeigt die zwei
   Nachbarn links und rechts.
3. Skript ausführen. Der Tab-Name ist das Kürzel aus `launcher/main/applications.c`
   (z. B. `a26`), der Art-Book-Name der Dateiname in `abn/_inc/systems/`.

`abn/`, `logos_png/`, `mock/` und `.venv/` sind nur Arbeitsdateien und nicht im Repo.
