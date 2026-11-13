#!/usr/bin/env python3
"""Prepare optimized JPEGs and editable metadata for Entretiempo's microSD."""
import argparse
from datetime import date, timedelta
from io import BytesIO
import json
import os
from pathlib import Path
import secrets
import sys

from PIL import ExifTags, Image, ImageDraw, ImageOps

LIMIT = 192 * 1024


def exif_date(exif):
    details = exif.get_ifd(ExifTags.IFD.Exif)
    for value in (details.get(36867), details.get(36868)):
        if not isinstance(value, str):
            continue
        try:
            return date.fromisoformat(value[:10].replace(":", "-")).isoformat()
        except ValueError:
            continue
    return ""


def coords(exif):
    gps = exif.get_ifd(ExifTags.IFD.GPSInfo)
    try:
        lat = sum(float(v) / 60**i for i, v in enumerate(gps[2]))
        lon = sum(float(v) / 60**i for i, v in enumerate(gps[4]))
        if len(gps[2]) != 3 or len(gps[4]) != 3 or gps[1] not in ("N", "S") or gps[3] not in ("E", "W"):
            return None, None
        if 0 <= lat <= 90 and 0 <= lon <= 180:
            return lat if gps[1] == "N" else -lat, lon if gps[3] == "E" else -lon
    except (KeyError, ValueError, TypeError, ZeroDivisionError):
        pass
    return None, None


def encode(image):
    fitted = ImageOps.exif_transpose(image).convert("RGBA")
    fitted.thumbnail((480, 254), Image.Resampling.LANCZOS)
    background = Image.new("RGB", fitted.size, "#141311")
    background.paste(fitted, mask=fitted.getchannel("A"))
    for quality in (86, 75, 65):
        output = BytesIO()
        background.save(output, format="JPEG", quality=quality, optimize=True, progressive=False)
        if output.tell() <= LIMIT:
            return output.getvalue()
    raise ValueError("El JPEG excede el tamaño máximo.")


def publish(root, jpeg, meta):
    while True:
        id_number = secrets.randbits(64)
        if not id_number:
            continue
        key = f"{id_number:016x}"
        folder = root / "entretiempo" / f"{id_number & 63:02x}"
        folder.mkdir(parents=True, exist_ok=True)
        basename = folder / key
        if not any(folder.glob(f"{key}.*")):
            break
    meta.update(id=key, rev=1)
    pending = basename.with_suffix(".import")
    try:
        with pending.open("wb") as stream:
            stream.write(jpeg)
            stream.flush()
            os.fsync(stream.fileno())
        with basename.with_suffix(".json").open("w", encoding="utf-8") as stream:
            json.dump(meta, stream, indent=2, ensure_ascii=False)
            stream.flush()
            os.fsync(stream.fileno())
        pending.rename(basename.with_suffix(".jpg"))
    finally:
        pending.unlink(missing_ok=True)


def demo(root, count):
    samples = []
    for index, colors in enumerate((
        ("#252d38", "#ba7750", "#161d28"),
        ("#455457", "#f2cd8e", "#253a36"),
        ("#1b2b48", "#d89178", "#14213a"),
        ("#474040", "#e7a96b", "#302c30"),
    )):
        sky, sun, ground = colors
        image = Image.new("RGB", (480, 254), sky)
        draw = ImageDraw.Draw(image)
        x = 104 + index * 84
        draw.ellipse((x - 27, 45, x + 27, 99), fill=sun)
        draw.polygon([(0, 206), (88, 127), (170, 180), (260, 114), (390, 192), (480, 140), (480, 254), (0, 254)], fill=ground)
        draw.text((20, 218), f"Entretiempo / Demostración {index+1}", fill="#f0e3ca")
        samples.append(encode(image))
    for index in range(count):
        day = date(2005, 11, 14) + timedelta(days=index * 3 // 2)
        if count <= 6:
            day = date(2005 + index * 4, 11, 14)
        publish(root, samples[index % len(samples)], {
            "date": "" if index == count-1 else day.isoformat(),
            "place": "Galería de prueba",
            "note": ("Gira la manivela para seguir leyendo.\n\n" * 42) if index == 0 else "",
        })


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sd", type=Path, help="Raíz de la microSD montada o carpeta de salida")
    parser.add_argument("images", nargs="*", type=Path)
    parser.add_argument("--date", type=date.fromisoformat, help="Sustituye la fecha EXIF")
    parser.add_argument("--no-gps", action="store_true")
    parser.add_argument("--demo", nargs="?", const=6, type=int, metavar="COUNT")
    args = parser.parse_args()
    if not args.sd.is_dir() or (args.demo is None and not args.images):
        parser.error("Indica una carpeta existente y fotos, o usa --demo.")
    if args.demo is not None:
        if not 1 <= args.demo <= 12000 or args.images:
            parser.error("--demo acepta de 1 a 12000 fotos y se usa por separado.")
        demo(args.sd, args.demo)
        return
    failed = False
    for path in args.images:
        try:
            with Image.open(path) as image:
                exif = image.getexif()
                lat, lon = (None, None) if args.no_gps else coords(exif)
                publish(args.sd, encode(image), {
                    "date": args.date.isoformat() if args.date else exif_date(exif),
                    "place": "", "lat": lat, "lon": lon, "note": "",
                })
            print(path.name)
        except (OSError, ValueError) as error:
            print(f"{path.name}: {error}", file=sys.stderr)
            failed = True
    sys.exit(int(failed))


if __name__ == "__main__":
    main()
