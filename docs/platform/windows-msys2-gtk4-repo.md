# Windows MSYS2 GTK4 Custom Repository Rebuild Procedure

**Target:** `gnc-ucrt64-gtk4-YYYYMMDD` tag in `gnucash-windows-deps-repo`

**Reference:** AIW-27 WP-3.3, AIW-37 Platform & Test

---

## Current State

- **Existing repo:** `gnucash-windows-deps-repo` (custom pacman repository)
- **Current tag:** `gnc-ucrt64-gtk4-reviewed-20260905-be306f4c` (GTK4 review tag)
- **CI workflow:** `.github/workflows/windows-gtk4-tests.yml`
- **MSYS2 environment:** UCRT64 (`mingw-w64-ucrt-x86_64-*` packages)

---

## Required Packages for GTK4 Stack

| Package | Status in MSYS2 | Action Required |
|---------|-----------------|-----------------|
| `mingw-w64-ucrt-x86_64-gtk4` | ✅ Available (4.14+) | Use as-is |
| `mingw-w64-ucrt-x86_64-gtksourceview5` | ❌ Not packaged | **Build from source** |
| `mingw-w64-ucrt-x86_64-webkit2gtk-4.1` | ❌ Not packaged | **Build from source** |
| `mingw-w64-ucrt-x86_64-gwengui-gtk4` | ❌ Not packaged | **Build from Gwenhywfar `branch-4`** |
| `mingw-w64-ucrt-x86_64-aqbanking6.10` | ❌ Not packaged | **Build from AqBanking master** |
| `mingw-w64-ucrt-x86_64-guile` | ✅ Available (3.0) | Use as-is |
| `mingw-w64-ucrt-x86_64-boost` | ✅ Available | Use as-is |
| `mingw-w64-ucrt-x86_64-libofx` | ✅ Available | Use as-is |
| `mingw-w64-ucrt-x86_64-libdbi` | ✅ Available | Use as-is |
| `mingw-w64-ucrt-x86_64-libdbi-driver-sqlite3` | ✅ Available | Use as-is |
| `mingw-w64-ucrt-x86_64-googletest` | ✅ Available | Use as-is |
| `mingw-w64-ucrt-x86_64-python` | ✅ Available | Use as-is |

---

## Rebuild Procedure

### Prerequisites
- Windows 10/11 or Linux with MSYS2 UCRT64
- MSYS2 installed with `base-devel`, `mingw-w64-ucrt-x86_64-toolchain`
- Write access to `gnucash-windows-deps-repo` (or fork)
- GPG key for package signing (if repo uses signed packages)

### Step 1: Prepare MSYS2 Build Environment
```bash
# In MSYS2 UCRT64 terminal
pacman -Syu --noconfirm
pacman -S --needed --noconfirm \
  base-devel \
  mingw-w64-ucrt-x86_64-toolchain \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-gtk4 \
  mingw-w64-ucrt-x86_64-guile \
  mingw-w64-ucrt-x86_64-boost \
  mingw-w64-ucrt-x86_64-libofx \
  mingw-w64-ucrt-x86_64-libdbi \
  mingw-w64-ucrt-x86_64-libdbi-driver-sqlite3 \
  mingw-w64-ucrt-x86_64-googletest \
  mingw-w64-ucrt-x86_64-python \
  mingw-w64-ucrt-x86_64-pkg-config \
  mingw-w64-ucrt-x86_64-gettext
```

### Step 2: Build GtkSourceView 5
```bash
# GtkSourceView 5 requires GTK4
git clone --depth 1 https://gitlab.gnome.org/GNOME/gtksourceview.git
cd gtksourceview
meson setup build \
  --prefix=/ucrt64 \
  -Dintrospection=disabled \
  -Dvapi=false \
  -Ddocs=false
ninja -C build install
# Package with makepkg-mingw if publishing to custom repo
```

### Step 3: Build Gwenhywfar GTK4 from Fork (`cs-202609-gtk4-gui`)
```bash
git clone --depth 1 --branch cs-202609-gtk4-gui https://github.com/copystring/gwenhywfar.git || \
  git clone --depth 1 --branch branch-4 https://github.com/aqbanking/gwenhywfar.git
cd gwenhywfar
if [ ! -f configure ]; then autoreconf -fiv; fi
./configure \
  --prefix=/ucrt64 \
  --with-guis="gtk4" \
  --disable-binaries \
  --disable-plugins
make -j$(nproc)
make install
```

### Step 4: Build AqBanking Against Gwenhywfar GTK4
```bash
git clone --depth 1 https://github.com/aqbanking/aqbanking.git
cd aqbanking
if [ ! -f configure ]; then autoreconf -fiv; fi
./configure \
  --prefix=/ucrt64 \
  --with-gwen-dir=/ucrt64
make -j$(nproc)
make install
```

### Step 5: WebKitGTK 4.1 Assessment
**Status:** WebKitGTK 4.1 build on Windows is complex and resource-intensive (~4 hours, requires 16GB+ RAM).

**Recommended Approach:**
1. **Short term (for CI):** Build without WebKit (`-D WITH_WEBKIT=OFF` in CMake) or use WebView2 (`-D WITH_WEBVIEW2=ON` — native Windows edge/chromium)
2. **Medium term (for distribution):** Set up dedicated build agent to produce `mingw-w64-ucrt-x86_64-webkit2gtk-4.1` package
3. **Upstream direction:** GnuCash on Windows is moving toward WebView2 (AIW-27 notes `WEBVIEW2_INCLUDE_DIR` already in CMake)

### Step 6: Create Pacman Repository Database
```bash
# In directory with built packages (.pkg.tar.zst)
repo-add gnc-ucrt64-gtk4.db.tar.gz *.pkg.tar.zst
# Sign if required
# gpg --detach-sign gnc-ucrt64-gtk4.db.tar.gz
```

### Step 7: Push to Custom Repository
```bash
# Tag the release
git tag gnc-ucrt64-gtk4-$(date +%Y%m%d)
git push origin --tags
# Upload packages to GitHub Release assets or hosting server
```

### Step 8: Update CI to Use Custom Repository
In `.github/workflows/windows-gtk4-tests.yml`:
```yaml
- name: Add Custom GTK4 Repo
  run: |
    echo "[gnc-ucrt64-gtk4]" >> /etc/pacman.conf
    echo "SigLevel = Optional TrustAll" >> /etc/pacman.conf
    echo "Server = https://github.com/copystring/gnucash-windows-deps-repo/releases/download/gnc-ucrt64-gtk4-LATEST" >> /etc/pacman.conf
    pacman -Sy
```

---

## Windows IME Assertion Mitigation

**Issue:** In GTK4 on Windows, IME (Input Method Editor) can trigger assertions in `gtk_im_context_simple_filter_keypress` when handling non-ASCII keyboard input.

**Mitigation in Custom Package:**
1. Check if patch is available upstream in GTK4: https://gitlab.gnome.org/GNOME/gtk/-/issues/
2. If not, apply workaround in GnuCash: set `GTK_IM_MODULE=simple` or initialize IME context with explicit null check
3. Document in release notes for Windows users

---

## Verification Checklist

- [ ] All required packages present in repo DB
- [ ] `pacman -Sy mingw-w64-ucrt-x86_64-gtk4` succeeds
- [ ] `pkg-config --modversion gwengui-gtk4` returns 5.15+ (from branch-4)
- [ ] `pkg-config --modversion aqbanking` returns 6.10+
- [ ] GnuCash CMake configure succeeds in MSYS2 UCRT64
- [ ] GnuCash build completes without missing symbol errors
- [ ] Basic tests pass (`ninja check`)
