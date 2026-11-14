"""Regression cases for malformed IR acceptance evidence."""

import copy
import json
from pathlib import Path
import tempfile

from compare_ir_evidence import summarize


def fixtures(native: bool) -> dict:
    common = {"native_x86_oracle": native, "mismatches": 0, "digest": "0123456789abcdef"}
    result = {}
    for backend, prefix in (("pfir", ""), ("remill", "remill-")):
        result[f"{prefix}arithmetic.json"] = dict(common, backend=backend, samples_per_instruction_width=1000000, instruction_width_pairs=6, encodings_per_pair=3, three_instruction_samples=1000000)
        result[f"{prefix}sse2.json"] = dict(common, backend=backend, samples_per_operation=1000000, operations=26, encodings_per_operation=3)
    result["x87-add.json"] = dict(common, samples_per_control=100000, control_combinations=12)
    result["x87-stack.json"] = dict(common, samples_per_encoding=1000000, encodings=9)
    return result


def write(root: Path, reports: dict) -> None:
    for name, report in reports.items():
        (root / name).write_text(json.dumps(report))


def main() -> None:
    with tempfile.TemporaryDirectory() as directory:
        reference, arm = Path(directory) / "native", Path(directory) / "arm"
        reference.mkdir()
        arm.mkdir()
        native, translated = fixtures(True), fixtures(False)
        write(reference, native)
        write(arm, translated)
        for backend in ("pfir", "remill"):
            (arm / f"{backend}-c-bytes.txt").write_text("123")
            row = dict(backend=backend, blocks=1000, cpu_ns_per_block=12.5, digest="0123456789abcdef")
            (arm / f"{backend}-benchmark.jsonl").write_text((json.dumps(row) + "\n") * 3)
        (arm / "long-double-format.json").write_text(json.dumps(dict(architecture="aarch64", mantissa_bits=113)))
        assert summarize(reference, arm)["milestone_complete"] is False
        cases = [
            ("arithmetic.json", "samples_per_instruction_width", float("nan")),
            ("arithmetic.json", "instruction_width_pairs", 0),
            ("sse2.json", "operations", 0),
            ("sse2.json", "operations", 25),
            ("sse2.json", "native_x86_oracle", True),
            ("x87-stack.json", "encodings", 0),
            ("x87-add.json", "control_combinations", 1),
            ("remill-arithmetic.json", "digest", "ffffffffffffffff"),
            ("arithmetic.json", "mismatches", False),
        ]
        for filename, field, value in cases:
            bad = copy.deepcopy(translated)
            bad[filename][field] = value
            write(arm, bad)
            try:
                summarize(reference, arm)
            except ValueError:
                pass
            else:
                raise AssertionError(f"accepted malformed {filename} {field}")
        print("IR evidence validation rejects all malformed regression cases.")


if __name__ == "__main__":
    main()
