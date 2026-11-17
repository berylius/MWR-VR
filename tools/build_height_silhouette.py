"""Convert our SVG into merged native LUI white-image spans (no custom GPU assets).

Run after editing height-tpose.svg; --check verifies the checked-in Lua is current.
The source uses circles, rectangles and polygons; unsupported geometry fails.
"""
from pathlib import Path
import argparse
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1] / "src/client/resources/ui_scripts"


def generate():
    root = ET.parse(ROOT / "height-tpose.svg").getroot()
    assert root.attrib["viewBox"] == "0 0 320 320"
    shapes = []
    for element in root:
        kind = element.tag.rsplit("}", 1)[-1]
        a = element.attrib
        if kind == "title":
            continue
        if kind == "circle":
            cx, cy, radius = (float(a[k]) for k in ("cx", "cy", "r"))
            shapes.append(lambda x, y, cx=cx, cy=cy, radius=radius: (x-cx)**2+(y-cy)**2 <= radius**2)
        elif kind == "rect":
            rx, ry, width, height = (float(a[k]) for k in ("x", "y", "width", "height"))
            shapes.append(lambda x, y, rx=rx, ry=ry, w=width, h=height: rx <= x < rx+w and ry <= y < ry+h)
        elif kind == "polygon":
            points = [tuple(map(float, point.split(","))) for point in a["points"].split()]
            def inside(x, y, points=points):
                value = False
                for (ax, ay), (bx, by) in zip(points, points[1:]+points[:1]):
                    if (ay > y) != (by > y) and x < (bx-ax)*(y-ay)/(by-ay)+ax:
                        value = not value
                return value
            shapes.append(inside)
        else:
            raise ValueError(f"Unsupported SVG shape: {kind}")
    spans, active = [], {}
    for y in range(321):
        runs = []
        if y < 320:
            start = None
            for x in range(321):
                ink = x < 320 and any(shape(x+.5, y+.5) for shape in shapes)
                if ink and start is None:
                    start = x
                elif not ink and start is not None:
                    runs.append((start, x))
                    start = None
        for run in list(active):
            if run not in runs:
                start = active.pop(run)
                spans.append((run[0], start, run[1], y))
        for run in runs:
            active.setdefault(run, y)
    spans.sort(key=lambda span: (span[1], span[0]))
    return ("-- Generated from height-tpose.svg by tools/build_height_silhouette.py.\n"
            "VRHeightSilhouette = {\n" + "".join("    {%d,%d,%d,%d},\n" % span for span in spans) + "}\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    destination = ROOT / "height_tpose.lua"
    text = generate()
    if args.check:
        assert destination.read_text(encoding="utf-8") == text, "Regenerate height_tpose.lua"
    else:
        destination.write_text(text, encoding="utf-8", newline="\n")
