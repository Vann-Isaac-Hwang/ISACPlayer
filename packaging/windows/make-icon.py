"""Regenerates resources/windows/isacplayer.ico from the application PNG.

The Windows executable embeds a multi-resolution icon. Regenerate it after
changing resources/icons/isacplayer.png:

    python packaging/windows/make-icon.py
"""

from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "resources" / "icons" / "isacplayer.png"
TARGET = ROOT / "resources" / "windows" / "isacplayer.ico"
SIZES = [(16, 16), (20, 20), (24, 24), (32, 32), (40, 40),
         (48, 48), (64, 64), (128, 128), (256, 256)]


def main() -> None:
    TARGET.parent.mkdir(parents=True, exist_ok=True)
    with Image.open(SOURCE) as image:
        image.convert("RGBA").save(TARGET, format="ICO", sizes=SIZES)
    print(f"wrote {TARGET.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
