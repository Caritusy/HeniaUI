#!/usr/bin/env python3
"""Compare two HeniaUI benchmark JSON files from the same runner."""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
from typing import Any


def load(path: pathlib.Path) -> dict[str, dict[str, Any]]:
    document = json.loads(path.read_text(encoding="utf-8"))
    if document.get("schema_version") != 1:
        raise ValueError(f"{path}: unsupported benchmark schema")
    return {scenario["name"]: scenario for scenario in document["scenarios"]}


def percent_change(baseline: float, candidate: float) -> float:
    if baseline == 0:
        return 0.0 if candidate == 0 else float("inf")
    return (candidate - baseline) * 100.0 / baseline


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline", type=pathlib.Path)
    parser.add_argument("candidate", type=pathlib.Path)
    parser.add_argument("--max-time-regression-percent", type=float, default=35.0)
    parser.add_argument("--max-memory-regression-percent", type=float, default=5.0)
    parser.add_argument("--minimum-time-delta-ns", type=int, default=10_000)
    parser.add_argument("--markdown", type=pathlib.Path)
    arguments = parser.parse_args()

    baseline = load(arguments.baseline)
    candidate = load(arguments.candidate)
    failures: list[str] = []
    notes: list[str] = []
    rows: list[str] = []

    missing = sorted(set(baseline) - set(candidate))
    extra = sorted(set(candidate) - set(baseline))
    if missing:
        failures.append("missing scenarios: " + ", ".join(missing))
    if extra:
        notes.append("new candidate scenarios (not base-compared): " + ", ".join(extra))

    for name in sorted(set(baseline) & set(candidate)):
        before = baseline[name]
        after = candidate[name]
        before_time = int(before["cpu"]["total_median_ns"])
        after_time = int(after["cpu"]["total_median_ns"])
        time_change = percent_change(before_time, after_time)
        rows.append(
            f"| {name} | {before_time / 1000.0:.2f} | "
            f"{after_time / 1000.0:.2f} | {time_change:+.1f}% | "
            f"{before['cpu']['allocations_median']} -> {after['cpu']['allocations_median']} | "
            f"{before['gpu']['upload_bytes']} -> {after['gpu']['upload_bytes']} |"
        )
        if (
            after_time - before_time > arguments.minimum_time_delta_ns
            and time_change > arguments.max_time_regression_percent
        ):
            failures.append(
                f"{name}: median CPU time regressed {time_change:.1f}% "
                f"({before_time} ns -> {after_time} ns)"
            )

        before_allocations = int(before["cpu"]["allocations_median"])
        after_allocations = int(after["cpu"]["allocations_median"])
        if after_allocations > before_allocations:
            failures.append(
                f"{name}: median allocations increased "
                f"({before_allocations} -> {after_allocations})"
            )

        for group, field in (
            ("gpu", "draw_calls"),
            ("gpu", "upload_bytes"),
            ("gpu", "cold_upload_bytes"),
        ):
            before_value = int(before[group][field])
            after_value = int(after[group][field])
            if after_value > before_value:
                failures.append(
                    f"{name}: {group}.{field} increased "
                    f"({before_value} -> {after_value})"
                )

        if "estimated_fragment_area_px" in before["gpu"]:
            before_area = int(before["gpu"]["estimated_fragment_area_px"])
            after_area = int(after["gpu"].get("estimated_fragment_area_px", 0))
            if after_area > before_area:
                failures.append(
                    f"{name}: gpu.estimated_fragment_area_px increased "
                    f"({before_area} -> {after_area})"
                )

        for field in ("cpu_resident_bytes", "gpu_buffer_bytes", "texture_bytes"):
            before_value = int(before["memory"][field])
            after_value = int(after["memory"][field])
            memory_change = percent_change(before_value, after_value)
            if memory_change > arguments.max_memory_regression_percent:
                failures.append(
                    f"{name}: memory.{field} regressed {memory_change:.1f}% "
                    f"({before_value} -> {after_value})"
                )

        before_hit_rate = float(before["cache"]["hit_rate"])
        after_hit_rate = float(after["cache"]["hit_rate"])
        if after_hit_rate + 0.01 < before_hit_rate:
            failures.append(
                f"{name}: cache hit rate fell "
                f"({before_hit_rate:.3f} -> {after_hit_rate:.3f})"
            )

    report = "\n".join(
        [
            "## HeniaUI benchmark comparison",
            "",
            "| Scenario | Base median us | Candidate median us | Change | Allocations | Upload bytes |",
            "|---|---:|---:|---:|---:|---:|",
            *rows,
            "",
            "Result: " + ("PASS" if not failures else "FAIL"),
        ]
        + (["", "Notes:", *[f"- {note}" for note in notes]] if notes else [])
        + (["", "Regressions:", *[f"- {failure}" for failure in failures]] if failures else [])
    )
    print(report)
    if arguments.markdown:
        arguments.markdown.write_text(report + "\n", encoding="utf-8")
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
        print(f"benchmark comparison failed: {error}", file=sys.stderr)
        raise SystemExit(2)
