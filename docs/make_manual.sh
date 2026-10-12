#!/bin/sh
# Builds docs/PLI-User-Guide.docx (twice, so the contents has real page numbers).
# Needs: node with the docx package, LibreOffice (soffice) and poppler.
set -e
cd "$(dirname "$0")"
SOFFICE=${SOFFICE:-soffice}
rm -f pages.json
node build_manual.js pages.json PLI-User-Guide.docx
for pass in 1 2; do
  $SOFFICE --headless --convert-to pdf PLI-User-Guide.docx >/dev/null 2>&1
  python3 pages.py PLI-User-Guide.pdf _toc.json pages.json
  node build_manual.js pages.json PLI-User-Guide.docx
done
$SOFFICE --headless --convert-to pdf PLI-User-Guide.docx >/dev/null 2>&1
echo "built docs/PLI-User-Guide.docx"
