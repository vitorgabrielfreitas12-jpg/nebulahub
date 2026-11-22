#!/usr/bin/env python3
"""Build the Nebula Hub catalog from public Archive.org file indexes."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile
import urllib.parse
import urllib.request

THEMES_ITEM = "ps4-permanent-themes-repository"
AVATARS_ITEM = "ps4-jb-avatars"
SEED = Path(__file__).resolve().parents[1] / "manifest" / "seed.json"


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

def fetch_index(item):
    request = urllib.request.Request(f"https://archive.org/metadata/{item}",
                                     headers={"User-Agent": "NebulaHub/1.0"})
    with urllib.request.urlopen(request, timeout=30) as response:
        data = response.read(16 * 1024 * 1024 + 1)
    if len(data) > 16 * 1024 * 1024:
        raise ValueError("archive index exceeds 16 MiB")
    index = json.loads(data)
    if not isinstance(index.get("files"), list):
        raise ValueError(f"missing file index for {item}")
    return {file["name"]: file for file in index["files"]
            if isinstance(file.get("name"), str)
            and file.get("private") not in (True, "true", "1")
            and "/" not in file["name"] and "\\" not in file["name"]}


def file_url(item, name):
    return f"https://archive.org/download/{item}/{urllib.parse.quote(name, safe='')}"


def file_size(record):
    size = str(record.get("size", ""))
    return int(size) if size.isdecimal() else 0


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

def build_catalog(seed, avatars, themes):
    entries = []
    pairs = {}
    duplicate_count = 0
    avatar_seed = [entry for entry in seed["entries"] if entry["type"] == "avatar"]
    theme_seed = [entry for entry in seed["entries"] if entry["type"] == "theme"]

    for original in avatar_seed:
        entry = dict(original)
        name = urllib.parse.unquote(urllib.parse.urlsplit(entry["download_url"]).path.rsplit("/", 1)[-1])
        entry["download_url"] = file_url(AVATARS_ITEM, name) if name in avatars else ""
        entry["file_size"] = file_size(avatars[name]) if name in avatars else 0
        entries.append(entry)

    for original in theme_seed:
        entry = dict(original)
        names = tuple(urllib.parse.unquote(urllib.parse.urlsplit(entry[field]).path.rsplit("/", 1)[-1])
                      for field in ("pkg_stub_url", "pkg_payload_url"))
        if all(name in themes for name in names):
            pairs[names] = entry
        else:
            entry.update(pkg_stub_url="", pkg_payload_url="", stub_size=0, payload_size=0)
            entries.append(entry)

    # Only pair exact matching file stems. Similar titles may have unrelated
    # licenses or regions. Standalone files are not inferred to be themes.
    candidates = {}
    for stub in sorted(themes):
        if not stub.endswith("_1.pkg"):
            continue
        base = stub[:-6]
        payload = base + "_2.pkg"
        if payload not in themes:
            continue
        candidates[(stub, payload)] = base

    by_hash = set()
    for names in sorted(candidates, key=lambda pair: (pair not in pairs, pair)):
        stub, payload = names
        base = candidates[names]
        hashes = (themes[stub].get("sha1"), themes[payload].get("sha1"))
        if all(hashes) and hashes in by_hash:
            duplicate_count += 1
            continue
        if all(hashes):
            by_hash.add(hashes)
        if names in pairs:
            entry = pairs[names]
        else:
            digest = hashlib.sha256(base.encode("utf-8")).hexdigest()[:16]
            category, subcategory = get_category(base)
            entry = {
                "id": f"th_{slugify(base)}_{digest}",
                "name": base,
                "type": "theme",
                "category": category,
                "subcategory": subcategory,
                "animated": bool(re.search(r"\b(dynamic|animated)\b", base, re.I)),
                "download_url": "",
                "file_format": "pkg_split",
                "source": "Internet Archive",
                "install_method": "pkg_install",
                "tags": get_tags(base),
            }
        preview = next((file_url(THEMES_ITEM, base + extension)
                        for extension in (".jpg", ".jpeg", ".png") if base + extension in themes), "")
        if preview or not entry.get("preview_url"):
            entry["preview_url"] = preview
        entry.update(pkg_stub_url=file_url(THEMES_ITEM, stub),
                     pkg_payload_url=file_url(THEMES_ITEM, payload),
                     stub_size=file_size(themes[stub]), payload_size=file_size(themes[payload]))
        entries.append(entry)
    return entries, duplicate_count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", default="manifest/manifest.json", type=Path)
    parser.add_argument("--offline", action="store_true", help="Use the versioned seed only")
    args = parser.parse_args()
    seed = json.loads(SEED.read_text(encoding="utf-8"))
    if args.offline:
        entries, duplicates = seed["entries"], 0
    else:
        entries, duplicates = build_catalog(seed, fetch_index(AVATARS_ITEM), fetch_index(THEMES_ITEM))

    manifest = {"version": "1.0.1", "entries": entries}
    manifest["total_avatars"] = sum(entry["type"] == "avatar" for entry in entries)
    manifest["total_themes"] = len(entries) - manifest["total_avatars"]
    manifest["duplicates_removed"] = duplicates
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=args.output.parent,
                                         delete=False) as stream:
            temporary = Path(stream.name)
            json.dump(manifest, stream, indent=2, ensure_ascii=False)
            stream.write("\n")
        os.replace(temporary, args.output)
    finally:
        if temporary:
            temporary.unlink(missing_ok=True)
    print(f"{len(entries)} registros · {manifest['total_avatars']} avatares em pacotes · "
          f"{manifest['total_themes']} temas · {duplicates} pares duplicados removidos")


if __name__ == "__main__":
    main()
