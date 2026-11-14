"""Validate same-run backend evidence and record the ADR-001 decision."""

import argparse
import json
import math
from pathlib import Path
import statistics


def read(root: Path, filename: str) -> dict:
    value = json.loads((root / filename).read_text())
    if not isinstance(value, dict):
        raise ValueError(f"{filename} must contain an object")
    return value


def count(value: object, minimum: int) -> bool:
    return type(value) is int and value >= minimum


def verify_pair(reference: Path, arm: Path, filename: str, sample_field: str, minimum: int, coverage: dict) -> str:
    native, translated = read(reference, filename), read(arm, filename)
    if native.get("native_x86_oracle") is not True or translated.get("native_x86_oracle") is not False:
        raise ValueError(f"{filename} needs native x86 and independent ARM64 evidence")
    for report in (native, translated):
        if type(report.get("mismatches")) is not int or report["mismatches"] != 0 or not count(report.get(sample_field), minimum):
            raise ValueError(f"{filename} has insufficient samples or mismatches")
        if report[sample_field] != native.get(sample_field):
            raise ValueError(f"{filename} sample counts differ")
        for field, required in coverage.items():
            if not count(report.get(field), required) or report[field] != native.get(field):
                raise ValueError(f"{filename} has incomplete or different {field}")
        if report.get("digest") != native.get("digest") or not isinstance(report.get("digest"), str):
            raise ValueError(f"{filename} digest mismatch")
    if len(native["digest"]) != 16 or any(c not in "0123456789abcdef" for c in native["digest"]):
        raise ValueError(f"{filename} has an invalid digest")
    return native["digest"]


def summarize(reference: Path, arm: Path) -> dict:
    evidence = {}
    for suite, field in (("arithmetic", "samples_per_instruction_width"), ("sse2", "samples_per_operation")):
        coverage = {"instruction_width_pairs": 6, "encodings_per_pair": 3, "three_instruction_samples": 1000000} if suite == "arithmetic" else {"operations": 26, "encodings_per_operation": 3}
        digests = []
        for backend, prefix in (("pfir", ""), ("remill", "remill-")):
            name = f"{prefix}{suite}.json"
            if any(read(root, name).get("backend") != backend for root in (reference, arm)):
                raise ValueError(f"{name} backend mismatch")
            digests.append(verify_pair(reference, arm, name, field, 1000000, coverage))
        if digests[0] != digests[1]:
            raise ValueError(f"{suite} backend results disagree")
        evidence[suite] = digests[0]
    evidence["x87_add"] = verify_pair(reference, arm, "x87-add.json", "samples_per_control", 100000, {"control_combinations": 12})
    evidence["x87_stack"] = verify_pair(reference, arm, "x87-stack.json", "samples_per_encoding", 1000000, {"encodings": 9})

    timings, benchmark_digests, block_counts = {}, set(), set()
    for backend in ("pfir", "remill"):
        samples = [json.loads(line) for line in (arm / f"{backend}-benchmark.jsonl").read_text().splitlines()]
        if len(samples) != 3:
            raise ValueError(f"{backend} needs three timing samples")
        for sample in samples:
            duration = sample.get("cpu_ns_per_block", 0)
            if sample.get("backend") != backend or not isinstance(duration, (int, float)) or not math.isfinite(duration) or duration <= 0:
                raise ValueError(f"{backend} has invalid timing evidence")
            if not count(sample.get("blocks"), 1) or not isinstance(sample.get("digest"), str):
                raise ValueError(f"{backend} has incomplete timing evidence")
            benchmark_digests.add(sample["digest"])
            block_counts.add(sample["blocks"])
        timings[backend] = statistics.median(sample["cpu_ns_per_block"] for sample in samples)
    if len(benchmark_digests) != 1 or len(block_counts) != 1:
        raise ValueError("timing workloads differ")
    sizes = {backend: int((arm / f"{backend}-c-bytes.txt").read_text()) for backend in ("pfir", "remill")}
    if any(size <= 0 for size in sizes.values()):
        raise ValueError("generated C sizes must be positive")
    host = read(arm, "long-double-format.json")
    if host.get("architecture") != "aarch64" or host.get("mantissa_bits") != 113:
        raise ValueError("the current ARM64 comparison requires its binary128 long double probe")
    return {
        "schema_version": 1,
        "decision": "PFIR for the production C intermediate representation",
        "correctness_digests": evidence,
        "arm64_median_cpu_ns_per_block": timings,
        "generated_c_bytes": sizes,
        "arm64_host_long_double_mantissa_bits": host["mantissa_bits"],
        "guest_x87_mantissa_bits": 64,
        "timing_scope": "Includes adapters and flag observation. No production speed claim.",
        "milestone_complete": False,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("reference", type=Path)
    parser.add_argument("arm64", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = summarize(args.reference, args.arm64)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
