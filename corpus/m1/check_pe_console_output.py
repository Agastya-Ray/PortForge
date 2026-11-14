import argparse
from pathlib import Path
import struct
import subprocess


INPUT_BYTES = bytes.fromhex("00 50 46 0d 0a 1a ff 00 0a")


def expected() -> dict[str, tuple[bytes, bytes, int]]:
    value = 0x531C7AB2
    words = []
    for _ in range(8):
        value = ((value + 0x92C7A1E5) & 0xFFFFFFFF) ^ 0x4107332F
        if value > 0xC0000000:
            value ^= 0x7010F0AA
        words.append(value)

    a, b, c, d, e = (
        0x76543210FEDCBA98, 0xFFFF000010203040, 0x123456789ABCDEF0,
        0x0101010190909090, 0xFEDCBA9876543210,
    )
    mask = (1 << 64) - 1
    calls = struct.pack(
        "<2Q", (((a + b) ^ (c - d)) + e) & mask, (((e + d) ^ (c - b)) + a) & mask
    )
    a, b = 0xFEDCBA9876543211, 0x800000000000007F
    arithmetic = []
    for _ in range(8):
        product = (a * b) & mask
        low = (product + a) & mask
        b = ((~b) + (low < product)) & mask
        a = (-low) & mask
        arithmetic.extend((a, b))
    storage_bytes = bytes((((v + i + 3) & 0xff) ^ 0xa5) for i, v in enumerate((0, 1, 0x7f, 0x80, 0xfe, 0xff, 0x0a, 0x0d)))
    storage_words = [~(v - 0x1234 - i) & 0xffff for i, v in enumerate((0, 1, 0x7fff, 0x8000, 0xfffe, 0xffff, 0x0a0d, 0x1234))]
    return {
        "text": (b"PortForge console\r\nsecond line\n", b"", 0),
        "count": (struct.pack("<8I", *words), b"", 0),
        "streams": (bytes.fromhex("00 ff 0d 0a 00 1a"), b"guest diagnostic\r\n", 23),
        "calls": (calls, b"", 0),
        "input": (INPUT_BYTES, b"", 0),
        "clock": (b"performance counter ok\n", b"", 0),
        "system": (b"system calls ok\n", b"", 0),
        "arithmetic": (struct.pack("<16Q", *arithmetic), b"", 0),
        "storage": (storage_bytes + struct.pack("<8H", *storage_words), b"", 0),
    }


def main() -> None:
    fixtures = expected()
    parser = argparse.ArgumentParser()
    parser.add_argument("fixture", choices=fixtures)
    parser.add_argument("result", type=Path)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--run-native", type=Path)
    mode.add_argument("--prepare-input", action="store_true")
    args = parser.parse_args()
    result = args.result
    input_bytes = INPUT_BYTES if args.fixture == "input" else b""
    if args.prepare_input:
        result.mkdir(parents=True, exist_ok=True)
        (result / "stdin.bin").write_bytes(input_bytes)
        return
    if args.run_native is not None:
        result.mkdir(parents=True, exist_ok=True)
        input_path = result.parent / "stdin.bin"
        input_path.write_bytes(input_bytes)
        with (input_path.open("rb") as stdin,
              (result / "stdout.bin").open("wb") as stdout,
              (result / "stderr.bin").open("wb") as stderr):
            status = subprocess.run([str(args.run_native.resolve())], stdin=stdin, stdout=stdout, stderr=stderr, timeout=30)
        (result / "exit.txt").write_bytes(f"{status.returncode}\n".encode("ascii"))

    stdout, stderr, status = fixtures[args.fixture]
    for filename, want in (("stdout.bin", stdout), ("stderr.bin", stderr), ("exit.txt", f"{status}\n".encode("ascii"))):
        actual = (result / filename).read_bytes()
        if actual != want:
            raise SystemExit(f"{args.fixture} {filename}: expected {want!r}, got {actual!r}")
    print(f"{args.fixture}: {len(stdout)} stdout bytes, {len(stderr)} stderr bytes, exit {status}")


if __name__ == "__main__":
    main()
