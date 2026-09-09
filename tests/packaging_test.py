"""Offline regression checks for the Arch bundler and private launcher."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
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
        "libexec/QtWebEngineProcess",
        "share/qt6/translations/qtwebengine_locales/en-US.pak",
        "plugins/platforms/libqoffscreen.so", "plugins/platforms/libqwayland.so",
        "plugins/platforms/libqxcb.so", "plugins/sqldrivers/libqsqlite.so",
    ] + [f"share/qt6/resources/{name}" for name in bundle.RESOURCES] \
        + [f"plugins/{name}" for name in bundle.WAYLAND_PLUGINS]
    for relative in required:
        write(opt / relative)
    for relative in ("omatube", "bin/omatube", "bin/yt-dlp", "bin/deno",
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
