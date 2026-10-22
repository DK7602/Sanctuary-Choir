#!/usr/bin/env python3
"""Assembles a browser preview of the Sanctuary Choir UI (simulated engine + demo audio).

Usage: python3 tools/preview/build_preview.py <output dir> [--artifact]

--artifact writes index.html without <html>/<head>/<body> wrappers (for hosts that add
their own page skeleton). Demo audio is embedded as base64 in demo-audio.js so it plays
without fetching media files.
"""
import base64
import os
import re
import shutil
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    out = os.path.abspath(sys.argv[1])
    artifact = "--artifact" in sys.argv[2:]
    if os.path.exists(out):
        shutil.rmtree(out)
    shutil.copytree(os.path.join(ROOT, "ui"), out, ignore=shutil.ignore_patterns("*.txt"))

    demo_dir = os.path.join(ROOT, "preview", "demo")
    entries = []
    for name in sorted(os.listdir(demo_dir)):
        if name.endswith(".mp3"):
            with open(os.path.join(demo_dir, name), "rb") as f:
                entries.append('  "%s": "%s"' % (name, base64.b64encode(f.read()).decode("ascii")))
    with open(os.path.join(out, "js", "demo-audio.js"), "w") as f:
        f.write("// Demo renders from the Sanctuary Choir engine (see preview/README.md).\n")
        f.write("window.SC_DEMO_AUDIO = {\n" + ",\n".join(entries) + "\n};\n")

    index = os.path.join(out, "index.html")
    with open(index) as f:
        html = f.read()
    html = html.replace('<script src="js/bridge.js"></script>',
                        '<script src="js/demo-audio.js"></script>\n<script src="js/bridge.js"></script>')
    if artifact:
        html = re.sub(r"<!doctype html>\s*", "", html, flags=re.I)
        html = re.sub(r"</?html[^>]*>\s*", "", html)
        html = re.sub(r"</?head>\s*", "", html)
        html = re.sub(r"</?body>\s*", "", html)
        html = re.sub(r'<meta charset="utf-8">\s*', "", html)
        html = re.sub(r'<meta name="viewport"[^>]*>\s*', "", html)
        html = html.replace("<title>Sanctuary Choir</title>",
                            '<title>Sanctuary Choir</title>\n<meta name="claude:viewport-fit-sides" content="cover">')
    with open(index, "w") as f:
        f.write(html)
    print("Preview written to", out)


if __name__ == "__main__":
    main()
