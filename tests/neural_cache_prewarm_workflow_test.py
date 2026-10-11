"""Offline tests for the existing-controller cache/prewarm workflow extension."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/nr-color"))
import assess
import cache_prewarm as workflow


class Controller:
    def __init__(self):
        self.revision = 1
        self.enabled = False
        self.other = 100
        self.calls = []
        self.uncertain = False
        self.applied = 0
        self.frames = 2
        self.native_creates = 2

    def status(self):
        return {"requestedConfigurationFingerprint": str(self.revision),
                "requestedConfiguration": {"upscaling": {"neuralRenderingPrewarmEnabled": self.enabled, "nrScale": self.other}},
                "settings": {"prewarmEnabled": self.enabled, "enabled": True},
                "runtime": {"successfulFrames": self.frames},
                "renderer": {"shaderGenerationApplied": self.applied,
                             "prewarm": {"nativeCreateCalls": self.native_creates, "nativeReleaseCalls": 0}},
                "counters": {field: 0 for field in workflow.CACHE_COUNTERS},
                "stereoRoutes": [{"fresh": True, "pairComplete": True, "eyeMasks": {"neuralCommitted": 3}}]}

    def call(self, payload, label, *, tool):
        self.calls.append((payload, label, tool))
        if payload["action"] == "nr_configure":
            if payload["expectedConfigurationFingerprint"] != str(self.revision):
                raise assess.MutationUncertain("concurrent edit")
            self.enabled = payload["prewarmEnabled"]
            self.revision += 1
            if self.uncertain:
                raise assess.MutationUncertain("lost acknowledgement")
            return {"neuralRendering": self.status(), "historyResetRequested": False}
        if payload["action"] == "nr_shader_invalidate":
            self.applied += 1
            self.frames += 2
            return {"ok": True, "queued": True}
        return {"neuralRendering": self.status()}


class WorkflowTest(unittest.TestCase):
    def collector(self, controller, measure, hour=12):
        sleeps = []
        collector = workflow.CacheEventCollector(controller, lambda: {"gameHour": hour, "receipt": "noon"}, measure, capture_idle=lambda: True, sleep=sleeps.append)
        return collector, sleeps

    def test_guarded_restore_and_no_persistence(self):
        controller = Controller()
        collector, sleeps = self.collector(controller, lambda trigger: {"trigger": trigger(), "wholeFrameMs": None})
        result = collector.collect(True)
        self.assertTrue(result["ok"])
        self.assertFalse(controller.enabled)
        self.assertTrue(result["restoration"]["performed"])
        self.assertEqual(sleeps, [5])
        self.assertIsNone(result["measurements"][0]["wholeFrameMs"])
        self.assertEqual([call[0]["action"] for call in controller.calls].count("nr_shader_invalidate"), 1)
        self.assertFalse(any("save" in call[0]["action"] for call in controller.calls))

    def test_concurrent_edit_survives(self):
        controller = Controller()
        def measure(trigger):
            trigger()
            controller.other, controller.revision = 75, controller.revision + 1
            return {}
        collector, _ = self.collector(controller, measure)
        with self.assertRaises(assess.MutationUncertain):
            collector.collect(True)
        self.assertEqual(controller.other, 75)
        self.assertTrue(controller.enabled)
        self.assertFalse(collector.report["restoration"]["performed"])

    def test_unknown_mutation_cannot_restore(self):
        controller = Controller()
        controller.uncertain = True
        collector, _ = self.collector(controller, lambda _: {})
        with self.assertRaises(assess.MutationUncertain):
            collector.collect(True)
        self.assertEqual(sum(call[0]["action"] == "nr_configure" for call in controller.calls), 1)

    def test_noon_gate_precedes_capture(self):
        controller = Controller()
        collector, sleeps = self.collector(controller, lambda _: self.fail("captured at night"), hour=23)
        with self.assertRaises(assess.AssessmentError):
            collector.collect(True)
        self.assertEqual(sleeps, [])
        self.assertFalse(controller.enabled)

    def test_unresolved_capture_prevents_restore(self):
        controller = Controller()
        def measure(trigger):
            trigger()
            raise RuntimeError("interrupted capture")
        collector, _ = self.collector(controller, measure)
        collector.capture_idle = lambda: False
        with self.assertRaises(RuntimeError):
            collector.collect(True)
        self.assertTrue(controller.enabled)
        self.assertFalse(collector.report["restoration"]["performed"])

    def test_released_capture_restores_after_failure(self):
        controller = Controller()
        collector, _ = self.collector(controller, lambda _: (_ for _ in ()).throw(RuntimeError("capture failure")))
        with self.assertRaises(RuntimeError):
            collector.collect(True)
        self.assertFalse(controller.enabled)
        self.assertTrue(collector.report["restoration"]["performed"])

    def test_no_actual_nr_cannot_be_measured(self):
        controller = Controller()
        original_status = controller.status
        controller.status = lambda: dict(original_status(), stereoRoutes=[])
        collector, _ = self.collector(controller, lambda _: self.fail("captured fallback"))
        with self.assertRaises(assess.AssessmentError):
            collector.collect(True)
        self.assertFalse(controller.enabled)

    def test_event_must_apply_without_recreation_and_present(self):
        for corruption in ("no_trigger", "no_apply", "no_frames", "recreate", "no_stereo"):
            with self.subTest(corruption=corruption):
                controller = Controller()
                def measure(trigger):
                    if corruption == "no_trigger":
                        return {}
                    trigger()
                    if corruption == "no_apply":
                        controller.applied -= 1
                    if corruption == "no_frames":
                        controller.frames -= 2
                    if corruption == "recreate":
                        controller.native_creates += 1
                    if corruption == "no_stereo":
                        status = controller.status
                        controller.status = lambda: dict(status(), stereoRoutes=[])
                    return {}
                collector, _ = self.collector(controller, measure)
                with self.assertRaises(assess.AssessmentError):
                    collector.collect(True)
                self.assertFalse(collector.report["ok"])
                self.assertFalse(controller.enabled)

    def test_plan_cannot_pass_disabled_or_backend_only_prewarm(self):
        for corruption in ("disabled", "backend_only", "partial", "missing", "boolean_count"):
            with self.subTest(corruption=corruption):
                plan, records = self.records()
                candidate = next(row for row in records if row["phase"] == "process_cold" and row["condition"] == "B1")
                if corruption == "disabled":
                    candidate["prewarmEnabled"] = False
                elif corruption == "backend_only":
                    candidate["firstPairPrewarm"]["preparedSlotMask"] = 0
                elif corruption == "partial":
                    candidate["firstPairPrewarm"]["adoptedSlotMask"] = 4
                elif corruption == "missing":
                    del candidate["firstPairPrewarm"]
                else:
                    candidate["invalidPresentedPairs"] = False
                self.assertFalse(workflow.validate(records, plan)["valid"])

    def records(self):
        plan = workflow.schedule()
        records = []
        for index, condition in enumerate(plan):
            record = dict(condition, identityVerified=True, performanceNeutralBefore=True, performanceNeutralAfter=True,
                          gameHour=12, restored=True, stereoQualified=True, featuresResidentAtStartup=0,
                          interrupted=False, invalidPresentedPairs=0, captureSeconds=30,
                          predeclaredDriftLimits={metric: 0 for metric in workflow.METRICS},
                          metrics={metric: 0 for metric in workflow.METRICS},
                          firstPairPrewarm={"selectedSlotMask": 12, "preparedSlotMask": 12, "adoptedSlotMask": 12,
                                            "unusedSlotMask": 0, "missedSlotMask": 0},
                          cacheDeltas=dict.fromkeys(workflow.CACHE_COUNTERS, 0) |
                                      {"nativeCreateCalls": 0, "nativeReleaseCalls": 0, "shaderGenerationApplied": 1})
            record.update({field: "fixture" for field in workflow.IDENTITY + workflow.MATCHED})
            record.update(buildId="a"*64, sourceCommit="b"*40, dllSha256="c"*64, runtimeSha256="d"*64, processId=index+1)
            records.append(record)
        return plan, records

    def test_schedule_and_missing_values(self):
        plan, records = self.records()
        self.assertTrue(workflow.validate(records, plan)["valid"])
        cold = [record for record in plan if record["phase"] == "process_cold" and record["repeat"] == 0]
        self.assertEqual([record["condition"] for record in cold], ["A1", "B1", "B1", "A1"])
        records[0]["metrics"]["nrD3D12Ms"] = None
        result = workflow.validate(records, plan)
        self.assertFalse(result["valid"])
        self.assertIsNone(result["records"][0]["metrics"]["nrD3D12Ms"])
        self.assertIsNone(result["performanceClaim"])

    def test_cold_process_and_quality_mismatch(self):
        plan, records = self.records()
        cold = [record for record in records if record["phase"] == "process_cold"]
        cold[1]["processId"] = cold[0]["processId"]
        self.assertFalse(workflow.validate(records, plan)["valid"])
        plan, records = self.records()
        records[1]["nrScale"] = 70
        self.assertFalse(workflow.validate(records, plan)["valid"])
        plan, records = self.records()
        records[1]["cacheDeltas"]["nativeCreateCalls"] = 1
        self.assertFalse(workflow.validate(records, plan)["valid"])

    def test_bracketing_drift_and_identity(self):
        plan, records = self.records()
        block = [row for row in records if row["phase"] == "settled_prewarm" and row["repeat"] == 0]
        for row, value in zip(block, (10, 9, 12)):
            row["metrics"]["wholeFrameMs"] = value
            row["predeclaredDriftLimits"]["wholeFrameMs"] = 2
        result = workflow.validate(records, plan)
        self.assertTrue(result["valid"])
        comparison = next(row for row in result["comparisons"] if row["phase"] == "settled_prewarm" and row["repeat"] == 0)
        self.assertEqual(comparison["metrics"]["wholeFrameMs"], {"baselineReference": 11, "candidateDelta": -2, "baselineDrift": 2, "driftWithinLimit": True})
        block[2]["metrics"]["wholeFrameMs"] = 13
        self.assertFalse(workflow.validate(records, plan)["valid"])
        plan, records = self.records()
        records[2]["dllSha256"] = "e"*64
        self.assertFalse(workflow.validate(records, plan)["valid"])
        records[2]["dllSha256"] = ""
        self.assertFalse(workflow.validate(records, plan)["valid"])


if __name__ == "__main__":
    unittest.main()
