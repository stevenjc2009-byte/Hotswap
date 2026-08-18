"""Generates the app's launcher art: icon.png, cia/banner.png, cia/banner.wav.

Everything is drawn at 4x and downsampled, because the icon ships at 48x48 and
rasterising the rounded corners directly at that size loses them entirely.

The mark is three cartridges below a slot rail with the middle one raised into
it - "pick one, it goes in". It carries no text and names no game, so it holds
up when other games are added and it survives a rename. Only the banner
wordmark depends on the name, which is why the name is an argument:

    python tools/make_art.py Carousel "Swap your mods. Play."

The three cartridge colours match the three buttons in the app: stock (blue),
the community mod (orange), your own (green).
"""

from __future__ import annotations

import struct
import sys
import wave
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent

SHELL = (20, 26, 38)
RAIL = (57, 67, 90)
STOCK = (46, 109, 184)
COMMUNITY = (216, 74, 30)
CUSTOM = (46, 158, 91)
ACTIVE = (255, 213, 74)
TEXT = (242, 245, 250)

MARK = 192  # the mark's native drawing size; every use scales from it


def draw_mark(size: int = MARK, shell: bool = True) -> Image.Image:
    """Three cartridges below a slot rail, the middle one raised into it."""
    img = Image.new("RGBA", (MARK, MARK), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    if shell:
        d.rounded_rectangle((0, 0, MARK - 1, MARK - 1), radius=34, fill=SHELL)

    d.rounded_rectangle((30, 52, 161, 64), radius=6, fill=RAIL)

    # The chosen mod, lifted out of the row and seated in the rail.
    d.rounded_rectangle((72, 34, 119, 95), radius=7, fill=COMMUNITY)
    d.rounded_rectangle((82, 46, 109, 52), radius=3, fill=SHELL)

    d.rounded_rectangle((30, 112, 77, 163), radius=7, fill=STOCK)
    d.rounded_rectangle((114, 112, 161, 163), radius=7, fill=CUSTOM)

    # The motion: this is the one that went up.
    d.line((96, 110, 96, 100), fill=ACTIVE, width=7)
    d.line((89, 107, 96, 100), fill=ACTIVE, width=7)
    d.line((103, 107, 96, 100), fill=ACTIVE, width=7)

    if size != MARK:
        img = img.resize((size, size), Image.LANCZOS)
    return img


def load_font(size: int, bold: bool = True) -> ImageFont.ImageFont:
    names = ("segoeuib.ttf", "arialbd.ttf") if bold else ("segoeui.ttf", "arial.ttf")
    for name in names:
        p = Path("C:/Windows/Fonts") / name
        if p.exists():
            return ImageFont.truetype(str(p), size)
    return ImageFont.load_default()


def make_icon(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    draw_mark(MARK).resize((48, 48), Image.LANCZOS).convert("RGB").save(path)


def make_banner(path: Path, name: str, tagline: str) -> None:
    S = 4  # supersample factor
    img = Image.new("RGB", (256 * S, 128 * S), SHELL)
    d = ImageDraw.Draw(img)

    mark = draw_mark(72 * S, shell=False)
    img.paste(mark, (14 * S, 28 * S), mark)

    d.text((100 * S, 38 * S), name, font=load_font(26 * S), fill=TEXT)
    d.text((101 * S, 74 * S), tagline, font=load_font(10 * S), fill=ACTIVE)

    for i, colour in enumerate((STOCK, COMMUNITY, CUSTOM)):
        x = (101 + i * 19) * S
        d.rounded_rectangle((x, 92 * S, x + 15 * S, 96 * S), radius=2 * S, fill=colour)

    path.parent.mkdir(parents=True, exist_ok=True)
    img.resize((256, 128), Image.LANCZOS).save(path)


def make_silent_wav(path: Path, seconds: float = 0.5, rate: int = 22050) -> None:
    """bannertool needs a valid PCM wav; silence keeps the CIA small and quiet."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(struct.pack("<h", 0) * int(rate * seconds))


def main() -> None:
    name = sys.argv[1] if len(sys.argv) > 1 else "Carousel"
    tagline = sys.argv[2] if len(sys.argv) > 2 else "Swap your mods. Play."

    make_icon(ROOT / "icon.png")
    make_banner(ROOT / "cia" / "banner.png", name, tagline)
    make_silent_wav(ROOT / "cia" / "banner.wav")

    # Large copy for the README header and the GitHub social card.
    draw_mark(512).convert("RGB").save(ROOT / "cia" / "logo-512.png")

    print(f"wrote icon.png, cia/banner.png, cia/banner.wav, cia/logo-512.png ({name})")


if __name__ == "__main__":
    main()
