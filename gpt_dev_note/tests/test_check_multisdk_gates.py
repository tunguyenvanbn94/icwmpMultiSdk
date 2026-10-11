"""No SDK/toolchain needed: fixture-only unit tests for the advisory preflight."""
import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "check_multisdk_gates.py"
spec = importlib.util.spec_from_file_location("check_multisdk_gates", SCRIPT)
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)


class PreflightTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.lib = self.root / module.LIB_REL
        self.app = self.root / module.APP_REL
        for base in (self.lib, self.app):
            (base / "sdk" / "mtk").mkdir(parents=True)
            (base / "sdk" / "bdk").mkdir(parents=True)
        (self.root / "tests" / "host").mkdir(parents=True)
        (self.root / "tests" / "host" / "run.sh").write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")

    def tearDown(self):
        self.tmp.cleanup()

    def test_clean_layout(self):
        self.assertEqual(module.inspect(self.root, ["mtk", "bdk"]), [])

    def test_missing_app_sdk_is_error(self):
        (self.app / "sdk" / "bdk").rmdir()
        codes = [x.code for x in module.inspect(self.root)]
        self.assertIn("SDK_PAIR_MISMATCH", codes)

    def test_required_sdk_missing(self):
        issues = module.inspect(self.root, ["uci"])
        self.assertIn("REQUIRED_SDK_MISSING", [x.code for x in issues])

    def test_bdk_sample_active_vs_comment(self):
        f = self.lib / "sdk" / "bdk" / "sdk.mk"
        f.write_text("# CFLAGS += -DBDK_SAMPLE_OBJECT\nCFLAGS += -O2 -DBDK_SAMPLE_OBJECT\n", encoding="utf-8")
        issues = [x for x in module.inspect(self.root) if x.code == "BDK_SAMPLE_IN_BUILD"]
        self.assertEqual(len(issues), 1)
        self.assertEqual(issues[0].line, 2)

    def test_sdk_macro_comment_ignored(self):
        t = self.lib / "tr098"
        t.mkdir()
        (t / "managementserver.c").write_text(
            "/* #ifdef DM_PLATFORM_BDK */\n// #ifdef DM_PLATFORM_BDK\n#ifdef DM_PLATFORM_BDK\n#endif\n",
            encoding="utf-8",
        )
        issues = [x for x in module.inspect(self.root) if x.code == "SDK_MACRO_IN_SHARED_MODEL"]
        self.assertEqual([x.line for x in issues], [3])

    def test_root_platform_hook_warning(self):
        (self.lib / "dmentry.c").write_text("void foo(void) { dm_platform_select_root(0); }\n", encoding="utf-8")
        issues = module.inspect(self.root)
        self.assertTrue(any(x.code == "ROOT_SELECTED_BY_SDK_HOOK" and x.severity == "WARN" for x in issues))

    def test_cli_strict_fails_and_report_generated(self):
        (self.lib / "sdk" / "bdk" / "sdk.mk").write_text("EXTRA_CFLAGS += -DBDK_SAMPLE_OBJECT\n", encoding="utf-8")
        jsonout = self.root / "preflight.json"
        mdout = self.root / "preflight.md"
        result = subprocess.run([sys.executable, str(SCRIPT), "--repo", str(self.root), "--strict",
                                 "--json-out", str(jsonout), "--md-out", str(mdout)],
                                check=False, capture_output=True, text=True)
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertTrue(jsonout.is_file())
        self.assertIn("BDK_SAMPLE_IN_BUILD", mdout.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
