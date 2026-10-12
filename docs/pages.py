"""Find the printed page of every contents entry in a rendered PDF.

usage: pages.py manual.pdf toc.json pages.json
Printed page numbers restart at 1 on the Preface, so printed = pdf page - front pages.
"""
import json
import re
import subprocess
import sys

pdf, tocf, outf = sys.argv[1:4]
toc = json.load(open(tocf, encoding='utf8'))

n = int(re.search(r'Pages:\s+(\d+)', subprocess.run(['pdfinfo', pdf], capture_output=True, text=True).stdout).group(1))
pages = []
for i in range(1, n + 1):
    t = subprocess.run(['pdftotext', '-f', str(i), '-l', str(i), '-layout', pdf, '-'], capture_output=True, text=True).stdout
    pages.append([ln.strip() for ln in t.splitlines()])

# the Preface heading is the first page whose lines contain exactly "Preface" after the contents
# (the contents itself lists "Preface" with a page number on the same line)
front = None
for i, lines in enumerate(pages):
    if 'Preface' in lines and not any(l.startswith('Contents') for l in lines):
        front = i
        break
assert front is not None, 'Preface page not found'

result = []
cursor = front
for e in toc:
    for i in range(cursor, n):
        if e['text'] in pages[i]:
            result.append(str(i - front + 1))
            cursor = i
            break
    else:
        raise SystemExit('heading not found: ' + e['text'])

json.dump(result, open(outf, 'w', encoding='utf8'), indent=1)
print('front pages:', front, ' total pages:', n, ' entries:', len(result))
