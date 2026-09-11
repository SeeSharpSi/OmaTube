# Bundled Arch releases

`./bin/package-arch` creates one x86_64 `.pkg.tar.zst` for Omarchy/Arch.
It builds OmaTube with both existing players, collects a private runtime,
audits it, and invokes `makepkg`. It never installs the package or changes
user configuration. Run it as a regular user, not root.

## Builder requirements

Use a fully updated, consistent x86_64 Arch build environment. Do not use
partial upgrades or copy an old AppImage staging tree. The bundled Qt
plugins must match the Qt libraries used to compile the app.

The following are **maintainer build dependencies**, not extra packages
that OmaTube users must install:

```sh
sudo pacman -Syu --needed base-devel python patchelf licenses \
  qt6-base qt6-declarative qt6-webengine qt6-wayland qt6-imageformats \
  qt6-svg mpv libpulse pipewire pipewire-audio
```

The script checks required tools and runtime files and fails if any are
missing. Internet access is needed on the first build to fetch the pinned
standalone yt-dlp and Deno releases and their notices. Downloads are checked
against SHA-256 values in `packaging/arch/runtime.json` and cached in
`build-arch/cache`. It never downloads a moving `latest` release.

```sh
./bin/test
JOBS=4 ./bin/package-arch
```

Application builds and staging live in `build-arch`, separate from normal
development output. Each invocation rebuilds the app and staging tree;
download cache is retained. Invoke the script from any working directory.
`packaging/arch/PKGBUILD` is an internal recipe used by this command, not
a standalone AUR recipe. No AUR package or binary repository is published
by this workflow.

`VERSION` in `yt-client.pro` controls the version. Increase `pkgrel` in
`packaging/arch/PKGBUILD` when repackaging the same application version.
Output in `dist/` consists of:

- `omatube-<version>-<pkgrel>-x86_64.pkg.tar.zst`
- Package SHA-256 file with a relative filename, usable after downloading.
- Runtime manifest with hashes, upstream helper pins, original library
  paths, exact owning Arch package versions, and the glibc minimum.
- Builder package inventory for reproducing the build environment.

This is a repeatable build procedure, not a claim of bit-for-bit
reproducibility. Arch library inputs come from the builder. Retain the
exact package/source revisions and a clean build environment for a release.

## Runtime layout

The package installs `/opt/omatube/omatube` and symlinks `/usr/bin/omatube`
to it. Desktop metadata and the icon use standard `/usr/share` directories.
Everything application-specific is private to `/opt/omatube`:

- `bin/omatube`, `bin/yt-dlp` wrapper plus `lib/yt-dlp/` onedir, and `bin/deno`.
- Qt, libmpv, FFmpeg and transitive libraries in `lib`.
- QML modules, Qt plugins, WebEngine process/resources, audio client modules.
- Component notices and `/opt/omatube/share/manifest.json`.

The launcher scopes Qt, library, audio-module, and helper paths to this
runtime. It does not disable Chromium sandboxing, replace global libraries,
edit user PATH files, change saved playback settings, or invoke a helper
updater. Existing source builds continue to use their normal system runtime.

External package dependencies are deliberately limited to `glibc` at least
as recent as the builder, `bash`, `coreutils`, `libglvnd`,
`vulkan-icd-loader`, and `ca-certificates`. The desktop provides the kernel,
fonts, graphics drivers, Wayland/X11, and audio services. No package can
bundle a replacement GPU driver or kernel for arbitrary machines.

## Verification

Packaging audits all copied ELF dependencies by exact SONAME, rewrites
absolute library references on copies, and checks private relative RPATHs.
Opaque standalone helpers are not patched: changing a PyInstaller binary
can corrupt its appended archive. yt-dlp ships as an onedir tree
(`lib/yt-dlp/` plus a `bin/yt-dlp` wrapper) instead of a onefile binary:
onefile extracts ~90MB to `$TMPDIR` on every launch, so per-channel live
checks plus mpv playback accumulate stale `_MEI*` dirs and later runs fail
with `Failed to extract ... return code -1` once the tmpfs quota fills.
Onedir runs in place with no extraction. The wrapper and the app's helper
spawns strip the private `LD_LIBRARY_PATH`/`QT_*`/audio env so helpers use
their own libs plus system glibc. File hashes and symlink containment are
checked again during `makepkg`.

Run the small offline packaging regression suite independently with:

```sh
python3 tests/packaging_test.py
```

To inspect a package without installing it:

```sh
pacman -Qip dist/omatube-0.1.1-1-x86_64.pkg.tar.zst
mkdir -p build-arch/verify-package
bsdtar -xf dist/omatube-0.1.1-1-x86_64.pkg.tar.zst -C build-arch/verify-package
python3 packaging/arch/bundle.py audit build-arch/verify-package
QT_QPA_PLATFORM=offscreen build-arch/verify-package/opt/omatube/omatube \
  --automation --quit-after-startup
```

Use an empty extraction directory for every package version. Read
`AUTOMATION.md` before scripted UI tests. Automation uses disposable data,
fake playback, and no YouTube requests. These checks do not validate actual
video playback, hardware decoding, Chromium sandbox behavior, or every
GPU/audio combination. Before release, test installation, upgrade, removal,
both player backends, metadata lookup, and native Wayland on a clean Omarchy
VM or test machine without relying on developer-installed application tools.

## Publishing checklist

1. Complete `THIRD_PARTY_NOTICES.md`'s corresponding-source and license
   review. The script inventories inputs and copies available notices; it
   does not create a complete GPL/LGPL corresponding-source release.
2. Retain exact OmaTube sources, packaging scripts, upstream helper inputs,
   Arch library sources/build recipes/patches, and required notices. Publish
   the required corresponding source alongside the binary distribution.
3. Build and test on the documented Arch/Omarchy baseline. Note the glibc
   minimum from the manifest. An older installation may need an OS update.
4. Sign the package with the release signing key, publish the public key
   fingerprint through a trusted channel, and publish signatures, checksums,
   manifest, and source materials. Signing is not automated by this script.
5. Upload the artifacts to the project's release host. No credentials are
   needed for local builds. Never include an API key or user database.

Bundling makes OmaTube responsible for runtime security updates. Rebuild
for Qt WebEngine/Chromium security fixes and yt-dlp compatibility changes.
Update the pinned helper version, URL, checksum, and notices together, then
repeat package verification. Users upgrade by installing the new package;
the package does not self-modify or silently install another update service.
