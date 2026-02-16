# PGM Image Guide for `epd_pgm`

This target accepts PGM grayscale files in either:
- `P5` (binary/raw)
- `P2` (ASCII/plain)

## Recommended: use the helper script (always writes strict `P5`)

Use:

```bash
./make_epd_pgm.sh ~/Downloads/EscherHandSphere.jpg ~/EHS.pgm
```

Options:

```bash
./make_epd_pgm.sh --force-dither input.jpg output.pgm
./make_epd_pgm.sh --no-dither input.jpg output.pgm
./make_epd_pgm.sh --6in input.jpg output.pgm
./make_epd_pgm.sh --7.8in input.jpg output.pgm
```

What it does:
- auto-orients image from EXIF
- rotates portrait images by +90 degrees so long edge matches the frame long edge
- scales/crops to the selected display profile:
  - `--7.8in`: `1872x1404` (default)
  - `--6in`: `1440x1072`
- auto-detects "high color count" images and applies Atkinson dithering to 16 grayscale levels
- writes a strict binary `P5` header and pixel payload

By default, dithering is enabled when estimated unique colors are greater than `128`.
The script prints whether dithering was applied.

## 1. Build the target

```bash
make -j4 LIB=GPIOD
```

This produces `./epd_pgm`.

## 2. Convert an image to compatible PGM

Use ImageMagick (`magick`) and force:
- grayscale colorspace
- 8-bit depth
- no compression
- PGM output (`pgm:` prefix)

```bash
magick input.png -colorspace Gray -depth 8 -compress none -define pnm:format=raw pgm:output.pgm
```

If your ImageMagick still writes `P2`, that is also accepted by `epd_pgm`.

## 3. Optional size handling before display

The program already truncates anything larger than panel size, but you can pre-process explicitly.

Resize to fit inside 1872x1404 without enlarging:

```bash
magick input.png -colorspace Gray -depth 8 -resize 1872x1404\> -compress none pgm:output.pgm
```

Center-crop exactly to 1872x1404:

```bash
magick input.png -colorspace Gray -depth 8 -gravity center -crop 1872x1404+0+0 +repage -compress none pgm:output.pgm
```

Force exact output dimensions with padding (white background):

```bash
magick input.png -colorspace Gray -depth 8 -resize 1872x1404 -background white -gravity center -extent 1872x1404 -compress none pgm:output.pgm
```

## 4. Inspect the PGM format (sanity check)

```bash
magick identify -verbose output.pgm | grep -E "Format|Geometry|Depth|Colorspace"
```

Expected:
- `Format: PGM`
- `Colorspace: Gray`
- `Depth: 8-bit`

Check exact header bytes:

```bash
hexdump -C -n 2 output.pgm
```

- `50 35` means `P5` (binary/raw)
- `50 32` means `P2` (ASCII/plain)

## 5. Send image to panel

```bash
sudo ./epd_pgm -2.51 ./output.pgm 0
sudo ./epd_pgm --incremental -2.51 ./output.pgm 0
sudo ./epd_pgm --no-clear -2.51 ./output.pgm 0
sudo ./epd_pgm --fast-clear -2.51 ./output.pgm 0
```

Arguments:
- `-2.51`: your panel VCOM value (from FPC label)
- `./output.pgm`: input grayscale PGM file
- `0`: display mode (optional; default `0`)
- `--incremental`: optional; refresh in incremental chunks (32 lines at a time, packed 4bpp) and print progress
- `--no-clear`: optional; skip pre-clear for speed (may increase ghosting)
- `--fast-clear`: optional; pre-clear using full-screen `1bpp` white refresh (lower data volume, faster, less aggressive)

Notes:
- Drawing starts at `(0,0)`.
- No scaling is done by `epd_pgm`.
- If image dimensions exceed panel dimensions, extra pixels are truncated.
- Program prints:
  - image update time (refresh path only)
  - total wall clock time from initialization to finish

## 6. Clear mode recommendations

`epd_pgm` supports three pre-clear behaviors:

- default (no flag): `INIT` 4bpp clear
  - Best image quality / least ghosting
  - Slowest startup
- `--fast-clear`: 1bpp packed clear
  - Much faster startup
  - May leave some residual ghosting/artifacts
- `--no-clear`: skip clear
  - Fastest startup
  - Highest risk of ghosting from previous frame

Recommended usage:

- Final quality display: use default clear (no flag)
- Fast iteration / previews: use `--fast-clear`
- Controlled experiments only: use `--no-clear`
