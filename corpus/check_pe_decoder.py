"""Run the decoder's PE entry integration test with the locked Windows fixtures."""

from pathlib import Path
import os
import subprocess
import sys
from tempfile import TemporaryDirectory
from zipfile import ZipFile


def main(wheel):
    with TemporaryDirectory(prefix="portforge-pe-decoder-") as temp, ZipFile(wheel) as archive:
        for name in ("t32.exe", "t64.exe"):
            (Path(temp) / name).write_bytes(archive.read(f"distlib/{name}"))
        subprocess.run(
            ["cargo", "test", "-p", "pf-decode", "--release", "--locked",
             "--offline", "--test", "pe_entries", "--", "--ignored"],
            env={**os.environ, "PORTFORGE_PE_FIXTURES": temp},
            check=True,
        )


if __name__ == "__main__":
    main(Path(sys.argv[1]))
