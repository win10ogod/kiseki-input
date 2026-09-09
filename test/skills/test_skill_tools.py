"""Behavioral regressions for bundled skill helpers; no desktop input or recording."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "skills/kiseki-teach-recording/scripts"


def import_script(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


validator = import_script("validator", SCRIPTS / "validate_bundle.py")
control = import_script("control", SCRIPTS / "recording_control.py")
runtime = import_script("runtime", ROOT / "skills/kiseki-project/scripts/inspect_runtime.py")


class SkillToolsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="kiseki skills ")
        self.root = Path(self.temp.name)
        self.bundle = self.root / "bundle"
        self.bundle.mkdir()
        frame = {"index": 0, "timestampMs": 0, "path": "keyframes/frame.bmp", "width": 1, "height": 1}
        (self.bundle / "keyframes").mkdir()
        # Structural fixture only; image interpretation is separately live-tested.
        (self.bundle / frame["path"]).write_bytes(b"BM fixture")
        (self.bundle / "SKILL.md").write_text("# Test bundle reading guide\n", encoding="utf-8")
        events = [{"index": i, "timestampMs": i, "type": "mouse_move", "x": i, "y": 20} for i in range(105)]
        events[90].update(type="mouse_wheel", deltaY=120, unit="native", extraField="retain-me")
        events[104].update(type="key", keyCode=13, state="up", repeat=False)
        actions = [dict(event, actionIndex=i) for i, event in enumerate(events)]
        timeline = [{"kind": "keyframe", "frameIndex": 0, "path": frame["path"], "timestampMs": 0}]
        timeline += [{"kind": "action", "actionIndex": i, "eventIndex": i, "timestampMs": i} for i in range(len(actions))]
        self.write("manifest.json", {"kind": "kiseki-teach-recording", "schemaVersion": 2,
                   "format": "agivar-style-action-keyframe-bundle", "title": 'Save: "quoted" # task',
                   "frameCount": 1, "keyframeCount": 1, "actionCount": len(actions), "eventCount": len(events),
                   "actualDurationMs": 105, "keyframes": [frame], "media": {}, "warnings": []})
        self.write("frames.json", {"schemaVersion": 1, "frames": [frame]})
        self.write("actions.json", {"schemaVersion": 1, "actions": actions})
        self.write("timeline.json", {"schemaVersion": 2, "items": timeline})
        self.write("annotations.json", {"schemaVersion": 1, "annotations": []})
        (self.bundle / "events.jsonl").write_text("\n".join(json.dumps(e) for e in events), encoding="utf-8")

    def tearDown(self):
        self.temp.cleanup()

    def write(self, name, value):
        (self.bundle / name).write_text(json.dumps(value), encoding="utf-8")

    def read(self, name):
        return json.loads((self.bundle / name).read_text(encoding="utf-8"))

    def run_script(self, script, *args):
        return subprocess.run([sys.executable, str(SCRIPTS / script), *map(str, args)],
                              capture_output=True, text=True, encoding="utf-8", cwd=self.root)

    def test_valid_bundle(self):
        summary, errors, _ = validator.validate_bundle(self.bundle)
        self.assertEqual(errors, [])
        self.assertTrue(summary["ok"])
        self.assertEqual(summary["actions"], 105)

    def test_malformed_json_and_wrong_shapes_produce_json_error(self):
        for broken in ('{', '[]', '{"kind": 12}'):
            with self.subTest(broken=broken):
                (self.bundle / "manifest.json").write_text(broken, encoding="utf-8")
                result = self.run_script("validate_bundle.py", self.bundle)
                self.assertEqual(result.returncode, 2)
                self.assertFalse(json.loads(result.stdout)["ok"])
                self.assertNotIn("Traceback", result.stderr)

    def test_wrong_array_and_nonobject_items(self):
        for value in (None, {}, [42]):
            self.write("actions.json", {"schemaVersion": 1, "actions": value})
            summary, errors, _ = validator.validate_bundle(self.bundle)
            self.assertFalse(summary["ok"])
            self.assertTrue(errors)

    def test_missing_media_and_path_escape(self):
        manifest = self.read("manifest.json")
        for name in ("media/missing.wav", "../outside.wav"):
            manifest["media"] = {"audio": name}
            self.write("manifest.json", manifest)
            self.assertFalse(validator.validate_bundle(self.bundle)[0]["ok"])

    def test_unknown_timeline_and_annotation_references(self):
        timeline = self.read("timeline.json")
        timeline["items"][-1]["eventIndex"] = 999
        self.write("timeline.json", timeline)
        self.write("annotations.json", {"schemaVersion": 1, "annotations": [{"frameIndex": 999, "text": "wrong"}]})
        _, errors, _ = validator.validate_bundle(self.bundle)
        self.assertTrue(any("event index" in e for e in errors))
        self.assertTrue(any("annotation" in e for e in errors))

    def test_polling_warning_is_preserved_without_rejecting_bundle(self):
        manifest = self.read("manifest.json")
        manifest["eventCaptureMode"] = "incomplete-polling-fallback"
        self.write("manifest.json", manifest)
        summary, errors, warnings = validator.validate_bundle(self.bundle)
        self.assertTrue(summary["ok"])
        self.assertFalse(errors)
        self.assertTrue(any("polling" in w for w in warnings))

    def test_draft_preserves_late_wheel_motion_and_fields(self):
        result = self.run_script("draft_skill_from_bundle.py", self.bundle, "--output-root", self.root / "skills", "--name", "save-flow")
        self.assertEqual(result.returncode, 0, result.stderr)
        skill = self.root / "skills/save-flow"
        evidence = (skill / "references/teaching-evidence.md").read_text(encoding="utf-8")
        records = [json.loads(line.split(": `", 1)[1][:-1]) for line in evidence.splitlines() if line.startswith("- #")]
        self.assertEqual(records, self.read("actions.json")["actions"])
        self.assertIn(str(self.bundle.resolve()), evidence)
        description = (skill / "SKILL.md").read_text(encoding="utf-8").split("description: ", 1)[1].splitlines()[0]
        self.assertIn('Save: "quoted" # task', json.loads(description))
        display = (skill / "agents/openai.yaml").read_text(encoding="utf-8").split("display_name: ", 1)[1].splitlines()[0]
        self.assertEqual(json.loads(display), 'Save: "quoted" # task')

    def test_explicit_excerpt_reports_coverage(self):
        result = self.run_script("draft_skill_from_bundle.py", self.bundle, "--output-root", self.root, "--name", "excerpt", "--action-limit", 5)
        self.assertEqual(result.returncode, 0, result.stderr)
        evidence = (self.root / "excerpt/references/teaching-evidence.md").read_text(encoding="utf-8")
        self.assertIn("Included 5 of 105", evidence)
        self.assertIn("explicitly requested excerpt", evidence)

    def test_invalid_bundle_writes_no_draft(self):
        (self.bundle / "frames.json").unlink()
        result = self.run_script("draft_skill_from_bundle.py", self.bundle, "--output-root", self.root, "--name", "invalid")
        self.assertEqual(result.returncode, 2)
        self.assertFalse((self.root / "invalid").exists())

    def test_existing_skill_and_invocation_policy_are_preserved(self):
        skill = self.root / "existing"
        (skill / "agents").mkdir(parents=True)
        metadata = 'policy:\n  allow_implicit_invocation: false\n'
        (skill / "agents/openai.yaml").write_text(metadata, encoding="utf-8")
        args = (self.bundle, "--output-root", self.root, "--name", "existing")
        self.assertEqual(self.run_script("draft_skill_from_bundle.py", *args).returncode, 2)
        self.assertEqual(self.run_script("draft_skill_from_bundle.py", *args, "--overwrite").returncode, 0)
        self.assertEqual((skill / "agents/openai.yaml").read_text(encoding="utf-8"), metadata)

    def active_state(self):
        state = self.root / "active.json"
        stop = self.bundle / ".kiseki-teach-stop"
        state.write_text(json.dumps({"schemaVersion": 1, "kind": "kiseki-teach-active-recording", "pid": os.getpid(),
                         "outputDirectory": str(self.bundle), "stopFile": str(stop)}), encoding="utf-8")
        return state, stop

    def test_status_is_read_only_and_stop_is_idempotent(self):
        state, stop = self.active_state()
        self.assertEqual(control.control("status", state, self.bundle)["state"], "active-state-present")
        self.assertFalse(stop.exists())
        control.control("stop", state, self.bundle)
        request = stop.read_bytes()
        control.control("stop", state, self.bundle)
        self.assertEqual(stop.read_bytes(), request)
        state.unlink()
        stop.unlink()
        self.assertTrue(control.control("stop", state, self.bundle)["finalized"])
        self.assertFalse(stop.exists())

    def test_stop_rejects_other_bundle_and_preserves_pending_recording(self):
        state, stop = self.active_state()
        with self.assertRaises(ValueError):
            control.control("stop", state, self.root / "other")
        self.assertFalse(stop.exists())
        result = control.control("stop", state, self.bundle, wait_ms=1)
        self.assertFalse(result["ok"])
        self.assertTrue(state.exists())
        self.assertEqual(result["state"], "stop-requested")

    def test_missing_recorder_does_not_create_files(self):
        with self.assertRaises(ValueError):
            control.control("stop", self.root / "missing-state", self.root / "missing-bundle")
        self.assertFalse((self.root / "missing-bundle").exists())

    def test_exact_missing_executable_has_no_silent_fallback(self):
        result = runtime.inspect(ROOT, str(self.root / "missing"), probe=True)
        self.assertFalse(result["ok"])
        self.assertEqual(result["checks"], {})
        self.assertEqual(result["executable"], str((self.root / "missing").resolve()))

    def test_runtime_probe_preserves_native_error_output(self):
        executable = self.root / "kiseki.exe"
        executable.touch()
        failed = subprocess.CompletedProcess([], 2, "not JSON", "native backend error")
        with patch.object(runtime.subprocess, "run", return_value=failed):
            result = runtime.inspect(ROOT, str(executable), probe=True)
        self.assertFalse(result["ok"])
        self.assertEqual(result["checks"]["modes"]["exitCode"], 2)
        self.assertEqual(result["checks"]["modes"]["stderr"], "native backend error")


if __name__ == "__main__":
    unittest.main()
