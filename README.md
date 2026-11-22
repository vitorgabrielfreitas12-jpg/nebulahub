# ✦ NEBULA HUB

**The ultimate PS4 avatar and theme library**
Avatar packs · static and dynamic themes · DualShock 4 navigation

---

## What it is

NEBULA HUB is a native PS4 homebrew application (`.pkg`) you install on a jailbroken PS4.
It ships with a catalog of 25 entries. Browse and search avatar packs and themes,
download files, and submit installation requests from the preview screen.

---

## Content sources

| Source | Content | Count |
|---|---|---|
| `archive.org/ps4-jb-avatars` | Avatar FPKG packs | 1,100+ individual avatars |
| `archive.org/ps4-permanent-themes-repository` | Official PSN theme PKGs | 1,500+ themes |

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
  clang-18 lld-18 binutils make curl imagemagick fonts-dejavu-core dotnet-sdk-8.0
bash scripts/bootstrap-sdk.sh
make -j"$(nproc)"
make verify
```

The bootstrap checks the SHA-256 of the OpenOrbis 0.5.4 release, SDL_ttf 2.0.15
source and header, nlohmann/json 3.11.3, and a pinned LibOrbisPkg revision.
The SDK supplies the target libc++, SDL2, SDL_image, startup object and ELF
conversion tools. The bootstrap builds PkgTool.Core with .NET 8 for OpenSSL 3. SDL_ttf links against the SDK's
FreeType exports. The build creates `build/eboot.elf`, `build/eboot.oelf`,
`build/pkg/eboot.bin` and `nebulahub.pkg`. Verification reads the ELF and OELF,
checks the PKG and compares its extracted payload with the staged files.

## Catalog URL

The packaged manifest is available on first launch without a catalog server.
Set `MANIFEST_URL` in `src/nebula.h` to your HTTPS manifest URL to enable refreshes.
The catalog entries still need reachable media URLs for previews and downloads.

To generate a separate manifest from the archive listing:

```bash
python3 scripts/gen_manifest.py --output /tmp/nebula-manifest.json
```

## USING THE APP

| Button | Action |
|---|---|
| D-Pad | Navigate |
| ✕ | Select / confirm |
| ○ | Back |
| △ | Favorite / unfavorite |
| □ | Open search from a library or add the selected search character |
| L1 / R1 | Filter search results |
| OPTIONS | Back / quit |

**To install an avatar:**
1. Avatars → pick category → select item → ✕
2. Preview screen → DOWNLOAD & INSTALL → ✕
3. Wait for download (see Downloads screen)
4. Press ✕ again to copy a single avatar or submit a pack installation request. Apply individual avatars through **Avatar Changer (LAPY)**.

**To install a theme:**
1. Themes → pick category → select item → ✕
2. Preview screen → DOWNLOAD & INSTALL → ✕
3. Both PKG parts download automatically
4. Press ✕ again after both downloads to submit the installation request.
5. Go to **Settings → Themes → My Themes** to apply

---

## Runtime verification

Use a PS4 to check installation, launch, DualShock 4 input, fonts and previews,
download redirects and TLS, write permissions under `/data`, and persistence
across restart. For theme pairs and avatar packs, verify that the console
accepts the installation request and that the content appears after installation.
