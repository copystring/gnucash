# macOS GTK4 Dependency Tarball Rebuild Procedure

**Target:** `gnucash-future-mac-dependencies.tar.xz` (to replace `gnucash-5.15-mac-dependencies.tar.xz`)

**Reference:** AIW-27 WP-3.2, AIW-37 Platform & Test

---

## Current GTK3 Tarball (Baseline)

- **URL:** https://downloads.sourceforge.net/gnucash/Dependencies/gnucash-5.15-mac-dependencies.tar.xz
- **SHA256:** `15d14226e93cf18c60eb36d79686171a6b920ed1f72a815b8bad8f8755c76a02`
- **Contents:** GTK3, GLib, Guile 2.2, Gwenhywfar 5.x (GTK3), AqBanking 6.9, WebKit2GTK 4.0, Googletest, Boost, libofx, libdbi, sqlite, etc.
- **Used by:** `.github/workflows/mac-tests.yaml`

---

## Required GTK4 Tarball Components

| Component | Version | Source | Notes |
|-----------|---------|--------|-------|
| GTK | 4.14+ (LTS) or 4.17+ | Homebrew `gtk4` | 4.17 required for GtkSourceView 5 |
| GtkSourceView | 5.x | Homebrew `gtksourceview5` | Requires GTK 4.17+ |
| WebKitGTK | 4.1 | Homebrew `webkitgtk` | |
| Guile | 3.0 | Homebrew `guile` | |
| Gwenhywfar | 5.15+ (GTK4) | **Build from `branch-4`** | Not released; clone `aqbanking/gwenhywfar` branch-4 |
| AqBanking | 6.10+ | **Build from source** | Not released; build against Gwenhywfar GTK4 |
| Googletest | 1.14+ | Homebrew `googletest` | Or download from GitHub |
| Boost | 1.85+ | Homebrew `boost` | |
| libofx | Latest | Homebrew `libofx` | |
| libdbi | Latest | Homebrew `libdbi` | |
| sqlite | Latest | Homebrew `sqlite` | |
| gettext | Latest | Homebrew `gettext` | |
| pkg-config | Latest | Homebrew `pkg-config` | |
| cmake | Latest | Homebrew `cmake` | |
| ninja | Latest | Homebrew `ninja` | |

---

## Rebuild Procedure

### Prerequisites
- macOS 14+ (Sonoma) or 15+ (Sequoia)
- Xcode Command Line Tools
- Homebrew
- Write access to SourceForge `gnucash/Dependencies/` directory

### Step 1: Prepare Build Environment
```bash
# Create isolated build directory
export BUILD_ROOT="$HOME/gnucash-mac-deps-gtk4"
export PREFIX="$BUILD_ROOT/inst"
export SRCROOT="$BUILD_ROOT/source"
mkdir -p "$PREFIX" "$SRCROOT"
export PATH="$PREFIX/bin:$PATH"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PKG_CONFIG_PATH"
```

### Step 2: Install Base Dependencies via Homebrew
```bash
brew install ninja cmake pkg-config gettext gtk4 guile libofx libdbi sqlite webkitgtk googletest boost gtksourceview
# Verify GTK version
pkg-config --modversion gtk4  # Should be 4.14+ or 4.17+
```

### Step 3: Build Gwenhywfar GTK4 from Fork (`cs-202609-gtk4-gui`)
```bash
cd "$SRCROOT"
git clone --depth 1 --branch cs-202609-gtk4-gui https://github.com/copystring/gwenhywfar.git || \
  git clone --depth 1 --branch branch-4 https://github.com/aqbanking/gwenhywfar.git
cd gwenhywfar
if [ ! -f configure ]; then autoreconf -fiv; fi
./configure \
  --prefix="$PREFIX" \
  --with-guis="gtk4" \
  --disable-binaries \
  --disable-plugins
make -j$(sysctl -n hw.ncpu)
make install
# Verify
pkg-config --modversion gwengui-gtk4
```

### Step 4: Build AqBanking Against Gwenhywfar GTK4
```bash
cd "$SRCROOT"
git clone --depth 1 https://github.com/aqbanking/aqbanking.git
cd aqbanking
if [ ! -f configure ]; then autoreconf -fiv; fi
PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PKG_CONFIG_PATH" ./configure \
  --prefix="$PREFIX" \
  --with-gwen-dir="$PREFIX"
make -j$(sysctl -n hw.ncpu)
make install
# Verify
pkg-config --modversion aqbanking
```

### Step 5: Create Tarball
```bash
cd "$BUILD_ROOT"
tar -cJf gnucash-future-mac-dependencies.tar.xz -C inst .
# Compute SHA256
sha256sum gnucash-future-mac-dependencies.tar.xz
```

### Step 6: Upload to SourceForge
```bash
# Using rsync (requires SourceForge shell access)
rsync -avz gnucash-future-mac-dependencies.tar.xz \
  <username>@frs.sourceforge.net:/home/frs/project/gnucash/Dependencies/

# Or use SourceForge web UI: https://sourceforge.net/p/gnucash/admin/dependencies/
```

### Step 7: Update CI Configuration
Update `.github/workflows/mac-gtk4-tests.yml` and any CMake references:
- Update download URL to new tarball
- Update SHA256 checksum
- Update `GTK_MIN_VERSION` in CMake if needed

---

## Validation Checklist

- [ ] Tarball extracts cleanly into `$PREFIX`
- [ ] `pkg-config --modversion gtk4` returns 4.14+
- [ ] `pkg-config --modversion gwengui-gtk4` returns 5.15+ (from branch-4)
- [ ] `pkg-config --modversion aqbanking` returns 6.10+
- [ ] CMake configure succeeds: `cmake -GNinja -DCMAKE_PREFIX_PATH=$PREFIX ...`
- [ ] Build succeeds: `ninja`
- [ ] Tests pass: `ninja check`
- [ ] macOS app bundle created and launches without missing dylib errors

---

## Rollback Plan

If GTK4 tarball causes regressions:
1. Revert `.github/workflows/mac-gtk4-tests.yml` to use `gnucash-5.15-mac-dependencies.tar.xz`
2. Keep GTK4 tarball as separate file (`gnucash-future-mac-dependencies.tar.xz`)
3. CI can run GTK3 and GTK4 tests side-by-side using different tarballs
