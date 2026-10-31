#!/usr/bin/env python3
"""Make the single PDF for the Google Form (max 1 MB): docs/report.pdf + your handwritten Task F pages (max 2).
Usage: python tools/merge_appendix.py page1.jpg page2.jpg [-o submission.pdf]     (JPG/PNG photos or scans)
Needs: pip install pillow pypdf.   Photos are converted to greyscale and shrunk until the PDF is < 1,000,000 bytes."""
import io, os, sys
from PIL import Image, ImageOps
from pypdf import PdfReader, PdfWriter

args = sys.argv[1:]; out = "submission.pdf"
if "-o" in args: i = args.index("-o"); out = args[i + 1]; del args[i:i + 2]
if not 1 <= len(args) <= 2: sys.exit("give 1 or 2 image files (handwritten appendix is max 2 pages)")
report = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs", "report.pdf")

def build(width, quality):
    w = PdfWriter()
    for pg in PdfReader(report).pages: w.add_page(pg)
    for f in args:
        im = ImageOps.exif_transpose(Image.open(f)).convert("L")
        im = ImageOps.autocontrast(im, cutoff=1)
        if im.width > width: im = im.resize((width, int(im.height * width / im.width)), Image.LANCZOS)
        b = io.BytesIO(); im.save(b, "PDF", resolution=width / 8.27, quality=quality)  # A4 width 8.27 in
        for pg in PdfReader(io.BytesIO(b.getvalue())).pages: w.add_page(pg)
    b = io.BytesIO(); w.write(b); return b.getvalue()

for width, q in ((1600, 60), (1400, 50), (1200, 45), (1000, 40), (850, 35)):
    data = build(width, q)
    if len(data) < 1_000_000: break
open(out, "wb").write(data)
print(f"{out}: {len(PdfReader(out).pages)} pages, {len(data)/1024:.0f} KiB", "OK" if len(data) < 1_000_000 else "STILL TOO BIG")
