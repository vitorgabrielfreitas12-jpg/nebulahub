# ✦ NEBULA HUB

**The ultimate PS4 avatar and theme library**
Avatar packs · static and dynamic themes · DualShock 4 navigation

---

## What it is

NEBULA HUB is a native PS4 homebrew application (`.pkg`) you install on a jailbroken PS4.
It ships with an offline catalog of 876 entries. Browse and search avatar packs and themes,
download files, and submit installation requests from the preview screen.

---

## Content sources

| Source | Entries in the packaged catalog |
|---|---|
| `archive.org/ps4-jb-avatars` | Four avatar-pack records, three indexed downloads |
| `archive.org/ps4-permanent-themes-repository` | 872 theme records, 854 indexed pairs |

File names and sizes come from the public archive indexes. An indexed file may
still fail to download due to availability, network policy or console TLS.

---

## BUILD ON MOBILE — Step by Step (GitHub Actions, no PC needed)

This is the **recommended path** if you're on Android.

### Step 1 — Fork / create the repo

1. Open [github.com](https://github.com) in your phone browser
2. Sign in or create a free account
3. Tap **+** (top right) → **New repository**
4. Name it `nebulahub`
5. Set it to **Public**
6. Tap **Create repository**

### Step 2 — Upload the project files

1. On GitHub, tap **Add file** → **Upload files**
2. Upload this entire zip contents (you can upload the zip itself or extract first)
   - On Android: use **Files by Google** or **ZArchiver** to extract, then upload the folder contents
3. Tap **Commit changes**

   Alternatively use **GitHub Mobile** app (free on Play Store):
   - Open the repo → tap Files tab → upload

### Step 3 — Trigger the build

1. In your repo, tap the **Actions** tab
2. If you see "I understand my workflows…" — tap **Enable**
3. Tap **Build NEBULA HUB PKG** in the left sidebar
4. Tap **Run workflow** → **Run workflow** (green button)
5. Wait 3–8 minutes

### Step 4 — Download your PKG

1. Once the build goes green (✅), tap it
2. Scroll to **Artifacts** at the bottom
3. Tap **nebulahub-pkg**
4. It downloads as a `.zip` — extract it to get `nebulahub.pkg`

### Step 5 — Install on PS4

Copy `nebulahub.pkg` to the root of an exFAT USB drive and select it in GoldHEN's
Package Installer. Installation and launch on firmware 14.00 require a compatible
GoldHEN setup and validation on that console.

## Local build

Use x86-64 Ubuntu 24.04 with Clang and LLD 18. GitHub Actions runs the same commands.

```bash
sudo apt-get update
sudo apt-get install --no-install-recommends \
  clang-18 lld-18 binutils make g++ libsdl2-2.0-0 python3 curl \
  imagemagick fonts-dejavu-core dotnet-sdk-8.0
bash scripts/bootstrap-sdk.sh
make -j"$(nproc)"
make test
```

The bootstrap checks the SHA-256 of the OpenOrbis 0.5.4 release, SDL_ttf 2.0.15
source and header, nlohmann/json 3.11.3, and a pinned LibOrbisPkg revision.
The SDK supplies the target libc++, SDL2, stb_image, startup object and ELF
conversion tools. The bootstrap builds PkgTool.Core with .NET 8 for OpenSSL 3. SDL_ttf links against the SDK's
FreeType exports. The build creates `build/eboot.elf`, `build/eboot.oelf`,
`build/pkg/eboot.bin` and `nebulahub.pkg`. Verification reads the ELF and OELF,
checks the PKG and compares its extracted payload with the staged files.

## Catalog updates

To update the catalog without rebuilding the app, place a one-line HTTPS URL
in `/data/NEBULAHUB/manifest-url.txt` and restart the app. Use a direct URL to a
JSON document you are authorized to publish, for example a file on your own
GitHub Pages site. No endpoint is configured by default. The Settings screen
shows the last update result without displaying the configured URL.

The app starts from the bundled manifest or the last valid remote cache.
It validates the new manifest in the background, then applies it when the
Home screen is active. Favorites are keyed by stable IDs, including favorites
whose records are temporarily absent. The download cap is 16 MiB, and the
manifest cap is 10,000 entries. An empty, malformed or duplicate-ID update
leaves the previous catalog in place.

Generate a new manifest separately before publishing it:

```bash
python3 scripts/gen_manifest.py --output /tmp/nebula-manifest.json
```

The generator pairs exact `_1.pkg` and `_2.pkg` names, retains IDs from the seed,
and uses archive sizes and exact name matches for previews. Unclassified
standalone PKGs require independent validation before being added as
`pkg_theme` entries. Use `--offline` to export just the reviewed seed.

## USING THE APP

| Button | Action |
|---|---|
| D-Pad / left stick | Navigate |
| ✕ | Select / confirm |
| ○ | Back |
| △ | Favorite / unfavorite |
| □ | Open search from a library or add the selected search character |
| L1 / R1 | Filter search results |
| OPTIONS | Back / quit |

**To install an avatar:**
1. Avatars → pick category → select item → ✕
2. Preview screen → DOWNLOAD → ✕
3. Wait for download (see Downloads screen)
4. Select INSTALL to validate the local file and submit the installation request. For example, the Anime pack is a PS4 application PKG with category `gd` and title ID `LAPY90015`. Apply individual avatars through **Avatar Changer (LAPY)**. An individual `.xavatar` entry uses the separate copy flow.

**To install a theme:**
1. Themes → pick category → select item → ✕
2. Preview screen → DOWNLOAD → ✕
3. Wait for both downloads when the theme has two components.
4. Select INSTALL to validate the files and submit part 1 or the standalone package.
5. For a pair, wait until the PS4 confirms part 1, then select INSTALL again to submit part 2 in the same app session.
6. Check **Settings → Themes → My Themes** after the console finishes.

---

## Runtime verification

Use a PS4 to check installation, launch, DualShock 4 input, fonts and previews,
download redirects and TLS, write permissions under `/data`, and persistence
across restart. For theme pairs and avatar packs, verify that the console
accepts the installation request and that the content appears after installation.


The six packaged PNGs are original category illustrations. Three catalog
entries link to matching JPEGs in the archive. Other entries show a fallback.
Decoding and HTTP run outside the render loop.

Transfers use verified EOF and a temporary file before publishing. The app
reads Content-Length from the actual GET when supplied. Pause, cancellation
or an interrupted connection leaves no completed download. Resume starts a
fresh transfer because partial HTTP responses are not combined without
validator and range support.

If an operation fails on the console, copy `/data/NEBULAHUB/diagnostics.log`
and record the exact system error and whether it happened during Download or
Install. Installation requests are submitted separately after local validation.


### Download diagnostics

`Queued` means the transfer is waiting behind another file. `Validating` follows
the transfer and precedes publication of the final file. `Completed` means the
file passed the app's structural and role checks. The PS4 installation service
still decides if the submitted package can be installed. Removing finished
history keeps the final file. Cancelled or interrupted transfers leave only
temporary files, which the app cleans up on cancellation or next launch.

In Settings, **Keep failed download for diagnostics** opts in for the current
session. The app can retain one fully transferred sample rejected by package
validation, up to 192 MiB, as `/data/NEBULAHUB/diagnostic-rejected.bin`. A new
sample replaces the previous one. **Delete rejected sample** shows the stored
size and requests deletion. The install flow only reads the normal download
paths. Logs omit the source URL path and query to protect credentials.

For a short console test, the first part of **Azure Earth Orbital Remastered -
Dynamic** is 1 MiB. It should reach Completed before the second part starts.
Only submit installation after both parts pass. For remote preview testing use
**TEKKEN 7**, **PAC-MAN Championship Edition 2** or **TERA - Anniversary**.
Other entries may show an illustration while loading, when no individual preview
is listed, or after a preview failure.
