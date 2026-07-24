#!/usr/bin/env python3
"""
sprites2lvgl.py — slice AI-generated 3x3 expression sheets into LVGL 9
RGB565 C arrays + a persona animation manifest.

Input:  firmware/assets/<persona>/raw/<persona>_NN_<name>.png   (3x3 grid sheets)
Output: firmware/components/persona_assets/  (self-contained IDF component)
        - <persona>_NN_<name>_fXX.c   one lv_image_dsc_t per frame
        - <persona>_anims.h/.c        moo_anim_t table, serial order
        - CMakeLists.txt              auto-generated

Usage:
    python3 tools/sprites2lvgl.py --persona cow
    python3 tools/sprites2lvgl.py --persona cow --size 240 --grid 3
Requires: Pillow  (pip install Pillow)
"""

import argparse
import re
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow missing: pip install Pillow")

# fps / loop per animation name; default (12, False)
ANIM_PARAMS = {
    "idle_blink": (12, False),   # cadence driven by face_ui, not loop
    "idle_look":  (10, False),   # cadence driven by face_ui, not loop
    "pat":        (12, False),
    "happy":      (12, False),
    "celebrate":  (14, False),
    "sad":        (8,  False),
    "sleepy":     (6,  True),
    "alert":      (12, False),
    "task_done":  (12, False),
    "wake":       (10, False),
    "eat":        (8,  True),
}

SHEET_RE = re.compile(r"^(?P<persona>[a-z0-9]+)_(?P<order>\d{2})_(?P<name>[a-z0-9_]+)\.png$")


def rgb565_bytes(img: Image.Image) -> bytes:
    """RGB888 -> RGB565 little-endian."""
    rgb = img.convert("RGB")
    out = bytearray(rgb.width * rgb.height * 2)
    i = 0
    for r, g, b in rgb.getdata():
        v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        out[i] = v & 0xFF
        out[i + 1] = v >> 8
        i += 2
    return bytes(out)


def emit_frame_c(path: Path, sym: str, img: Image.Image) -> None:
    data = rgb565_bytes(img)
    w, h = img.size
    lines = [
        '#include "lvgl.h"',
        "",
        f"static const uint8_t {sym}_map[] = {{",
    ]
    for off in range(0, len(data), 16):
        chunk = ", ".join(f"0x{b:02X}" for b in data[off:off + 16])
        lines.append(f"    {chunk},")
    lines += [
        "};",
        "",
        f"const lv_image_dsc_t {sym} = {{",
        "    .header = {",
        "        .magic = LV_IMAGE_HEADER_MAGIC,",
        "        .cf = LV_COLOR_FORMAT_RGB565,",
        f"        .w = {w},",
        f"        .h = {h},",
        f"        .stride = {w * 2},",
        "    },",
        f"    .data_size = {len(data)},",
        f"    .data = {sym}_map,",
        "};",
        "",
    ]
    path.write_text("\n".join(lines))


def slice_sheet(sheet: Image.Image, grid: int, size: int):
    cw, ch = sheet.width // grid, sheet.height // grid
    for row in range(grid):
        for col in range(grid):
            cell = sheet.crop((col * cw, row * ch, (col + 1) * cw, (row + 1) * ch))
            side = min(cell.size)
            left = (cell.width - side) // 2
            top = (cell.height - side) // 2
            cell = cell.crop((left, top, left + side, top + side))
            yield cell.resize((size, size), Image.LANCZOS)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--persona", required=True)
    ap.add_argument("--grid", type=int, default=3)
    ap.add_argument("--size", type=int, default=240)
    ap.add_argument("--repo", default=str(Path(__file__).resolve().parent.parent))
    args = ap.parse_args()

    repo = Path(args.repo)
    raw_dir = repo / "firmware" / "assets" / args.persona / "raw"
    out_dir = repo / "firmware" / "components" / "persona_assets"
    out_dir.mkdir(parents=True, exist_ok=True)

    sheets = []
    for p in sorted(raw_dir.glob(f"{args.persona}_*.png")):
        m = SHEET_RE.match(p.name)
        if m:
            sheets.append((int(m.group("order")), m.group("name"), p))
    if not sheets:
        sys.exit(f"no sheets matching {args.persona}_NN_<name>.png in {raw_dir}")

    src_files, anims = [], []
    for order, name, path in sheets:
        sheet = Image.open(path)
        frame_syms = []
        for idx, frame in enumerate(slice_sheet(sheet, args.grid, args.size)):
            sym = f"img_{args.persona}_{order:02d}_{name}_f{idx:02d}"
            fname = f"{sym}.c"
            emit_frame_c(out_dir / fname, sym, frame)
            frame_syms.append(sym)
            src_files.append(fname)
        fps, loop = ANIM_PARAMS.get(name, (12, False))
        anims.append((name, frame_syms, fps, loop))
        print(f"  {path.name}: {len(frame_syms)} frames @ {fps} fps loop={loop}")

    # manifest header
    hdr = [
        "#pragma once",
        "",
        '#include "anim_engine.h"',
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
    ]
    hdr += [f"#define MOO_HAS_ANIM_{n.upper()} 1" for n, *_ in anims]
    hdr.append("")
    hdr += [f"extern const moo_anim_t moo_anim_{n};" for n, *_ in anims]
    hdr += ["", "#ifdef __cplusplus", "}", "#endif", ""]
    (out_dir / f"{args.persona}_anims.h").write_text("\n".join(hdr))

    # manifest source
    src = [f'#include "{args.persona}_anims.h"', ""]
    for name, syms, fps, loop in anims:
        src += [f"extern const lv_image_dsc_t {s};" for s in syms]
        src.append(f"static const lv_image_dsc_t *frames_{name}[] = {{")
        src += [f"    &{s}," for s in syms]
        src += [
            "};",
            f"const moo_anim_t moo_anim_{name} = {{",
            f"    .frames = frames_{name},",
            f"    .count = {len(syms)},",
            f"    .fps = {fps},",
            f"    .loop = {'true' if loop else 'false'},",
            "};",
            "",
        ]
    (out_dir / f"{args.persona}_anims.c").write_text("\n".join(src))
    src_files.append(f"{args.persona}_anims.c")

    cml = [
        "# GENERATED by tools/sprites2lvgl.py — do not edit",
        "idf_component_register(SRCS",
    ]
    cml += [f'    "{f}"' for f in src_files]
    cml += [
        '    INCLUDE_DIRS "."',
        "    REQUIRES anim_engine)",
        "",
    ]
    (out_dir / "CMakeLists.txt").write_text("\n".join(cml))

    (out_dir / "idf_component.yml").write_text(
        "# GENERATED by tools/sprites2lvgl.py — do not edit\n"
        "dependencies:\n"
        '  idf: ">=5.1"\n'
        '  lvgl/lvgl: "^9"\n'
    )

    total = sum(len(s) for _, s, _, _ in anims) * args.size * args.size * 2
    print(f"done: {len(anims)} anims, ~{total // 1024} KiB RGB565 in flash")


if __name__ == "__main__":
    main()
