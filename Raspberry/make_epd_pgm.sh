#!/usr/bin/env bash
set -euo pipefail

INPUT=""
OUTPUT=""

force_dither=0
no_dither=0
profile="7.8in"

FRAME_W=1872
FRAME_H=1404
COLOR_THRESHOLD=128

usage() {
  echo "Usage: ./make_epd_pgm.sh [options] <input_image> [output.pgm]"
  echo
  echo "Display profile (choose one):"
  echo "  --7.8in          1872x1404 (default)"
  echo "  --6in            1440x1072"
  echo
  echo "Dithering:"
  echo "  --force-dither   Always use Atkinson dither to 16 gray levels"
  echo "  --no-dither      Disable dithering"
  echo "                   (default: auto-dither when unique colors > ${COLOR_THRESHOLD})"
  echo
  echo "Other:"
  echo "  -h, --help       Show this help"
  echo "Example: ./make_epd_pgm.sh ~/Downloads/EscherHandSphere.jpg ~/EHS.pgm"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --force-dither)
      force_dither=1
      shift
      ;;
    --no-dither)
      no_dither=1
      shift
      ;;
    --7.8in)
      profile="7.8in"
      shift
      ;;
    --6in)
      profile="6in"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    -*)
      echo "Unknown option: $1"
      usage
      exit 1
      ;;
    *)
      if [[ -z "$INPUT" ]]; then
        INPUT="$1"
      elif [[ -z "$OUTPUT" ]]; then
        OUTPUT="$1"
      else
        echo "Too many positional arguments."
        usage
        exit 1
      fi
      shift
      ;;
  esac
done

if (( force_dither == 1 && no_dither == 1 )); then
  echo "Use only one of --force-dither or --no-dither."
  exit 1
fi

if [[ "$profile" == "7.8in" ]]; then
  FRAME_W=1872
  FRAME_H=1404
elif [[ "$profile" == "6in" ]]; then
  FRAME_W=1440
  FRAME_H=1072
else
  echo "Internal error: unknown profile '$profile'"
  exit 1
fi

if [[ -z "$INPUT" ]]; then
  usage
  exit 1
fi

if [[ ! -f "$INPUT" ]]; then
  echo "Input file not found: $INPUT"
  exit 1
fi

if ! command -v magick >/dev/null 2>&1; then
  echo "ImageMagick 'magick' command not found."
  exit 1
fi

if [[ -z "$OUTPUT" ]]; then
  base="$(basename "$INPUT")"
  OUTPUT="${base%.*}.pgm"
fi

tmp_raw="$(mktemp)"
cleanup() {
  rm -f "$tmp_raw"
}
trap cleanup EXIT

# Respect EXIF orientation before checking dimensions.
dims="$(magick "$INPUT" -auto-orient -ping -format "%w %h" info: 2>/dev/null || true)"
if [[ -z "$dims" ]]; then
  echo "Failed to read image dimensions with ImageMagick."
  echo "Input: $INPUT"
  exit 1
fi
img_w="${dims% *}"
img_h="${dims#* }"
if [[ -z "$img_w" || -z "$img_h" ]]; then
  echo "Unexpected identify output: '$dims'"
  exit 1
fi

rotate_needed=0
if (( img_h > img_w )); then
  # Portrait input: rotate so long edge maps to frame long edge (landscape).
  rotate_needed=1
fi

color_count="$(magick "$INPUT" -auto-orient -ping -format "%k" info: 2>/dev/null || true)"
if [[ -z "$color_count" ]]; then
  echo "Failed to estimate color count with ImageMagick."
  exit 1
fi

apply_dither=0
if (( force_dither == 1 )); then
  apply_dither=1
elif (( no_dither == 1 )); then
  apply_dither=0
elif (( color_count > COLOR_THRESHOLD )); then
  apply_dither=1
fi

echo "Input after auto-orient: ${img_w}x${img_h}"
echo "Display profile: ${profile}"
echo "Target frame: ${FRAME_W}x${FRAME_H}"
if (( img_h > img_w )); then
  echo "Portrait detected, rotating +90 degrees."
fi
echo "Estimated unique colors: ${color_count}"
if (( apply_dither == 1 )); then
  echo "Dithering: ON (Atkinson; reduce to 16 grayscale levels)"
else
  echo "Dithering: OFF"
fi

# Generate exactly FRAME_W*FRAME_H bytes (8-bit grayscale) and write a strict P5 header.
if (( rotate_needed == 1 )); then
  if (( apply_dither == 1 )); then
    magick "$INPUT" \
      -auto-orient \
      -rotate 90 \
      -colorspace Gray \
      -depth 8 \
      -resize "${FRAME_W}x${FRAME_H}^" \
      -gravity center \
      -extent "${FRAME_W}x${FRAME_H}" \
      -dither Atkinson \
      -colors 16 \
      -strip \
      gray:"$tmp_raw"
  else
    magick "$INPUT" \
      -auto-orient \
      -rotate 90 \
      -colorspace Gray \
      -depth 8 \
      -resize "${FRAME_W}x${FRAME_H}^" \
      -gravity center \
      -extent "${FRAME_W}x${FRAME_H}" \
      -strip \
      gray:"$tmp_raw"
  fi
else
  if (( apply_dither == 1 )); then
    magick "$INPUT" \
      -auto-orient \
      -colorspace Gray \
      -depth 8 \
      -resize "${FRAME_W}x${FRAME_H}^" \
      -gravity center \
      -extent "${FRAME_W}x${FRAME_H}" \
      -dither Atkinson \
      -colors 16 \
      -strip \
      gray:"$tmp_raw"
  else
    magick "$INPUT" \
      -auto-orient \
      -colorspace Gray \
      -depth 8 \
      -resize "${FRAME_W}x${FRAME_H}^" \
      -gravity center \
      -extent "${FRAME_W}x${FRAME_H}" \
      -strip \
      gray:"$tmp_raw"
  fi
fi

expected_size=$((FRAME_W * FRAME_H))
actual_size="$(wc -c < "$tmp_raw")"
if [[ "$actual_size" != "$expected_size" ]]; then
  echo "Unexpected raw payload size: got $actual_size bytes, expected $expected_size."
  exit 1
fi

{
  printf "P5\n%d %d\n255\n" "$FRAME_W" "$FRAME_H"
  cat "$tmp_raw"
} > "$OUTPUT"

echo "Wrote: $OUTPUT"
echo "Header check:"
hexdump -C -n 16 "$OUTPUT"
