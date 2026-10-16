# Linux Distro GTK4 Dependency Tracking

**Reference:** AIW-27 WP-3.4, AIW-37 Platform & Test

---

## Distro Status Matrix

| Distro | Release | GTK4 | GtkSourceView 5 | WebKitGTK 4.1 | Gwenhywfar GTK4 | AqBanking 6.10 | Action |
|--------|---------|------|-----------------|---------------|-----------------|----------------|--------|
| **Ubuntu** | 24.04 LTS | 4.14 ✅ | ❌ | ❌ (4.0) | ❌ | ❌ | File ITP; backport gtksourceview5 |
| **Ubuntu** | 24.10 | 4.16 ✅ | 5.22 ✅ | 4.1 ✅ | ❌ | ❌ | Best CI baseline; file ITP for Gwen/AqB |
| **Ubuntu** | 25.04 (dev) | 4.16+ ✅ | 5.22+ ✅ | 4.1 ✅ | ❌ | ❌ | Track |
| **Fedora** | 40 | 4.16 ✅ | 5.22 ✅ | 4.1 ✅ | ❌ | ❌ | File review request |
| **Fedora** | 41 (dev) | 4.16+ ✅ | 5.22+ ✅ | 4.1 ✅ | ❌ | ❌ | Track |
| **Debian** | 13 (Trixie) | 4.14+ ✅ | 5.22+ ✅ | 4.1 ✅ | ❌ | ❌ | File ITP (#107xxxx) |
| **Debian** | 12 (Bookworm) | 4.10 ✅ | ❌ | ❌ | ❌ | ❌ | Backports needed |
| **Arch** | Rolling | 4.16+ ✅ | 5.22+ ✅ | 4.1 ✅ | AUR only | AUR only | Monitor AUR; contribute PKGBUILD |
| **openSUSE** | Tumbleweed | 4.16+ ✅ | 5.22+ ✅ | 4.1 ✅ | ❌ | ❌ | File OBS request |
| **Alpine** | 3.20+ | 4.14+ ✅ | 5.22+ ✅ | 4.1 ✅ | ❌ | ❌ | File APKBUILD request |

---

## CI Baseline Recommendation

**Primary:** Ubuntu 24.10 (or 24.04 with gtksourceview5 backport)
- GTK 4.14+ (LTS)
- GtkSourceView 5 available
- WebKitGTK 4.1 available
- Most common CI runner

**Secondary:** Fedora 40+ (for RPM-based validation)
- Newer packages typically
- Good for catching distro-specific issues

**Tertiary:** Arch (rolling, latest upstream)
- Sanity check against latest GTK4
- Not for production CI (rolling breaks)

---

## Gwenhywfar/AqBanking Packaging Status

### Upstream Tracking
| Project | Repo | GTK4 Branch | Last Release | GTK4 Release Target |
|---------|------|-------------|--------------|---------------------|
| Gwenhywfar | `aqbanking/gwenhywfar` | `branch-4` (commit `40e8521f`, May 2026) | 5.14.1 (GTK3) | 5.15+ (unreleased) |
| AqBanking | `aqbanking/aqbanking` | Follows Gwenhywfar | 6.9.2 (GTK3) | 6.10+ (unreleased) |

### Distro Packaging Actions Required

#### Debian/Ubuntu (ITP - Intent To Package)
```bash
# For Gwenhywfar GTK4
# File bug at https://bugs.debian.org/ with title:
# "ITP: gwenhywfar -- GTK4 GUI support (gwengui-gtk4)"
# Reference: upstream branch-4, target 5.15+

# For AqBanking GTK4
# File bug: "ITP: aqbanking -- GTK4 support (requires gwenhywfar-gtk4)"
# Depends on Gwenhywfar GTK4 ITP
```

#### Fedora (Bugzilla Review Request)
```
# Component: Package Review
# Summary: Review Request: gwenhywfar-gui-gtk4 - GTK4 backend for Gwenhywfar
# Spec file: Add subpackage %package gui-gtk4, BuildRequires: gtk4-devel
```

#### Arch Linux (AUR PKGBUILD)
```bash
# Maintain/update AUR packages:
# gwenhywfar-gtk4-git (tracks branch-4)
# aqbanking-gtk4-git (tracks master with GTK4)
```

---

## Monitoring Checklist

- [ ] Check `aqbanking/gwenhywfar` weekly for `5.15+` tag
- [ ] Check Debian `unstable` for `libgwengui-gtk4-dev` availability
- [ ] Check Ubuntu archive for `libgtksourceview-5-dev` backport to 24.04
- [ ] When Gwenhywfar releases: submit packaging requests to Debian/Fedora/openSUSE
- [ ] Switch CI from building from source to distro packages once available

---

## Escalation Triggers

| Condition | Threshold | Action |
|-----------|-----------|--------|
| Gwenhywfar 5.15+ not released by 2027-01-15 | 2.5 months before G4 gate | Escalate to CEO/board: decide whether to offer packaging assistance or fork |
| Ubuntu 24.04 gtksourceview5 not backported | At CI setup time | Use Ubuntu 24.10 runner or build from source |
| WebKitGTK 4.1 not in MSYS2 by 2027-02-01 | At Windows packaging | Build custom MSYS2 package in custom repo |

---

## Technical Details: Fork Build Instructions (Current State)

Until Gwenhywfar 5.15+ is released, the CI workflows build from source:

```bash
# Build Gwenhywfar GTK4 from fork or branch-4
git clone --depth 1 --branch cs-202609-gtk4-gui https://github.com/copystring/gwenhywfar.git || \
  git clone --depth 1 --branch branch-4 https://github.com/aqbanking/gwenhywfar.git
cd gwenhywfar
autoreconf -fiv
./configure --prefix=/path/to/inst --with-guis="gtk4" --disable-binaries --disable-plugins
make -j$(nproc)
make install

# Build AqBanking against Gwenhywfar GTK4
git clone --depth 1 https://github.com/aqbanking/aqbanking.git
cd aqbanking
autoreconf -fiv
PKG_CONFIG_PATH=/path/to/inst/lib/pkgconfig:$PKG_CONFIG_PATH ./configure \
  --prefix=/path/to/inst --with-gwen-dir=/path/to/inst
make -j$(nproc)
make install
```
