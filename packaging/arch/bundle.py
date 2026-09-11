#!/usr/bin/env python3
"""Collect an Arch-built private runtime and audit it without host fallbacks."""
import argparse
import glob
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import urllib.request
import zipfile


HOST_ALLOW = frozenset({
    "ld-linux-x86-64.so.2", "libc.so.6", "libm.so.6", "libpthread.so.0",
    "libdl.so.2", "librt.so.1", "libresolv.so.2", "libutil.so.1",
    "libanl.so.1", "libmvec.so.1", "libGL.so.1", "libGLX.so.0",
    "libEGL.so.1", "libOpenGL.so.0", "libGLdispatch.so.0", "libvulkan.so.1",
})
QML_TREES = ("QtQml", "QtQuick", "QtWebChannel", "QtWebEngine")
WAYLAND_PLUGINS = (
    "wayland-shell-integration/libxdg-shell.so",
    "wayland-graphics-integration-client/libqt-plugin-wayland-egl.so",
)
PLUGINS = (
    "platforms/libqoffscreen.so", "platforms/libqwayland.so",
    "platforms/libqxcb.so", "sqldrivers/libqsqlite.so",
    "iconengines/libqsvgicon.so", "tls", "xcbglintegrations",
    "imageformats/libqgif.so", "imageformats/libqico.so",
    "imageformats/libqjpeg.so", "imageformats/libqsvg.so",
    "imageformats/libqwebp.so",
) + WAYLAND_PLUGINS
OPTIONAL_PLUGINS = (
    "wayland-decoration-client", "egldeviceintegrations", "networkinformation",
)
RESOURCES = (
    "qtwebengine_resources.pak", "qtwebengine_resources_100p.pak",
    "qtwebengine_resources_200p.pak", "qtwebengine_devtools_resources.pak",
    "v8_context_snapshot.bin",
)
EXPLICIT_LIBS = (
    "libssl.so.3", "libcrypto.so.3", "libnssckbi.so", "libsoftokn3.so",
    "libfreebl3.so", "libfreeblpriv3.so",
)
OPAQUE = {"bin/yt-dlp", "bin/deno"}
# Self-contained upstream helper trees (PyInstaller onedir). Their ELFs link
# against their own _internal directory and system glibc, not the private Qt
# runtime, so patchelf and the private-lib closure must leave them alone.
# bin/yt-dlp is a small wrapper script; the real tree lives under lib/yt-dlp/.
OPAQUE_PREFIXES = ("lib/yt-dlp/",)


def is_opaque(relative_posix):
    return relative_posix in OPAQUE or relative_posix.startswith(OPAQUE_PREFIXES)


def run(*cmd):
    result = subprocess.run([str(c) for c in cmd], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"{' '.join(map(str, cmd))}: {result.stderr.strip()}")
    return result.stdout


def sha256_file(path):
    with open(path, "rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def is_elf(path):
    with open(path, "rb") as stream:
        return stream.read(4) == b"\x7fELF"


def dt_needed(path):
    return re.findall(r"\(NEEDED\)\s+Shared library: \[(.*?)\]",
                      run("readelf", "-d", path))


def rpath_of(path):
    values = re.findall(r"\((?:RUNPATH|RPATH)\)\s+Library .*?: \[(.*?)\]",
                        run("readelf", "-d", path))
    return [entry for value in values for entry in value.split(":")]


def ldd_map(path):
    """Resolve on original builder files, preserving their original RPATHs."""
    mapping = {}
    for line in run("ldd", path).splitlines():
        match = re.match(r"\s*(\S+) => (\S+)", line)
        if match:
            name, target = match.groups()
            mapping[Path(name).name] = None if target == "not" else target
        else:
            match = re.match(r"\s*(/\S+) \(", line)
            if match:
                target = match.group(1)
                mapping[Path(target).name] = target
    return mapping


def fetch_verify(url, dest, digest):
    dest = Path(dest)
    if dest.exists():
        if sha256_file(dest) != digest:
            raise RuntimeError(f"checksum mismatch in cache: {dest}")
        return
    temporary = dest.with_suffix(dest.suffix + ".part")
    with urllib.request.urlopen(url, timeout=120) as response, temporary.open("wb") as out:
        shutil.copyfileobj(response, out)
    if sha256_file(temporary) != digest:
        temporary.unlink()
        raise RuntimeError(f"checksum mismatch: {url}")
    temporary.replace(dest)


def symlink_issues(opt):
    opt = Path(opt).resolve()
    issues = []
    for root, dirs, files in os.walk(opt):
        for name in dirs + files:
            path = Path(root, name)
            if path.is_symlink():
                try:
                    target = path.resolve(strict=True)
                    if not target.is_relative_to(opt):
                        issues.append(f"symlink escapes package: {path}")
                except (OSError, RuntimeError):
                    issues.append(f"dangling/cyclic symlink: {path}")
    return issues


def elf_closure_issues(opt, check_rpath=True):
    opt = Path(opt).resolve()
    lib = opt / "lib"
    have = {p.name for p in lib.iterdir() if p.is_file() and is_elf(p)} if lib.is_dir() else set()
    issues = []
    count = 0
    for path in sorted(opt.rglob("*")):
        if not path.is_file() or path.is_symlink() or not is_elf(path):
            continue
        if is_opaque(path.relative_to(opt).as_posix()):
            continue
        count += 1
        for needed in dt_needed(path):
            if "/" in needed and check_rpath:
                issues.append(f"absolute DT_NEEDED {needed} in {path}")
            elif Path(needed).name not in have | HOST_ALLOW:
                issues.append(f"unresolved {needed} needed by {path}")
        if check_rpath:
            paths = rpath_of(path)
            if not paths:
                issues.append(f"missing RPATH: {path}")
            for entry in paths:
                if not (entry == "$ORIGIN" or entry.startswith("$ORIGIN/")):
                    issues.append(f"absolute/foreign RPATH {entry} in {path}")
                elif Path(entry.replace("$ORIGIN", str(path.parent))).resolve() != lib:
                    issues.append(f"RPATH {entry} misses private lib: {path}")
    return issues, count


def audit(root):
    root = Path(root).resolve()
    opt = root / "opt/omatube" if (root / "opt/omatube").is_dir() else root
    issues = symlink_issues(opt)
    required = [
        "omatube", "bin/omatube", "bin/yt-dlp", "bin/deno", "bin/qt.conf",
        "lib/yt-dlp/yt-dlp_linux",
        "lib/yt-dlp/_internal/Cryptodome/Cipher/_ARC4.abi3.so",
        "libexec/QtWebEngineProcess", "share/manifest.json",
        "share/qt6/translations/qtwebengine_locales/en-US.pak",
        "plugins/platforms/libqoffscreen.so", "plugins/platforms/libqwayland.so",
        "plugins/platforms/libqxcb.so", "plugins/sqldrivers/libqsqlite.so",
    ] + [f"share/qt6/resources/{name}" for name in RESOURCES] \
        + [f"plugins/{name}" for name in WAYLAND_PLUGINS]
    for relative in required:
        if not (opt / relative).is_file():
            issues.append(f"required file missing: {relative}")
    for relative in ("omatube", "bin/omatube", "bin/yt-dlp", "bin/deno",
                     "lib/yt-dlp/yt-dlp_linux",
                     "libexec/QtWebEngineProcess"):
        if not os.access(opt / relative, os.X_OK):
            issues.append(f"required executable not executable: {relative}")
    for tree in QML_TREES:
        directory = opt / "qml" / tree
        if not directory.is_dir() or not any(directory.iterdir()):
            issues.append(f"required QML tree missing/empty: {tree}")
    entries = []
    manifest = opt / "share/manifest.json"
    if manifest.is_file():
        entries = json.loads(manifest.read_text())["files"]
        paths = set()
        for entry in entries:
            relative = entry["path"]
            path = opt / relative
            if path.is_absolute() and not path.resolve().is_relative_to(opt):
                issues.append(f"manifest path escapes package: {relative}")
                continue
            if relative in paths:
                issues.append(f"duplicate manifest path: {relative}")
            paths.add(relative)
            if not path.is_file() or path.is_symlink():
                issues.append(f"manifest file missing: {relative}")
            elif sha256_file(path) != entry["sha256"]:
                issues.append(f"manifest hash mismatch: {relative}")
        actual = {p.relative_to(opt).as_posix() for p in opt.rglob("*") if p.is_file()}
        if paths != actual - {"share/manifest.json"}:
            issues.append("manifest does not cover exactly the runtime files")
    if not entries:
        issues.append("manifest has no files")
    closure, count = elf_closure_issues(opt)
    issues.extend(closure)
    if (root / "usr").is_dir():
        link = root / "usr/bin/omatube"
        if not link.is_symlink() or os.readlink(link) != "/opt/omatube/omatube":
            issues.append("usr/bin/omatube link wrong/missing")
        for relative in ("usr/share/applications/omatube.desktop",
                         "usr/share/icons/hicolor/scalable/apps/omatube.svg"):
            if not (root / relative).is_file():
                issues.append(f"package file missing: {relative}")
    if issues:
        raise RuntimeError("audit failed:\n" + "\n".join(issues))
    print(f"Audit passed: {count} ELFs, {len(entries)} manifest entries")


class Bundler:
    def __init__(self, args):
        self.args = args
        self.opt = Path(args.out).resolve() / "opt/omatube"
        self.lib = self.opt / "lib"
        self.files = {}
        self.seeds = set()
        self.origins = {}
        self.owners = {}
        self.versions = dict(line.split(None, 1) for line in run("pacman", "-Q").splitlines())
        self.skipped = []

    def copy_file(self, source, target, provenance=None, track_seed=True):
        source = Path(source).resolve(strict=True)
        target = Path(target)
        if target.parent == self.lib:
            previous = self.origins.get(target.name)
            if previous and previous != source:
                raise RuntimeError(f"conflicting sources for {target.name}: {previous}, {source}")
            self.origins[target.name] = source
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        shutil.copymode(source, target)
        if track_seed and is_elf(target):
            self.seeds.add(source)
        if provenance:
            owner, version, origin = provenance
        else:
            if source not in self.owners:
                try:
                    self.owners[source] = run("pacman", "-Qqo", source).splitlines()[0]
                except (RuntimeError, IndexError):
                    if not source.is_relative_to(Path(self.args.root).resolve()):
                        raise RuntimeError(f"unowned builder input, cannot record provenance: {source}") from None
                    self.owners[source] = "omatube"
            owner = self.owners[source]
            version = self.versions.get(owner, self.args.version)
            origin = str(source)
        relative = target.relative_to(self.opt).as_posix()
        self.files[relative] = {"path": relative, "source": origin,
                                "owner": owner, "owner_version": version}

    def copy_tree(self, source, target, license_files=False):
        top = Path(source).resolve(strict=True)

        def visit(src, dst, ancestors):
            real = src.resolve(strict=True)
            # Arch license files can point to shared documentation outside
            # their package directory. Copy their contents, never the link.
            shared_notice = license_files and real.is_file() and real.is_relative_to(Path("/usr/share"))
            if not real.is_relative_to(top) and not shared_notice:
                raise RuntimeError(f"source symlink escapes tree: {src}")
            if real.is_dir():
                if real in ancestors:
                    raise RuntimeError(f"cyclic source symlink: {src}")
                for child in sorted(real.iterdir()):
                    visit(child, dst / child.name, ancestors | {real})
            elif real.is_file():
                self.copy_file(real, dst)
            else:
                raise RuntimeError(f"unsupported source: {src}")

        visit(Path(source), Path(target), set())

    def install_onedir_helper(self, helper, cached):
        """Unpack a PyInstaller onedir zip once; avoid per-run /tmp extraction.

        The legacy onefile binary extracts ~90MB to $TMPDIR on every launch.
        OmaTube spawns yt-dlp for each channel, so stale _MEI dirs accumulate
        and later runs fail with 'Failed to extract ... return code -1'
        (zlib Z_ERRNO: TMPDIR write failed: quota/full). The onedir zip runs
        in place with no extraction, fixing live checks and mpv playback for
        every install, not just one machine.
        """
        name = helper["name"]
        executable = helper.get("executable", name)
        provenance = ("bundled-helper", helper["version"], helper["url"])
        target_dir = self.opt / "lib" / name
        if target_dir.exists():
            raise RuntimeError(f"output already exists: {target_dir}")
        extract_dir = Path(self.args.cache) / f"{name}-{helper['version']}.extracted"
        if extract_dir.exists():
            shutil.rmtree(extract_dir)
        extract_dir.mkdir(parents=True)
        with zipfile.ZipFile(cached) as archive:
            infos = archive.infolist()
            if not infos:
                raise RuntimeError(f"empty helper archive: {cached}")
            tops = set()
            for info in infos:
                filename = info.filename
                if not filename or filename.startswith("/") or ".." in Path(filename).parts:
                    raise RuntimeError(f"unsafe helper archive entry: {filename}")
                mode = (info.external_attr >> 16) & 0o170000
                if mode == 0o120000:
                    raise RuntimeError(f"helper archive symlink unsupported: {filename}")
                tops.add(Path(filename).parts[0])
                target = extract_dir / filename
                if info.is_dir():
                    target.mkdir(parents=True, exist_ok=True)
                else:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with archive.open(info) as stream, target.open("wb") as out:
                        shutil.copyfileobj(stream, out)
                    file_mode = (info.external_attr >> 16) & 0o7777
                    target.chmod(file_mode or 0o644)
            if executable not in tops or "_internal" not in tops:
                raise RuntimeError(f"unexpected {name} onedir layout: {sorted(tops)}")
            if sorted(tops) != sorted([executable, "_internal"]):
                raise RuntimeError(f"unexpected {name} onedir top level: {sorted(tops)}")
        main_source = extract_dir / executable
        if not main_source.is_file():
            raise RuntimeError(f"helper executable missing in archive: {executable}")
        for root, _, files in os.walk(extract_dir):
            for filename in files:
                source = Path(root, filename)
                target = target_dir / source.relative_to(extract_dir)
                # Self-contained upstream tree: never seed the private-lib
                # closure (its _internal libs are not in private lib/).
                self.copy_file(source, target, provenance, track_seed=False)
        main_target = target_dir / executable
        main_target.chmod(0o755)
        self.write_helper_wrapper(helper)
        shutil.rmtree(extract_dir)

    def write_helper_wrapper(self, helper):
        """PATH-stable wrapper that runs the helper without private Qt env."""
        name = helper["name"]
        executable = helper.get("executable", name)
        wrapper = self.opt / "bin" / name
        text = (
            "#!/bin/sh\n"
            f"# Bundled {name} onedir wrapper. Resolves via PATH for the app\n"
            "# and the mpv ytdl hook; runs without the private Qt/audio env so\n"
            "# the helper uses its own _internal libs plus system glibc.\n"
            "set -eu\n"
            "HERE=\"$(dirname \"$(readlink -f \"$0\")\")\"\n"
            "unset LD_LIBRARY_PATH QT_PLUGIN_PATH QML2_IMPORT_PATH QML_IMPORT_PATH "
            "QT_QPA_PLATFORM_PLUGIN_PATH QTWEBENGINEPROCESS_PATH "
            "QTWEBENGINE_RESOURCES_PATH QTWEBENGINE_LOCALES_PATH "
            "SPA_PLUGIN_DIR PIPEWIRE_MODULE_DIR\n"
            f"exec \"$HERE/../lib/{name}/{executable}\" \"$@\"\n"
        )
        wrapper.parent.mkdir(parents=True, exist_ok=True)
        wrapper.write_text(text)
        wrapper.chmod(0o755)
        relative = wrapper.relative_to(self.opt).as_posix()
        self.files[relative] = {"path": relative, "source": f"generated:{name}-wrapper",
                                "owner": "omatube", "owner_version": self.args.version}

    def run_bundle(self, runtime):
        query = lambda key: Path(run("qmake6", "-query", key).strip())
        plugins = query("QT_INSTALL_PLUGINS")
        qml = query("QT_INSTALL_QML")
        data = query("QT_INSTALL_DATA")
        translations = query("QT_INSTALL_TRANSLATIONS")
        inputs = [(Path(self.args.app), self.opt / "bin/omatube")]
        inputs += [(plugins / name, self.opt / "plugins" / name) for name in PLUGINS]
        inputs += [(qml / name, self.opt / "qml" / name) for name in QML_TREES]
        inputs += [(data / "resources" / name, self.opt / "share/qt6/resources" / name)
                   for name in RESOURCES]
        inputs += [
            (query("QT_INSTALL_LIBEXECS") / "QtWebEngineProcess", self.opt / "libexec/QtWebEngineProcess"),
            (translations / "qtwebengine_locales/en-US.pak",
             self.opt / "share/qt6/translations/qtwebengine_locales/en-US.pak"),
            (Path("/usr/lib/spa-0.2"), self.lib / "spa-0.2"),
            (Path("/usr/lib/pipewire-0.3"), self.lib / "pipewire-0.3"),
        ]
        inputs += [(Path("/usr/lib") / name, self.lib / name) for name in EXPLICIT_LIBS]
        pulse = sorted(glob.glob("/usr/lib/pulseaudio/libpulsecommon-*.so"))
        if not pulse:
            raise RuntimeError("missing builder PulseAudio client library")
        inputs += [(Path(path), self.lib / Path(path).name) for path in pulse]
        for name in OPTIONAL_PLUGINS:
            if (plugins / name).is_dir():
                inputs.append((plugins / name, self.opt / "plugins" / name))
            else:
                self.skipped.append(f"optional plugin absent: {name}")
        if (data / "resources/icudtl.dat").is_file():
            inputs.append((data / "resources/icudtl.dat", self.opt / "share/qt6/resources/icudtl.dat"))
        missing = [str(src) for src, _ in inputs if not src.exists()]
        if missing:
            raise RuntimeError("missing builder inputs:\n" + "\n".join(missing))
        if self.opt.exists():
            raise RuntimeError(f"output already exists, use an empty staging directory: {self.opt}")
        self.lib.mkdir(parents=True)
        for source, target in inputs:
            if source.is_dir():
                self.copy_tree(source, target)
            else:
                self.copy_file(source, target)

        cache = Path(self.args.cache)
        for helper in runtime["helpers"]:
            cached = cache / f"{helper['name']}-{helper['version']}"
            fetch_verify(helper["url"], cached, helper["sha256"])
            if helper.get("format") == "onedir-zip":
                self.install_onedir_helper(helper, cached)
                continue
            source = cached
            if helper["name"] == "deno":
                source = cached.with_name(cached.name + ".bin")
                with zipfile.ZipFile(cached) as archive:
                    if archive.namelist() != [helper["entry"]]:
                        raise RuntimeError("unexpected Deno archive contents")
                    with archive.open(helper["entry"]) as stream, source.open("wb") as out:
                        shutil.copyfileobj(stream, out)
            target = self.opt / "bin" / helper["name"]
            self.copy_file(source, target, ("bundled-helper", helper["version"], helper["url"]))
            target.chmod(0o755)
        for notice in runtime["licenses"]:
            cached = cache / f"LICENSE-{notice['name']}-{notice['sha256']}"
            fetch_verify(notice["url"], cached, notice["sha256"])
            self.copy_file(cached, self.opt / "licenses" / notice["name"] / "LICENSE",
                           ("upstream-notice", "", notice["url"]))

        # Inspect originals, never relocated copies whose builder RPATH is lost.
        queue = list(self.seeds)
        seen = set()
        while queue:
            original = queue.pop()
            if original in seen:
                continue
            seen.add(original)
            for name, source in ldd_map(original).items():
                if name in HOST_ALLOW:
                    continue
                if source is None:
                    raise RuntimeError(f"unresolved {name} needed by {original}")
                real = Path(source).resolve(strict=True)
                if name in self.origins:
                    if self.origins[name] != real:
                        raise RuntimeError(f"conflicting sources for {name}")
                    continue
                self.copy_file(real, self.lib / name)
                queue.append(real)

        # Collect notices after closure so transitive library owners are included.
        owners = {entry["owner"] for entry in self.files.values()}
        for owner in sorted(owners - {"omatube", "bundled-helper", "upstream-notice"}):
            directory = Path("/usr/share/licenses") / owner
            if directory.is_dir():
                self.copy_tree(directory, self.opt / "licenses" / owner, license_files=True)
            else:
                self.skipped.append(f"no Arch package-specific license directory: {owner}")
        self.copy_tree(Path("/usr/share/licenses/spdx"), self.opt / "licenses/spdx")
        project = Path(self.args.root)
        for name in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
            self.copy_file(project / name, self.opt / "licenses/omatube" / name)
        self.copy_file(project / "packaging/arch/omatube", self.opt / "omatube")
        (self.opt / "omatube").chmod(0o755)
        self.copy_file(project / "packaging/arch/qt.conf", self.opt / "bin/qt.conf")
        issues, _ = elf_closure_issues(self.opt, check_rpath=False)
        if issues:
            raise RuntimeError("closure incomplete:\n" + "\n".join(issues))
        for path in sorted(self.opt.rglob("*")):
            if not path.is_file() or not is_elf(path):
                continue
            if is_opaque(path.relative_to(self.opt).as_posix()):
                continue
            for needed in dt_needed(path):
                if "/" in needed:
                    run("patchelf", "--replace-needed", needed, Path(needed).name, path)
            relative = os.path.relpath(self.lib, path.parent)
            run("patchelf", "--force-rpath", "--set-rpath",
                "$ORIGIN" if relative == "." else "$ORIGIN/" + relative, path)
        for entry in self.files.values():
            entry["sha256"] = sha256_file(self.opt / entry["path"])
        manifest = {
            "version": self.args.version, "arch": platform.machine(),
            "qt_version": run("qmake6", "-query", "QT_VERSION").strip(),
            "glibc_min": self.versions["glibc"].rsplit("-", 1)[0],
            "host_allowlist": sorted(HOST_ALLOW), "helpers": runtime["helpers"],
            "skipped_optional": self.skipped,
            "files": sorted(self.files.values(), key=lambda entry: entry["path"]),
        }
        text = json.dumps(manifest, indent=2) + "\n"
        (self.opt / "share/manifest.json").write_text(text)
        (Path(self.args.meta) / "manifest.json").write_text(text)
        (Path(self.args.meta) / "builder-packages.txt").write_text(
            "".join(f"{name} {version}\n" for name, version in sorted(self.versions.items())))
        audit(self.opt)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    build = commands.add_parser("bundle")
    for name in ("app", "out", "cache", "runtime", "meta", "version", "root"):
        build.add_argument("--" + name, required=True)
    commands.add_parser("audit").add_argument("root")
    args = parser.parse_args()
    try:
        if args.command == "audit":
            audit(args.root)
        else:
            for tool in ("readelf", "ldd", "patchelf", "pacman", "qmake6"):
                if not shutil.which(tool):
                    raise RuntimeError(f"missing builder tool: {tool}")
            Path(args.cache).mkdir(parents=True, exist_ok=True)
            Path(args.meta).mkdir(parents=True, exist_ok=True)
            Bundler(args).run_bundle(json.loads(Path(args.runtime).read_text()))
    except (RuntimeError, OSError, ValueError, KeyError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
