"""Build the unified-precipitation before/after regression contact sheet."""

from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageEnhance, ImageStat


ROOT = Path(__file__).resolve().parents[1] / "docs" / "precipitation-state-captures"
CASES = ("dry", "normal", "heavy", "acid")
THUMB = (512, 384)
HEADER = 28


def fitted(path: Path) -> Image.Image:
    image = Image.open(path).convert("RGB")
    # The deterministic harness can emit a one-line GPU-behind diagnostic.
    image = image.crop((0, 24, image.width, image.height))
    image.thumbnail(THUMB)
    return image


def main() -> None:
    canvas = Image.new("RGB", (THUMB[0] * 3, (THUMB[1] + HEADER) * len(CASES)), "#101010")
    draw = ImageDraw.Draw(canvas)
    draw.text((8, 7), "before", fill="white")
    draw.text((THUMB[0] + 8, 7), "after", fill="white")
    draw.text((THUMB[0] * 2 + 8, 7), "8x difference", fill="white")

    for row, name in enumerate(CASES):
        before = fitted(ROOT / "before" / f"{name}.png")
        after = fitted(ROOT / "after" / f"{name}.png")
        difference = ImageChops.difference(before, after)
        mean_abs = sum(ImageStat.Stat(difference).mean) / 3.0
        changed = sum(pixel != (0, 0, 0) for pixel in difference.getdata())
        total = difference.width * difference.height
        print(f"{name}: mean_abs={mean_abs:.4f}, changed={changed}/{total} ({100.0 * changed / total:.2f}%)")
        difference = ImageEnhance.Brightness(difference).enhance(8.0)

        y = row * (THUMB[1] + HEADER)
        if row:
            draw.text((8, y + 7), "before", fill="white")
            draw.text((THUMB[0] + 8, y + 7), "after", fill="white")
            draw.text((THUMB[0] * 2 + 8, y + 7), "8x difference", fill="white")
        draw.text((410, y + 7), name, fill="#8fd3ff")
        for column, image in enumerate((before, after, difference)):
            x = column * THUMB[0] + (THUMB[0] - image.width) // 2
            canvas.paste(image, (x, y + HEADER))

    canvas.save(ROOT / "comparison.png")


if __name__ == "__main__":
    main()
