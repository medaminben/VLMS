# Tesseract OCR (library / DLL only — no CLI) for VLMS
#
# Windows customer package — self-contained app folder (no system pollution):
#
#   <AppFolder>/
#     vlms.exe
#     tesseract*.dll
#     leptonica-*.dll
#     (other OCR dependency DLLs only)
#     tessdata/
#       ara.traineddata
#       fra.traineddata
#       eng.traineddata
#     schema.sql
#
# Rules:
#   - DLLs + tessdata live beside the exe only
#   - no tesseract.exe
#   - nothing installed to System32 / PATH / Program Files\Tesseract-OCR
#   - include/ and lib/ stay on the build PC (not shipped)
#
# Languages: ship all three above, but the app only needs ONE to enable OCR.
# Whatever *.traineddata is present becomes a language the librarian can pick
# from the dropdown on the "from image" button, labelled with the catalogue's
# own name for it. Adding a language is a file copy, not a code change -- drop
# spa.traineddata in beside the others and Spanish books can be read. The
# exception is osd.traineddata, which is orientation data rather than a
# recognition model and is deliberately never offered.
#
# Populate from a UB Mannheim install (MSVC — needs VC++ runtime on target PCs):
#   powershell -File scripts\fetch_windows_ocr.ps1 -SourceDir 'C:\Program Files\Tesseract-OCR'
#
# Self-contained MinGW bundle (recommended for customer installs):
#   pacman -S mingw-w64-x86_64-tesseract mingw-w64-x86_64-leptonica ...
#   powershell -File scripts\fetch_windows_ocr.ps1 -Toolchain MinGW
#
# Linux developer:
#   sudo apt install libtesseract-dev libleptonica-dev \
#                    tesseract-ocr-ara tesseract-ocr-fra tesseract-ocr-eng
#   ./scripts/stage_linux_ocr.sh   # optional tessdata into linux/
