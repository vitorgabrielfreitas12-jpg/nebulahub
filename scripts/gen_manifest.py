#!/usr/bin/env python3
"""
NEBULA HUB — Manifest Generator
Fetches the archive.org ps4-permanent-themes-repository listing
and generates a full manifest.json with every PKG pair detected.

Usage:
    python3 scripts/gen_manifest.py --output manifest/manifest.json
"""

import urllib.request
import urllib.parse
import json
import re
import argparse
import time
import os

THEMES_ARCHIVE  = "https://archive.org/download/ps4-permanent-themes-repository/"
AVATARS_ARCHIVE = "https://archive.org/download/ps4-jb-avatars/"

ANIME_KEYWORDS = [
    "naruto","dragon ball","one piece","bleach","attack on titan","demon slayer",
    "my hero academia","sword art","jojo","fairy tail","tokyo ghoul","hunter x hunter",
    "fullmetal","death note","miku","vocaloid","hatsune","one punch","mob psycho",
    "black clover","boruto","re:zero","overlord","konosuba","steins","violet evergarden",
    "code geass","evangelion","fate","persona","tales"
]

GAME_KEYWORDS = [
    "god of war","ghost of tsushima","bloodborne","dark souls","elden ring","sekiro",
    "last of us","spider-man","uncharted","horizon","ratchet","nioh","days gone",
    "death stranding","detroit","control","outer worlds","final fantasy","ff7","ff15",
    "kingdom hearts","dragon quest","monster hunter","resident evil","devil may cry",
    "dmc","crash","spyro","sonic","minecraft","call of duty","battlefield","titanfall",
    "destiny","division","rainbow six","far cry","assassin","watch dogs","cyberpunk",
    "witcher","red dead","gta","mafia","bioshock","prey","fallout","the outer worlds",
    "borderlands","risk of rain","hollow knight","celeste","cuphead","dead cells",
    "tekken","street fighter","mortal kombat","guilty gear","dragon ball fighterz",
    "persona 5","yakuza","like a dragon","nier","automata","metal gear","silent hill"
]

def slugify(name):
    s = name.lower()
    s = re.sub(r"[^a-z0-9]+", "_", s)
    s = s.strip("_")
    return s[:40]

def fetch_listing(url):
    req = urllib.request.Request(url, headers={"User-Agent": "NebulaHub/1.0"})
    with urllib.request.urlopen(req, timeout=30) as r:
        return r.read().decode("utf-8")

def get_category(name):
    nl = name.lower()
    for kw in ANIME_KEYWORDS:
        if kw in nl:
            return "anime", kw.replace(" ", "_")
    for kw in GAME_KEYWORDS:
        if kw in nl:
            return "games", kw.replace(" ", "_")
    return "other", "general"

def get_tags(name):
    tags = []
    nl = name.lower()
    for kw in ANIME_KEYWORDS + GAME_KEYWORDS:
        if kw in nl:
            tags.append(kw.replace(" ", "_"))
    if "dynamic" in nl or "animated" in nl:
        tags.append("dynamic")
        tags.append("animated")
    else:
        tags.append("static")
    return list(dict.fromkeys(tags))[:8]

def parse_theme_listing(html):
    # Find all .pkg files in the listing
    files = re.findall(r'href="([^"]+\.pkg)"', html)
    # Decode URL-encoded names
    files = [urllib.parse.unquote(f) for f in files
             if not urllib.parse.urlsplit(f).netloc and "/" not in f]
    return files

def build_theme_pairs(files):
    # Group _1.pkg and _2.pkg pairs
    stubs   = {}  # name → filename
    payloads = {}
    for f in files:
        if f.endswith("_1.pkg"):
            base = f[:-6]
            stubs[base] = f
        elif f.endswith("_2.pkg"):
            base = f[:-6]
            payloads[base] = f
    pairs = []
    for base, stub in stubs.items():
        if base in payloads:
            pairs.append((base, stub, payloads[base]))
    return pairs

def generate_theme_entries(pairs, base_url):
    entries = []
    id_counter = 1
    for base, stub_file, payload_file in pairs:
        name = base.replace("_", " ").replace("-", " ").strip()
        animated = "dynamic" in name.lower() or "animated" in name.lower()
        cat, subcat = get_category(name)
        tags = get_tags(name)
        slug = slugify(name)

        stub_url    = base_url + urllib.parse.quote(stub_file)
        payload_url = base_url + urllib.parse.quote(payload_file)

        entry = {
            "id":               f"th_{slug}_{id_counter:04d}",
            "name":             name.title(),
            "type":             "theme",
            "category":         cat,
            "subcategory":      subcat,
            "animated":         animated,
            "preview_url":      "",
            "download_url":     "",
            "pkg_stub_url":     stub_url,
            "pkg_payload_url":  payload_url,
            "file_size":        0,
            "stub_size":        0,
            "payload_size":     0,
            "file_format":      "pkg_split",
            "source":           "ps4-permanent-themes-repository / archive.org",
            "install_method":   "pkg_install",
            "tags":             tags
        }
        entries.append(entry)
        id_counter += 1
    return entries

def build_avatar_entries():
    # Avatar entries are FPKG bulk packs (see Phase 1 findings)
    return [
        {
            "id": "av_pack_001",
            "name": "Extra Avatars Mega Pack",
            "type": "avatar",
            "category": "mixed",
            "subcategory": "general",
            "animated": False,
            "preview_url": "",
            "download_url": f"{AVATARS_ARCHIVE}Extra%20Avatars.pkg",
            "pkg_stub_url": "",
            "pkg_payload_url": "",
            "file_size": 724535296,
            "stub_size": 0,
            "payload_size": 0,
            "file_format": "xavatar_fpkg",
            "source": "F1nnish / archive.org",
            "install_method": "pkg_install",
            "tags": ["mixed", "pack", "600+"]
        },
        {
            "id": "av_pack_002",
            "name": "Extra Avatars — Anime Pack",
            "type": "avatar",
            "category": "anime",
            "subcategory": "mixed_anime",
            "animated": False,
            "preview_url": "",
            "download_url": f"{AVATARS_ARCHIVE}Extra%20Avatars%20-%20Anime.pkg",
            "pkg_stub_url": "",
            "pkg_payload_url": "",
            "file_size": 157286400,
            "stub_size": 0,
            "payload_size": 0,
            "file_format": "xavatar_fpkg",
            "source": "F1nnish / archive.org",
            "install_method": "pkg_install",
            "tags": ["anime", "pack", "200+", "naruto", "dragonball", "onepiece"]
        },
        {
            "id": "av_pack_003",
            "name": "Extra Avatars — PSN Originals",
            "type": "avatar",
            "category": "playstation",
            "subcategory": "psn_originals",
            "animated": False,
            "preview_url": "",
            "download_url": f"{AVATARS_ARCHIVE}Extra%20Avatars%20-%20PSN.pkg",
            "pkg_stub_url": "",
            "pkg_payload_url": "",
            "file_size": 263192576,
            "stub_size": 0,
            "payload_size": 0,
            "file_format": "xavatar_fpkg",
            "source": "F1nnish / archive.org",
            "install_method": "pkg_install",
            "tags": ["psn", "official", "playstation", "pack", "300+"]
        },
        {
            "id": "av_pack_004",
            "name": "Extra Avatars — Games Pack",
            "type": "avatar",
            "category": "games",
            "subcategory": "mixed_games",
            "animated": False,
            "preview_url": "",
            "download_url": "",
            "pkg_stub_url": "",
            "pkg_payload_url": "",
            "file_size": 0,
            "stub_size": 0,
            "payload_size": 0,
            "file_format": "xavatar_fpkg",
            "source": "F1nnish / archive.org",
            "install_method": "pkg_install",
            "tags": ["games", "pack", "godofwar", "spiderman", "uncharted"]
        }
    ]

def main():
    parser = argparse.ArgumentParser(description="Generate NEBULA HUB manifest")
    parser.add_argument("--output", default="manifest/manifest.json")
    parser.add_argument("--offline", action="store_true",
                        help="Skip network fetch, use seed data only")
    args = parser.parse_args()

    print("NEBULA HUB — Manifest Generator")
    print("="*40)

    theme_entries = []

    if not args.offline:
        print(f"Fetching theme listing from archive.org...")
        try:
            html = fetch_listing(THEMES_ARCHIVE)
            files = parse_theme_listing(html)
            print(f"  Found {len(files)} .pkg files")
            pairs = build_theme_pairs(files)
            print(f"  Built {len(pairs)} stub/payload pairs")
            theme_entries = generate_theme_entries(pairs, THEMES_ARCHIVE)
            print(f"  Generated {len(theme_entries)} theme entries")
        except Exception as e:
            print(f"  Warning: network fetch failed ({e}) — using seed data")
    else:
        print("Offline mode — using seed entries only")

    avatar_entries = build_avatar_entries()
    print(f"Avatar pack entries: {len(avatar_entries)}")

    all_entries = avatar_entries + theme_entries

    manifest = {
        "version":       "1.0.0",
        "generated":     time.strftime("%Y-%m-%d"),
        "total_avatars": sum(1 for e in all_entries if e["type"] == "avatar"),
        "total_themes":  sum(1 for e in all_entries if e["type"] == "theme"),
        "entries":       all_entries
    }

    os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
    with open(args.output, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)

    print(f"\n✅ Manifest written → {args.output}")
    print(f"   {manifest['total_avatars']} avatar entries")
    print(f"   {manifest['total_themes']} theme entries")
    print(f"   {len(all_entries)} total")

if __name__ == "__main__":
    main()
