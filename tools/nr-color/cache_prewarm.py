"""Matched NR cache/prewarm experiment plans and guarded collection.

Use the existing DevBench Controller (or its direct-MCP adapter). The caller
supplies the existing scene/noon and profiler-capture operations; this module
opens no transport, launches no process and never saves or deletes settings or
shader caches. Cold runs require externally prepared fresh process sessions.
"""
from __future__ import annotations

import argparse
import copy
import json
import re
from pathlib import Path
import time

import assess

NR_TOOL = "communityshaders.neural_rendering"
IDENTITY = ("buildId", "sourceCommit", "dllSha256", "runtimeSha256", "processId", "processStart")
MATCHED = ("scene", "gameResolution", "dlssPreset", "nrScale", "enhancement", "coverage", "shaderCacheIdentity")
CACHE_COUNTERS = ("runtimeInitializations", "interopInitializations", "resetAttempts", "failures", "stereoFailures")
METRICS = ("wholeFrameMs", "wholeFrameCpuMs", "wholeFrameGpuMs", "nrD3D11Ms", "nrD3D12Ms", "waitMs", "memoryPeakBytes")


def schedule(repeats: int = 5) -> list[dict]:
    if type(repeats) is not int or not 5 <= repeats <= 20:
        raise ValueError("Use 5..20 matched repeats")
    result = []
    for repeat in range(repeats):
        for phase, conditions in (("cache_event", ("A0", "A1", "B1")),
                                  ("settled_cache", ("A0", "A1", "A0")),
                                  ("settled_prewarm", ("A1", "B1", "A1")),
                                  ("process_cold", ("A1", "B1", "B1", "A1")),
                                  ("stress", ("A1", "B1"))):
            for ordinal, condition in enumerate(conditions):
                result.append({"phase": phase, "repeat": repeat, "ordinal": ordinal,
                               "condition": condition, "prewarmEnabled": condition == "B1",
                               "freshProcess": phase == "process_cold",
                               "noonResetRequired": True, "settleSeconds": 5,
                               "startupWindowSeparate": phase == "process_cold",
                               "minimumCaptureSeconds": 30 if phase.startswith("settled_") else None})
    return result


def committed_stereo(status):
    return status.get("settings", {}).get("enabled") is True and any(
        route.get("fresh") is True and route.get("pairComplete") is True
        and route.get("eyeMasks", {}).get("neuralCommitted") == 3
        for route in status.get("stereoRoutes", []))


def cache_event_deltas(before, after):
    """Require continued presentation and no native/backend churn across the event."""
    paths = {field: ("counters", field) for field in CACHE_COUNTERS}
    paths.update({field: ("renderer", "prewarm", field) for field in ("nativeCreateCalls", "nativeReleaseCalls")})
    paths.update(successfulFrames=("runtime", "successfulFrames"),
                 shaderGenerationApplied=("renderer", "shaderGenerationApplied"))
    deltas = {}
    for field, path in paths.items():
        values = []
        for status in (before, after):
            value = status
            for part in path:
                value = value.get(part) if isinstance(value, dict) else None
            values.append(value)
        if not all(assess.integer(value) for value in values):
            raise assess.AssessmentError("Cache event lacks counter " + field)
        deltas[field] = values[1] - values[0]
        if (deltas[field] <= 0 if field in ("successfulFrames", "shaderGenerationApplied") else deltas[field] != 0):
            raise assess.AssessmentError("Cache event violated " + field)
    if not committed_stereo(after):
        raise assess.AssessmentError("Cache event did not retain a committed NR stereo pair")
    return deltas


class CacheEventCollector:
    """One owned cache-event condition; scene and profiler owners stay external.

    reset_noon returns its command receipt and observed gameHour. measure accepts
    a zero-argument trigger, runs it inside its bounded capture, and returns the
    complete measurement receipt. It must finalize its own capture on failure. capture_idle verifies that
    ownership is released before any settings restoration.
    The existing controller retains the complete request/response journal.
    """
    def __init__(self, controller, reset_noon, measure, *, capture_idle, sleep=time.sleep):
        self.controller, self.reset_noon, self.measure = controller, reset_noon, measure
        self.sleep, self.capture_idle = sleep, capture_idle
        self.capture_started = False
        self.original = self.owned = None
        self.uncertain = False
        self.report = {"ok": False, "restoration": {"performed": False}, "measurements": []}

    def status(self, label):
        result = self.controller.call({"action": "nr_status"}, label, tool=NR_TOOL)["neuralRendering"]
        if not result.get("requestedConfigurationFingerprint"):
            raise assess.AssessmentError("NR configuration fingerprint missing")
        return result

    def guard(self, label):
        status = self.status(label)
        if self.owned and status["requestedConfigurationFingerprint"] != self.owned["requestedConfigurationFingerprint"]:
            raise assess.MutationUncertain("Concurrent NR edit; automatic restoration would overwrite it")
        return status

    def configure(self, enabled, label):
        before = self.guard(label + "-guard")
        if before["settings"]["prewarmEnabled"] == enabled:
            self.owned = before
            return
        self.uncertain = True
        reply = self.controller.call({"action": "nr_configure", "prewarmEnabled": enabled,
                                      "expectedConfigurationFingerprint": before["requestedConfigurationFingerprint"]}, label, tool=NR_TOOL)
        after = reply["neuralRendering"]
        expected = copy.deepcopy(before["requestedConfiguration"])
        expected["upscaling"]["neuralRenderingPrewarmEnabled"] = enabled
        if after["requestedConfiguration"] != expected or reply.get("historyResetRequested") is not False:
            raise assess.MutationUncertain("Prewarm acknowledgement changed rendering or lacks a guarded outcome")
        self.owned = after
        self.uncertain = False

    def collect(self, prewarm_enabled: bool):
        if type(prewarm_enabled) is not bool:
            raise ValueError("prewarm_enabled must be boolean")
        try:
            self.original = self.owned = self.status("cache-event-original")
            self.configure(prewarm_enabled, "cache-event-configure")
            scene = self.reset_noon()
            hour = scene.get("gameHour")
            if type(hour) not in (int, float) or not 12 <= hour <= 12.05:
                raise assess.AssessmentError("Matched condition lacks verified noon")
            self.report["sceneReset"] = scene
            self.sleep(5)
            before = self.guard("cache-event-before")
            if not committed_stereo(before):
                raise assess.AssessmentError("Cache event requires a current committed NR stereo pair")
            self.report["before"] = before
            triggered = False
            def trigger():
                nonlocal triggered
                if triggered:
                    raise assess.AssessmentError("Cache event trigger must run exactly once")
                self.guard("cache-event-trigger-guard")
                triggered = True
                reply = self.controller.call({"action": "nr_shader_invalidate"}, "cache-event-invalidate", tool=NR_TOOL)
                if reply.get("ok") is not True or reply.get("queued") is not True:
                    raise assess.AssessmentError("Cache event invalidation was not acknowledged")
                return reply
            self.capture_started = True
            self.report["measurements"].append(self.measure(trigger))
            if self.capture_idle() is not True:
                raise assess.MutationUncertain("Capture ownership is unresolved")
            self.capture_started = False
            self.report["after"] = self.guard("cache-event-after")
            if not triggered:
                raise assess.AssessmentError("Capture did not trigger cache invalidation")
            self.report["cacheDeltas"] = cache_event_deltas(before, self.report["after"])
            self.report["ok"] = True
        finally:
            if self.capture_started:
                try:
                    inactive = self.capture_idle() is True
                except Exception:
                    inactive = False
                if not inactive:
                    self.uncertain = True
                    self.report["restoration"] = {"performed": False, "error": "capture ownership unresolved"}
            if self.original is not None and not self.uncertain:
                try:
                    self.configure(self.original["settings"]["prewarmEnabled"], "cache-event-restore")
                    self.guard("cache-event-restored")
                    self.report["restoration"] = {"performed": True}
                except (assess.AssessmentError, KeyError) as error:
                    self.report["ok"] = False
                    self.report["restoration"] = {"performed": False, "error": str(error)}
        return self.report


def validate(records: list[dict], planned: list[dict]) -> dict:
    """Fail closed on missing/mismatched evidence; never replace missing by zero."""
    gaps, cold_processes, groups = [], set(), {}
    if len(records) != len(planned):
        gaps.append("condition count differs from the preserved plan")
    for index, (record, condition) in enumerate(zip(records, planned)):
        prefix = f"condition {index}"
        if any(record.get(key) != condition[key] for key in ("phase", "repeat", "ordinal", "condition")):
            gaps.append(prefix + ": schedule mismatch")
        if record.get("prewarmEnabled") is not condition["prewarmEnabled"]:
            gaps.append(prefix + ": prewarm setting differs from the planned condition")
        for field in IDENTITY + MATCHED:
            if record.get(field) is None or record.get(field) == "":
                gaps.append(prefix + ": missing " + field)
        for field, length in (("buildId", 64), ("sourceCommit", 40), ("dllSha256", 64), ("runtimeSha256", 64)):
            if not isinstance(record.get(field), str) or not re.fullmatch(r"[0-9a-fA-F]{%d}" % length, record[field]):
                gaps.append(prefix + ": invalid " + field)
        if not assess.integer(record.get("processId")) or record["processId"] == 0 or not isinstance(record.get("processStart"), str):
            gaps.append(prefix + ": invalid process identity")
        if condition["phase"].startswith("settled_") and (not assess.number(record.get("captureSeconds")) or record["captureSeconds"] < 30):
            gaps.append(prefix + ": settled capture shorter than 30 seconds")
        if record.get("interrupted") is not False or (not assess.integer(record.get("invalidPresentedPairs")) or record["invalidPresentedPairs"] != 0):
            gaps.append(prefix + ": interrupted capture or invalid presentation")
        if record.get("identityVerified") is not True or record.get("performanceNeutralBefore") is not True or record.get("performanceNeutralAfter") is not True:
            gaps.append(prefix + ": producer/owner verification missing")
        if not (assess.number(record.get("gameHour")) and 12 <= record["gameHour"] <= 12.05 and assess.number(record.get("settleSeconds")) and record["settleSeconds"] >= 5):
            gaps.append(prefix + ": unmatched scene time")
        if record.get("restored") is not True or record.get("stereoQualified") is not True:
            gaps.append(prefix + ": restoration or stereo qualification missing")
        for metric in METRICS:
            value = record.get("metrics", {}).get(metric)
            if not assess.number(value) or value < 0:
                gaps.append(prefix + ": unavailable " + metric)
        group = groups.setdefault((condition["phase"], condition["repeat"]), [])
        group.append(record)
        if condition["freshProcess"]:
            process = (record.get("processId"), record.get("processStart"))
            if process in cold_processes or (not assess.integer(record.get("featuresResidentAtStartup")) or record["featuresResidentAtStartup"] != 0) or record.get("startupWindowSeparate") is not True:
                gaps.append(prefix + ": not an independent cold-start experiment")
            cold_processes.add(process)
            if condition["prewarmEnabled"]:
                first_pair = record.get("firstPairPrewarm", {})
                if not isinstance(first_pair, dict) or any(
                        not assess.integer(first_pair.get(field)) or first_pair[field] != expected
                        for field, expected in (("selectedSlotMask", 12), ("preparedSlotMask", 12),
                                                ("adoptedSlotMask", 12), ("unusedSlotMask", 0), ("missedSlotMask", 0))):
                    gaps.append(prefix + ": first pair lacks exact native prewarm reuse (backend-only, missed or unavailable)")
        if condition["phase"] == "cache_event" and condition["condition"] != "A0":
            delta = record.get("cacheDeltas", {})
            for field in ("nativeCreateCalls", "nativeReleaseCalls") + CACHE_COUNTERS:
                if not assess.integer(delta.get(field)) or delta[field] != 0:
                    gaps.append(prefix + ": shader invalidation changed " + field)
            if type(delta.get("shaderGenerationApplied")) is not int or delta["shaderGenerationApplied"] <= 0:
                gaps.append(prefix + ": invalidation was not observed")
    comparisons = []
    for (phase, repeat), group in groups.items():
        if any(any(record.get(field) != group[0].get(field) for field in MATCHED) for record in group[1:]):
            gaps.append("matched condition changed scene, quality or cache identity")
        candidate_builds = {(row.get("buildId"), row.get("sourceCommit"), row.get("dllSha256")) for row in group if row.get("condition") != "A0"}
        if len(candidate_builds) != 1 or len({row.get("runtimeSha256") for row in group}) != 1:
            gaps.append("matched candidate executable or NR runtime identity changed")
        if phase.startswith("settled_") and len(group) == 3:
            comparison = {"phase": phase, "repeat": repeat, "metrics": {}}
            for metric in METRICS:
                values = [row.get("metrics", {}).get(metric) for row in group]
                result = {"baselineReference": None, "candidateDelta": None, "baselineDrift": None, "driftWithinLimit": None}
                limits = [row.get("predeclaredDriftLimits", {}).get(metric) for row in group]
                if all(assess.number(value) and value >= 0 for value in values):
                    before, candidate, after = values
                    reference = (before + after) / 2
                    result.update(baselineReference=reference, candidateDelta=candidate-reference, baselineDrift=after-before)
                    if all(assess.number(limit) and limit >= 0 and limit == limits[0] for limit in limits):
                        result["driftWithinLimit"] = abs(after-before) <= limits[0]
                if result["driftWithinLimit"] is not True:
                    gaps.append(f"{phase} repeat {repeat}: {metric} drift inconclusive")
                comparison["metrics"][metric] = result
            comparisons.append(comparison)
    return {"valid": not gaps, "gaps": gaps, "records": records,
            "comparisons": comparisons, "performanceClaim": None, "defaultPrewarmEnabled": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("plan", "validate"))
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--records", type=Path)
    args = parser.parse_args()
    planned = schedule(args.repeats)
    if args.action == "plan":
        print(json.dumps({"schedule": planned, "conditions": {"A0": "unchanged baseline", "A1": "shader invalidation only", "B1": "A1 with exact prewarm"}}, indent=2))
        return 0
    if args.records is None:
        parser.error("validate requires --records")
    result = validate(json.loads(args.records.read_text(encoding="utf-8")), planned)
    print(json.dumps(result, indent=2))
    return 0 if result["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
