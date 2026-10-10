"""Extract original embedded manual figures without resizing or retouching them.

Usage: python extract_manual_images.py PATH_TO_REFERENCE_PDF_DIRECTORY
Requires pypdf and Pillow. This helper is documentation tooling, not a product dependency.
"""

from pathlib import Path
import hashlib
import json
import shutil
import sys

from PIL import Image
from pypdf import PdfReader


SOURCES = [
    (
        "ZNB_GettingStarted_en_70.pdf",
        "https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/"
        "getting_started/1316_0062_01/ZNB_GettingStarted_en_70.pdf",
        [
            (37, 0, "gs-v70-p037-main-quad.png"),
            (38, 0, "gs-v70-p038-toolbar.png"),
            (39, 2, "gs-v70-p039-hardkeys.png"),
            (40, 2, "gs-v70-p040-softtool.png"),
            (46, 1, "gs-v70-p046-add-trace.png"),
            (47, 0, "gs-v70-p047-figure-1.png"),
            (47, 1, "gs-v70-p047-figure-2.png"),
            (48, 1, "gs-v70-p048-marker-placement.png"),
            (48, 2, "gs-v70-p048-marker-strip.png"),
        ],
    ),
    (
        "ZNB_UserManual_en_74.pdf",
        "https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/"
        "user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf",
        [
            (90, 0, "um-v74-p090-single-cartesian.png"),
            (91, 0, "um-v74-p091-trace-strip.png"),
            (92, 0, "um-v74-p092-marker-symbol.png"),
            (94, 0, "um-v74-p094-marker-info-1.png"),
            (94, 1, "um-v74-p094-marker-info-2.png"),
            (95, 0, "um-v74-p095-marker-table.png"),
            (99, 0, "um-v74-p099-channel-info.png"),
            (802, 0, "um-v74-p802-color-scheme-dialog.png"),
            (803, 0, "um-v74-p803-limit-trace-example.png"),
        ],
    ),
]

PAGE_RENDERS = [
    ("page37-37.png", "gs-v70-page-037.png"),
    ("page38-38.png", "gs-v70-page-038.png"),
    ("page39-39.png", "gs-v70-page-039.png"),
    ("softtools-40.png", "gs-v70-page-040.png"),
    ("page48-48.png", "gs-v70-page-048.png"),
    ("manual-page90-0090.png", "um-v74-page-090.png"),
    ("manual-page94-0094.png", "um-v74-page-094.png"),
]


def main() -> None:
    source_root = Path(sys.argv[1]).resolve()
    root = Path(__file__).resolve().parent
    manual = root / "manual"
    pages = root / "pages"
    manual.mkdir(exist_ok=True)
    pages.mkdir(exist_ok=True)
    records = []
    sources = []
    for pdf_name, url, figures in SOURCES:
        source = source_root / pdf_name
        reader = PdfReader(source)
        sources.append({
            "manual": pdf_name,
            "source": url,
            "sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        })
        for page_number, image_index, filename in figures:
            embedded = list(reader.pages[page_number - 1].images)[image_index]
            target = manual / filename
            target.write_bytes(embedded.data)
            records.append({
                "file": target.relative_to(root).as_posix(),
                "source": url,
                "manual": pdf_name,
                "pdfPage": page_number,
                "printedPage": page_number,
                "imageObject": embedded.name,
                "width": embedded.image.width,
                "height": embedded.image.height,
                "sha256": hashlib.sha256(target.read_bytes()).hexdigest(),
                "method": "PDF embedded image exported at native pixel dimensions, no resizing or retouching",
            })
    page_records = []
    for old_name, new_name in PAGE_RENDERS:
        source = source_root / old_name
        if source.exists():
            target = pages / new_name
            shutil.copyfile(source, target)
            source_pdf = SOURCES[0] if new_name.startswith("gs-") else SOURCES[1]
            with Image.open(target) as rendered:
                page_records.append({
                    "file": target.relative_to(root).as_posix(),
                    "source": source_pdf[1],
                    "manual": source_pdf[0],
                    "pdfPage": int(new_name[-7:-4]),
                    "width": rendered.width,
                    "height": rendered.height,
                    "sha256": hashlib.sha256(target.read_bytes()).hexdigest(),
                    "method": "whole-page reference render copied without alteration",
                })
    (root / "manual-manifest.json").write_text(
        json.dumps(records, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    (root / "source-manifest.json").write_text(
        json.dumps(sources, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    (root / "page-manifest.json").write_text(
        json.dumps(page_records, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(f"Extracted {len(records)} original manual figures.")


if __name__ == "__main__":
    main()
