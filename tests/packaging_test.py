"""Offline regression checks for the Arch bundler and private launcher."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import types
import unittest
import zipfile
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("bundle", ROOT / "packaging/arch/bundle.py")
bundle = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bundle)


def write(path, data=b"payload", executable=False):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    if executable:
        path.chmod(0o755)
    return path


def seed_runtime(opt):
    required = [
        "omatube", "bin/omatube", "bin/yt-dlp", "bin/deno", "bin/qt.conf",
        "lib/yt-dlp/yt-dlp_linux",
        "lib/yt-dlp/_internal/Cryptodome/Cipher/_ARC4.abi3.so",
        "libexec/QtWebEngineProcess",
        "share/qt6/translations/qtwebengine_locales/en-US.pak",
        "plugins/platforms/libqoffscreen.so", "plugins/platforms/libqwayland.so",
        "plugins/platforms/libqxcb.so", "plugins/sqldrivers/libqsqlite.so",
    ] + [f"share/qt6/resources/{name}" for name in bundle.RESOURCES] \
        + [f"plugins/{name}" for name in bundle.WAYLAND_PLUGINS]
    for relative in required:
        write(opt / relative)
    for relative in ("omatube", "bin/omatube", "bin/yt-dlp", "bin/deno",
                     "lib/yt-dlp/yt-dlp_linux",
                     "libexec/QtWebEngineProcess"):
        (opt / relative).chmod(0o755)
    for tree in bundle.QML_TREES:
        write(opt / "qml" / tree / "qmldir")
    files = [{"path": path.relative_to(opt).as_posix(),
              "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
             for path in sorted(opt.rglob("*")) if path.is_file()]
    (opt / "share/manifest.json").write_text(json.dumps({"files": files}))


class PackagingTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.opt = self.root / "opt"
        self.lib = self.opt / "lib"
        self.lib.mkdir(parents=True)

    def elf_issues(self, needed, rpath=("$ORIGIN/../lib",)):
        write(self.opt / "bin/app", b"\x7fELFfake")
        with mock.patch.object(bundle, "dt_needed", return_value=needed), \
                mock.patch.object(bundle, "rpath_of", return_value=rpath):
            return bundle.elf_closure_issues(self.opt)[0]

    def test_exact_soname_required(self):
        write(self.lib / "libfoo.so.3")
        issues = self.elf_issues(["libfoo.so.2"])
        self.assertTrue(any("unresolved libfoo.so.2" in issue for issue in issues))

    def test_library_outside_search_path_does_not_satisfy_dependency(self):
        write(self.opt / "extra/libfoo.so.2")
        issues = self.elf_issues(["libfoo.so.2"])
        self.assertTrue(any("unresolved libfoo.so.2" in issue for issue in issues))

    def test_non_elf_file_cannot_satisfy_library_dependency(self):
        write(self.lib / "libfoo.so.2", b"not a shared library")
        issues = self.elf_issues(["libfoo.so.2"])
        self.assertTrue(any("unresolved libfoo.so.2" in issue for issue in issues))

    def test_absolute_dependency_rejected(self):
        write(self.lib / "libfoo.so.2")
        issues = self.elf_issues(["/usr/lib/libfoo.so.2"])
        self.assertTrue(any("absolute DT_NEEDED" in issue for issue in issues))

    def test_rpath_must_resolve_to_private_lib(self):
        self.assertEqual(self.elf_issues(["libc.so.6"]), [])
        for rpath in ("/usr/lib", "$ORIGIN", "$ORIGIN_SUFFIX/../lib"):
            with self.subTest(rpath=rpath):
                issues = self.elf_issues(["libc.so.6"], [rpath])
                self.assertTrue(any("RPATH" in issue for issue in issues))

    def test_escaping_file_and_directory_symlinks_rejected(self):
        outside = self.root / "outside"
        write(outside / "file")
        (self.opt / "bad-file").symlink_to(outside / "file")
        (self.opt / "bad-directory").symlink_to(outside, target_is_directory=True)
        self.assertEqual(len(bundle.symlink_issues(self.opt)), 2)

    def test_copy_rejects_escaping_symlinks(self):
        source = self.root / "source"
        write(source / "file")
        outside = write(self.root / "outside/file")
        builder = bundle.Bundler.__new__(bundle.Bundler)
        for target in (outside, outside.parent):
            with self.subTest(target=target):
                link = source / "bad"
                link.symlink_to(target)
                with self.assertRaisesRegex(RuntimeError, "escapes tree"):
                    builder.copy_tree(source, self.opt / "copy")
                link.unlink()

    def test_cache_mismatch_fails_before_network(self):
        cached = write(self.root / "cached", b"wrong bytes")
        with mock.patch.object(bundle.urllib.request, "urlopen") as network:
            with self.assertRaisesRegex(RuntimeError, "cache"):
                bundle.fetch_verify("https://example.invalid/file", cached, "0" * 64)
            network.assert_not_called()

    def test_valid_manifest_accepted(self):
        seed_runtime(self.opt)
        bundle.audit(self.opt)

    def test_wayland_integration_required(self):
        seed_runtime(self.opt)
        (self.opt / "plugins" / bundle.WAYLAND_PLUGINS[0]).unlink()
        with self.assertRaisesRegex(RuntimeError, "required file missing.*libxdg-shell"):
            bundle.audit(self.opt)

    def test_manifest_tampering_detected(self):
        seed_runtime(self.opt)
        (self.opt / "bin/qt.conf").write_bytes(b"tampered")
        with self.assertRaisesRegex(RuntimeError, "hash mismatch"):
            bundle.audit(self.opt)

    def test_manifest_traversal_detected(self):
        seed_runtime(self.opt)
        manifest = self.opt / "share/manifest.json"
        data = json.loads(manifest.read_text())
        data["files"].append({"path": "../outside", "sha256": "0" * 64})
        manifest.write_text(json.dumps(data))
        with self.assertRaisesRegex(RuntimeError, "escapes package"):
            bundle.audit(self.opt)

    def test_unlisted_payload_detected(self):
        seed_runtime(self.opt)
        write(self.opt / "stowaway")
        with self.assertRaisesRegex(RuntimeError, "cover exactly"):
            bundle.audit(self.opt)

    def make_builder(self, cache_dir):
        builder = bundle.Bundler.__new__(bundle.Bundler)
        builder.opt = self.opt
        builder.lib = self.lib
        builder.files = {}
        builder.seeds = set()
        builder.origins = {}
        builder.args = types.SimpleNamespace(
            cache=str(cache_dir), version="0.1.0", root=str(ROOT))
        return builder

    def make_onedir_zip(self, path, extra_tops=(), with_symlink=False,
                        with_traversal=False):
        with zipfile.ZipFile(path, "w") as archive:
            archive.writestr("yt-dlp_linux", b"#!/bin/sh\nexit 0\n")
            archive.writestr(
                "_internal/Cryptodome/Cipher/_ARC4.abi3.so", b"fake-so")
            archive.writestr("_internal/libz.so.1", b"fake-lib")
            for top in extra_tops:
                archive.writestr(f"{top}/file", b"extra")
            if with_traversal:
                archive.writestr("../escape", b"bad")
            if with_symlink:
                info = zipfile.ZipInfo("_internal/link")
                info.external_attr = (0o120777 << 16)
                archive.writestr(info, b"target")
        return path

    def test_opaque_helper_tree_skipped_by_closure(self):
        write(self.lib / "libfoo.so.2")
        write(self.opt / "lib/yt-dlp/_internal/fake.so", b"\x7fELFfake")
        write(self.opt / "lib/yt-dlp/yt-dlp_linux", b"\x7fELFfake")
        with mock.patch.object(bundle, "dt_needed", return_value=["libfoo.so.2"]), \
                mock.patch.object(bundle, "rpath_of", return_value=[]):
            issues, _ = bundle.elf_closure_issues(self.opt)
        self.assertFalse(any("yt-dlp" in issue for issue in issues))

    def test_onedir_install_creates_wrapper_and_manifest(self):
        cache = self.root / "cache"
        cache.mkdir()
        zipped = self.make_onedir_zip(cache / "yt-dlp-2026.08.19")
        builder = self.make_builder(cache)
        helper = {"name": "yt-dlp", "version": "2026.08.19",
                  "url": "https://example.invalid/yt-dlp_linux.zip",
                  "format": "onedir-zip", "executable": "yt-dlp_linux"}
        builder.install_onedir_helper(helper, zipped)
        main = self.opt / "lib/yt-dlp/yt-dlp_linux"
        self.assertTrue(main.is_file())
        self.assertTrue(os.access(main, os.X_OK))
        canary = self.opt / "lib/yt-dlp/_internal/Cryptodome/Cipher/_ARC4.abi3.so"
        self.assertTrue(canary.is_file())
        wrapper = self.opt / "bin/yt-dlp"
        self.assertTrue(wrapper.is_file())
        self.assertTrue(os.access(wrapper, os.X_OK))
        self.assertEqual(builder.seeds, set())
        paths = {entry["path"] for entry in builder.files.values()}
        self.assertIn("lib/yt-dlp/yt-dlp_linux", paths)
        self.assertIn("lib/yt-dlp/_internal/Cryptodome/Cipher/_ARC4.abi3.so", paths)
        self.assertIn("bin/yt-dlp", paths)

    def test_onedir_wrapper_cleans_private_env_and_forwards_args(self):
        cache = self.root / "cache"
        cache.mkdir()
        probe = b'#!/bin/sh\n/usr/bin/env\nprintf "ARG:%s\\n" "$@"\n'
        with zipfile.ZipFile(cache / "yt-dlp-2026.08.19", "w") as archive:
            archive.writestr("yt-dlp_linux", probe)
            archive.writestr(
                "_internal/Cryptodome/Cipher/_ARC4.abi3.so", b"fake-so")
        builder = self.make_builder(cache)
        helper = {"name": "yt-dlp", "version": "2026.08.19",
                  "url": "https://example.invalid/yt-dlp_linux.zip",
                  "format": "onedir-zip", "executable": "yt-dlp_linux"}
        builder.install_onedir_helper(helper, cache / "yt-dlp-2026.08.19")
        wrapper = self.opt / "bin/yt-dlp"
        environment = dict(os.environ, LD_LIBRARY_PATH="/hostile",
                           QT_PLUGIN_PATH="/hostile",
                           QML2_IMPORT_PATH="/hostile",
                           QML_IMPORT_PATH="/hostile",
                           QT_QPA_PLATFORM_PLUGIN_PATH="/hostile",
                           QTWEBENGINEPROCESS_PATH="/hostile",
                           QTWEBENGINE_RESOURCES_PATH="/hostile",
                           QTWEBENGINE_LOCALES_PATH="/hostile",
                           SPA_PLUGIN_DIR="/hostile",
                           PIPEWIRE_MODULE_DIR="/hostile")
        result = subprocess.run(
            [str(wrapper), "first argument", "--dump-json"],
            capture_output=True, text=True, env=environment, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        values = dict(line.split("=", 1) for line in lines if "=" in line)
        for key in ("LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QML2_IMPORT_PATH",
                    "QML_IMPORT_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH",
                    "QTWEBENGINEPROCESS_PATH", "QTWEBENGINE_RESOURCES_PATH",
                    "QTWEBENGINE_LOCALES_PATH", "SPA_PLUGIN_DIR",
                    "PIPEWIRE_MODULE_DIR"):
            self.assertNotIn(key, values)
        self.assertIn("ARG:first argument", lines)
        self.assertIn("ARG:--dump-json", lines)

    def test_onedir_rejects_unexpected_layout(self):
        cache = self.root / "cache"
        cache.mkdir()
        builder = self.make_builder(cache)
        helper = {"name": "yt-dlp", "version": "2026.08.19",
                  "url": "https://example.invalid/yt-dlp_linux.zip",
                  "format": "onedir-zip", "executable": "yt-dlp_linux"}
        bad = self.make_onedir_zip(cache / "bad-extra", extra_tops=("README",))
        with self.assertRaisesRegex(RuntimeError, "top level"):
            builder.install_onedir_helper(helper, bad)
        traversal = self.make_onedir_zip(
            cache / "bad-traversal", with_traversal=True)
        with self.assertRaisesRegex(RuntimeError, "unsafe"):
            builder.install_onedir_helper(helper, traversal)
        symlink = self.make_onedir_zip(
            cache / "bad-symlink", with_symlink=True)
        with self.assertRaisesRegex(RuntimeError, "symlink"):
            builder.install_onedir_helper(helper, symlink)

    def test_audit_requires_onedir_canary(self):
        seed_runtime(self.opt)
        (self.opt / "lib/yt-dlp/_internal/Cryptodome/Cipher/_ARC4.abi3.so").unlink()
        with self.assertRaisesRegex(RuntimeError, "required file missing.*_ARC4"):
            bundle.audit(self.opt)

    def test_launcher_environment_and_spaced_arguments(self):
        private = self.root / "directory with spaces/opt/omatube"
        launcher = write(private / "omatube", (ROOT / "packaging/arch/omatube").read_bytes(), True)
        write(private / "bin/omatube", b'#!/bin/sh\n/usr/bin/env\nprintf "ARG:%s\\n" "$@"\n', True)
        for name in ("yt-dlp", "deno"):
            write(private / "bin" / name, b"#!/bin/sh\nexit 0\n", True)
        environment = dict(os.environ, PATH="/hostile:/usr/bin:/bin", LD_LIBRARY_PATH="/hostile",
                           QT_PLUGIN_PATH="/hostile", QML2_IMPORT_PATH="/hostile",
                           QML_IMPORT_PATH="/hostile", QT_QPA_PLATFORM_PLUGIN_PATH="/hostile",
                           QT_QPA_PLATFORMTHEME="hostile", QT_QPA_PLATFORM="offscreen")
        result = subprocess.run([str(launcher), "first argument", "second argument"],
                                capture_output=True, text=True, env=environment, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        values = dict(line.split("=", 1) for line in lines if "=" in line)
        self.assertTrue(values["PATH"].startswith(f"{private}/bin:"))
        expected = {
            "QT_PLUGIN_PATH": "plugins", "QML2_IMPORT_PATH": "qml", "QML_IMPORT_PATH": "qml",
            "QT_QPA_PLATFORM_PLUGIN_PATH": "plugins/platforms",
            "QTWEBENGINEPROCESS_PATH": "libexec/QtWebEngineProcess",
            "QTWEBENGINE_RESOURCES_PATH": "share/qt6/resources",
            "QTWEBENGINE_LOCALES_PATH": "share/qt6/translations/qtwebengine_locales",
            "SPA_PLUGIN_DIR": "lib/spa-0.2", "PIPEWIRE_MODULE_DIR": "lib/pipewire-0.3",
            "LD_LIBRARY_PATH": "lib",
        }
        for name, relative in expected.items():
            self.assertEqual(values[name], str(private / relative))
        self.assertEqual(values["QT_QPA_PLATFORM"], "offscreen")
        self.assertNotIn("QT_QPA_PLATFORMTHEME", values)
        self.assertIn("ARG:first argument", lines)
        self.assertIn("ARG:second argument", lines)


if __name__ == "__main__":
    unittest.main()
