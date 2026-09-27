#!/usr/bin/env bash
# Stage tessdata (and optional .so libs) for Linux development — no CLI binary.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${ROOT}/third_party/tesseract/linux"

TESSDATA_SRC=""
for candidate in \
  /usr/share/tesseract-ocr/5/tessdata \
  /usr/share/tesseract-ocr/4.00/tessdata \
  /usr/share/tesseract-ocr/tessdata \
  /usr/share/tessdata
do
  if [[ -f "${candidate}/eng.traineddata" ]]; then
    TESSDATA_SRC="${candidate}"
    break
  fi
done

if [[ -z "${TESSDATA_SRC}" ]]; then
  echo "Could not find tessdata. Install:"
  echo "  sudo apt install tesseract-ocr-ara tesseract-ocr-fra tesseract-ocr-eng"
  exit 1
fi

for lang in ara fra eng; do
  if [[ ! -f "${TESSDATA_SRC}/${lang}.traineddata" ]]; then
    echo "Missing ${lang}.traineddata in ${TESSDATA_SRC}"
    exit 1
  fi
done

mkdir -p "${DEST}/tessdata"
cp -L "${TESSDATA_SRC}/ara.traineddata" "${DEST}/tessdata/"
cp -L "${TESSDATA_SRC}/fra.traineddata" "${DEST}/tessdata/"
cp -L "${TESSDATA_SRC}/eng.traineddata" "${DEST}/tessdata/"

# Optional: stage shared libs next to tessdata for portable installs.
if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists tesseract; then
  mkdir -p "${DEST}/lib"
  for lib in $(pkg-config --libs-only-L tesseract lept 2>/dev/null | tr ' ' '\n' | sed -n 's/^-L//p'); do
    for name in libtesseract.so* liblept.so* libleptonica.so*; do
      compgen -G "${lib}/${name}" >/dev/null && cp -a "${lib}/"${name} "${DEST}/lib/" || true
    done
  done
fi

echo "Staged Linux OCR data at ${DEST}"
echo "Build with: libtesseract-dev libleptonica-dev (linked in-process, no tesseract CLI)."
echo "Reconfigure CMake after staging."
