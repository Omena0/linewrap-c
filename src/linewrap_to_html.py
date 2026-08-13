#!/usr/bin/env python3
import argparse
import base64
import html
import re
import sys
from pathlib import Path

try:
    from PIL import ImageFont
except ImportError:
    print("error: Pillow is required: pip install pillow", file=sys.stderr)
    raise SystemExit(1)

TOKEN_RE = re.compile(r"<([+-]?(?:\d+(?:\.\d*)?|\.\d+))>|([^<\s]+)")

def parse_line(line):
    return [
        ("s", float(m.group(1))) if m.group(1) is not None
        else ("t", m.group(2))
        for m in TOKEN_RE.finditer(line)
    ]

def is_separator(line):
    return line.startswith("----------") and line.endswith("----------")

def measure_line(tokens, font):
    x = 0.0
    runs = []
    for kind, value in tokens:
        if kind == "s":
            x += value
        else:
            runs.append((x, value))
            x += font.getlength(value)
    return x, runs

def main():
    parser = argparse.ArgumentParser(
        description="Turn linewrap C output into a static minified HTML visualizer."
    )
    parser.add_argument("input", help="file containing the C program output")
    parser.add_argument("font", help="TTF/OTF/WOFF/WOFF2 font used by linewrap")
    parser.add_argument("-o", "--output", help="output HTML path (default: INPUT.html)")
    parser.add_argument("--font-size", type=int, default=24,
                        help="font size used by the C program (default: 24)")
    parser.add_argument("--padding", type=int, default=40,
                        help="page padding in px (default: 40)")
    parser.add_argument("--line-gap", type=int, default=10,
                        help="vertical gap between lines in px (default: 10)")
    args = parser.parse_args()

    input_path = Path(args.input)
    font_path = Path(args.font)
    output_path = Path(args.output) if args.output else input_path.with_suffix(".html")

    try:
        text = input_path.read_text(encoding="utf-8")
    except OSError as exc:
        print(f"error: failed to read input: {exc}", file=sys.stderr)
        raise SystemExit(1)

    try:
        font = ImageFont.truetype(str(font_path), args.font_size)
    except Exception as exc:
        print(f"error: failed to load font: {exc}", file=sys.stderr)
        raise SystemExit(1)

    raw_lines = text.replace("\r\n", "\n").replace("\r", "\n").split("\n")
    rendered = []
    detected_width = 0.0

    for line in raw_lines:
        if not line:
            continue
        if is_separator(line):
            rendered.append(("separator", line))
            continue

        tokens = parse_line(line)
        if not tokens:
            continue

        width, runs = measure_line(tokens, font)
        detected_width = max(detected_width, width)
        rendered.append(("line", runs))

    if detected_width <= 0:
        print("error: no renderable lines found", file=sys.stderr)
        raise SystemExit(1)

    strip_width = max(1, int(round(detected_width)))
    line_height = args.font_size + args.line_gap
    total_height = max(line_height, len(rendered) * line_height)
    page_width = strip_width + args.padding * 2

    suffix = font_path.suffix.lower()
    mime = {
        ".ttf": "font/ttf",
        ".otf": "font/otf",
        ".woff": "font/woff",
        ".woff2": "font/woff2",
    }.get(suffix, "font/ttf")

    try:
        font_b64 = base64.b64encode(font_path.read_bytes()).decode("ascii")
    except OSError as exc:
        print(f"error: failed to read font: {exc}", file=sys.stderr)
        raise SystemExit(1)

    svg = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{page_width}" height="{total_height}" viewBox="0 0 {page_width} {total_height}">'
    ]

    y = args.font_size

    for kind, data in rendered:
        if kind == "separator":
            ymid = y - args.font_size // 2
            svg.append(
                f'<line x1="{args.padding}" y1="{ymid}" '
                f'x2="{args.padding + strip_width}" y2="{ymid}" '
                f'stroke="#444" stroke-width="1"/>'
            )
            y += line_height
            continue

        x0 = args.padding

        # Exact left/right endpoints of the text strip.
        svg.append(
            f'<line x1="{x0}" y1="{y - args.font_size}" '
            f'x2="{x0}" y2="{y + 10}" stroke="#AAA" stroke-width="1"/>'
        )
        svg.append(
            f'<line x1="{x0 + strip_width}" y1="{y - args.font_size}" '
            f'x2="{x0 + strip_width}" y2="{y + 10}" stroke="#AAA" stroke-width="1"/>'
        )

        for x, word in data:
            svg.append(
                f'<text x="{x0 + x:.3f}" y="{y}" '
                f'font-family="LinewrapEmbedded" font-size="{args.font_size}px" '
                f'fill="#eee">{html.escape(word)}</text>'
            )

        y += line_height

    svg.append("</svg>")
    svg_text = "".join(svg)

    html_doc = (
        "<!doctype html>"
        '<html><head><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width,initial-scale=1">'
        f"<style>@font-face{{font-family:LinewrapEmbedded;src:url(data:{mime};base64,{font_b64})}}"
        f"html,body{{margin:0;background:#111;color:#eee;min-height:100%;display:flex;justify-content:center;align-items:flex-start;overflow:auto}}"
        f"svg{{display:block;margin:{args.padding}px auto;width:{page_width}px;height:{total_height}px}}"
        "</style></head><body>"
        f"{svg_text}"
        "</body></html>"
    )

    # Static/minified: no JS, no external requests.
    html_doc = re.sub(r">\s+<", "><", html_doc)
    html_doc = html_doc.replace("\n", "").replace("\r", "")

    try:
        output_path.write_text(html_doc, encoding="utf-8")
    except OSError as exc:
        print(f"error: failed to write output: {exc}", file=sys.stderr)
        raise SystemExit(1)

    print(
        f"wrote {output_path} "
        f"(width={strip_width}px, lines={sum(k == 'line' for k, _ in rendered)})"
    )

if __name__ == "__main__":
    main()
