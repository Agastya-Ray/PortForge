"""Check real PE modules against GNU objdump, both before and after packing."""

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

from pip._vendor import distlib


def check(pf, source, output, architectures):
    subprocess.run([pf, "detect", source, "--out", output], check=True)
    manifest = json.loads((output / "manifest.json").read_text())
    report = json.loads((output / "report.json").read_text())
    assert not report["failures"], report["failures"]
    assert {module["path"]: module["architecture"] for module in manifest["modules"]} == architectures
    for module in manifest["modules"]:
        dump = subprocess.check_output([os.environ.get("PE_OBJDUMP", "objdump"), "-p", source / module["path"]], text=True)
        dlls = {name.lower() for name in re.findall(r"DLL Name:\s+(\S+)", dump)}
        assert {entry["dll"] for entry in module["imports"]} == dlls, module["path"]


def main(pf, packer, evidence):
    samples = {"t32.exe": "x86", "t64.exe": "x64", "w32.exe": "x86"}
    with TemporaryDirectory(prefix="portforge-packed-") as temp:
        original, packed = Path(temp) / "original", Path(temp) / "packed"
        original.mkdir()
        packed.mkdir()
        for name in samples:
            shutil.copyfile(Path(distlib.__file__).parent / name, original / name)
            subprocess.run([packer, "-q", "-o", packed / name, original / name],
                           check=True, stdout=subprocess.DEVNULL)
        check(pf, original, evidence / "original", samples)
        check(pf, packed, evidence / "packed", samples)
        print("3 Windows executables and their packed variants agree with GNU objdump")


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve(), sys.argv[2], Path(sys.argv[3]))
