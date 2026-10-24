#!/usr/bin/env python3
"""Writes THIRD-PARTY-NOTICES.txt for a Sanctuary Choir download: the licence of every library
built into the plug-in (JUCE and what it vendors), of the fonts, and of the WebView2 loader on
Windows, so the downloads carry the notices their licences ask for.

Usage: collect_notices.py <JUCE source dir> <output file> --platform macos|windows|linux
                          [--webview2 <folder the Microsoft.Web.WebView2 package was installed to>]

The libraries are read from JUCE's own bill of materials (JUCE.spdx.json): the version, licence
and the licence files where JUCE vendors each one. Only the libraries in the JUCE modules the
plug-in links (see plugin/CMakeLists.txt) and that its settings switch on are listed.
"""
import argparse
import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# The libraries the plug-in contains, by their name in JUCE.spdx.json, and the platforms that
# build them in (None: all). Left out: Oboe (Android), the ASIO, AAX and LV2 SDKs (not built),
# juce_animation and juce_javascript's libraries (modules the plug-in does not link) and JUCE's
# examples and tools.
LIBRARIES = [
    ("zlib", None),
    ("HarfBuzz", None),
    ("SheenBidi", None),
    ("LunaSVG", None),
    ("PlutoVG", None),
    ("libpng", None),
    ("jpeglib", None),
    ("libwebp", None),
    ("FLAC", None),
    ("libogg", None),
    ("libvorbis", None),
    ("Opus", None),
    ("opusfile", None),
    ("libopusenc", None),
    ("VST3 SDK", None),
    ("pslextensions", None),
    ("AudioUnitSDK", {"macos"}),
]

FONTS = [("Cinzel", "Cinzel"), ("Cormorant Garamond", "CormorantGaramond"), ("Montserrat", "Montserrat")]

RULE = "=" * 78


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read().rstrip() + "\n"


def licence_files(folder):
    names = sorted(os.listdir(folder))
    picked = [n for n in names
              if any(k in n.upper() for k in ("LICENSE", "LICENCE", "COPYING"))
              and not n.upper().endswith((".H", ".C", ".CPP"))]
    return [os.path.join(folder, n) for n in picked]


def jpeg_terms(folder):
    """The IJG licence is the LEGAL ISSUES section of jpeglib's README."""
    lines = read(os.path.join(folder, "README")).splitlines()
    start = next(i for i, l in enumerate(lines) if l.strip() == "LEGAL ISSUES")
    end = next(i for i in range(start + 2, len(lines)) if lines[i].strip() == "REFERENCES")
    return "\n".join(lines[start:end]).rstrip() + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("juce")
    ap.add_argument("out")
    ap.add_argument("--platform", required=True, choices=["macos", "windows", "linux"])
    ap.add_argument("--webview2")
    args = ap.parse_args()

    bom = json.load(open(os.path.join(args.juce, "JUCE.spdx.json"), encoding="utf-8"))
    packages = {p["name"]: p for p in bom["packages"]}
    juce = packages["JUCE"]

    parts = [
        "SANCTUARY CHOIR - THIRD-PARTY NOTICES\n",
        "Sanctuary Choir is free software under the GNU Affero General Public License,\n"
        "version 3 (LICENSE.txt, next to this file). Its source code is at\n"
        "https://github.com/DK7602/Sanctuary-Choir.\n\n"
        "It is built with the JUCE framework, which contains the libraries below, and uses\n"
        "the fonts below. Each comes under its own licence, reproduced here as its authors\n"
        "ask. The recorded singers are credited in CREDITS.txt in the SanctuaryVoices\n"
        "folder, and the measured rooms in ROOMS-CREDITS.txt.\n",
    ]

    def section(title, details, body):
        parts.append("\n" + RULE + "\n" + title + "\n" + "".join(d + "\n" for d in details) + RULE + "\n\n" + body)

    section("JUCE %s" % juce["versionInfo"],
            [juce["copyrightText"], "Used under the GNU Affero General Public License, version 3.",
             "https://juce.com"],
            read(os.path.join(args.juce, "LICENSE.md")))

    for name, platforms in LIBRARIES:
        if platforms and args.platform not in platforms:
            continue
        p = packages[name]
        source = p.get("sourceInfo", "")
        folder = os.path.join(args.juce, source.split("Vendored at ", 1)[1].split(" in the JUCE", 1)[0])
        version = p.get("versionInfo", "NOASSERTION")
        title = name if version == "NOASSERTION" else "%s %s" % (name, version)
        licence = p["licenseConcluded"]
        details = ["Licence: " + ("public domain" if licence == "LicenseRef-Public-Domain" else licence)]
        if p.get("downloadLocation", "NOASSERTION") != "NOASSERTION":
            details.append(p["downloadLocation"])
        if name == "jpeglib":
            body = jpeg_terms(folder)
        elif name == "pslextensions":
            body = "PreSonus's plug-in extension headers, placed in the public domain.\n"
        else:
            files = licence_files(folder)
            if not files:
                sys.exit("No licence file for %s in %s" % (name, folder))
            body = "\n".join(read(f) for f in files)
        section(title, details, body)

    if args.platform == "windows":
        if not args.webview2:
            sys.exit("--webview2 is needed for Windows (the WebView2 loader is linked in)")
        prefix = "Microsoft.Web.WebView2."
        packs = sorted(d for d in os.listdir(args.webview2) if d.startswith(prefix))
        found = [os.path.join(d, f) for pack in packs[-1:]
                 for d, _, fs in os.walk(os.path.join(args.webview2, pack)) for f in fs if f.upper() == "LICENSE.TXT"]
        if not found:
            sys.exit("No LICENSE.txt in a WebView2 package under " + args.webview2)
        section("Microsoft Edge WebView2 SDK " + packs[-1][len(prefix):],
                ["The WebView2 loader, linked into the Windows plug-in.",
                 "https://developer.microsoft.com/microsoft-edge/webview2"],
                read(min(found, key=len)))

    for font, file in FONTS:
        section("Font: " + font, ["Licence: OFL-1.1"],
                read(os.path.join(REPO, "ui", "assets", "fonts", "OFL-%s.txt" % file)))

    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(parts))
    print("Wrote %s (%d sections)" % (args.out, len(parts) - 2))


if __name__ == "__main__":
    main()
