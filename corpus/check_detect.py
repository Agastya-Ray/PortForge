"""Generate PE32/PE32+ fixtures and check the CLI's JSON against expected data."""

import json
import os
from pathlib import Path
import struct
import subprocess
import sys
from tempfile import TemporaryDirectory


def pe(x64, dll, imports, delayed=False, use_iat=False, delay_va=False):
    data = bytearray(0x600)

    def pack(offset, fmt, *values):
        struct.pack_into("<" + fmt, data, offset, *values)

    data[:2] = b"MZ"
    pack(0x3C, "I", 0x80)
    data[0x80:0x84] = b"PE\0\0"
    optional_size = 240 if x64 else 224
    pack(0x84, "HHIIIHH", 0x8664 if x64 else 0x14C, 1, 0, 0, 0,
         optional_size, (0x22 if x64 else 0x102) | (0x2000 if dll else 0))
    optional = 0x98
    pack(optional, "H", 0x20B if x64 else 0x10B)
    pack(optional + 4, "I", 0x400)
    pack(optional + 16, "II", 0x1000, 0x1000)
    image_base = 0x140000000 if x64 else 0x400000
    pack(optional + (24 if x64 else 28), "Q" if x64 else "I", image_base)
    pack(optional + 32, "II", 0x1000, 0x200)
    pack(optional + 40, "H", 6)
    pack(optional + 48, "H", 6)
    pack(optional + 56, "II", 0x2000, 0x200)
    pack(optional + 68, "H", 3)
    pointer = "Q" if x64 else "I"
    pack(optional + 72, pointer * 4, 0x100000, 0x1000, 0x100000, 0x1000)
    count = optional + (108 if x64 else 92)
    pack(count, "I", 16)
    section = optional + optional_size
    data[section:section + 8] = b".text\0\0\0"
    pack(section + 8, "IIII", 0x400, 0x1000, 0x400, 0x200)
    pack(section + 36, "I", 0x60000020)
    data[0x200:0x206] = b"\xb8\x01\0\0\0\xc3"
    if imports:
        width, index = (32, 13) if delayed else (20, 1)
        pack(count + 4 + 8 * index, "II", 0x1040, width * 2)
        name, thunk, symbol = 0x1100, 0x1140, 0x1180
        name_base = image_base if delay_va else 0
        if delayed:
            pack(0x240, "8I", 0 if delay_va else 1, name + name_base, 0,
                 thunk + name_base, thunk + name_base, 0, 0, 0)
        else:
            pack(0x240, "5I", 0 if use_iat else thunk, 0, 0, name, thunk)
        library, function, ordinal = imports
        data[0x300:0x300 + len(library)] = library.encode("ascii")
        data[0x382:0x382 + len(function)] = function.encode("ascii")
        pack(0x340, pointer * 3, symbol + name_base,
             (1 << (63 if x64 else 31)) | ordinal, 0)
    return data


def main(binary, evidence_dir):
    with TemporaryDirectory(prefix="portforge-corpus-") as temp:
        folder = Path(temp)
        root = folder / "game"
        root.mkdir()
        (root / "bin").mkdir()
        (root / "Title_Data").mkdir()
        (root / "Title_Data" / "globalgamemanagers").touch()
        (root / "world.pck").write_bytes(b"GDPC")
        for marker in ("renpy/__init__.py", "www/js/rpg_core.js", "js/rmmz_core.js"):
            path = root / marker
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        (root / "unrelated.pck").write_bytes(b"no")
        cases = [
            ("D3D11.dll", "D3D11CreateDevice", "d3d11", None),
            ("d3d9.dll", "Direct3DCreate9", "d3d9", None),
            ("OPENGL32.dll", "wglCreateContext", "opengl", None),
            ("vulkan-1.dll", "vkGetInstanceProcAddr", "vulkan", None),
            ("XINPUT1_3.dll", "XInputGetState", None, "xinput"),
            ("dinput8.dll", "DirectInput8Create", None, "directinput"),
            ("user32.dll", "RegisterRawInputDevices", None, "raw_input"),
            ("kernel32.dll", "GetModuleHandleW", None, None),
            ("d3d12.dll", "D3D12CreateDevice", "d3d12", None),
            (None, None, None, None),
        ]
        modules, graphics, inputs = [], {}, {}
        for x64 in (False, True):
            for number, (library, symbol, gfx, input_api) in enumerate(cases):
                dll = number % 2 == 1
                path = f'bin/{"x64" if x64 else "x86"}-{number}.' + ("dll" if dll else "exe")
                if number == 0 and not x64 and os.name != "nt":
                    path = 'bin/x86-"quoted"-0.exe'
                delayed = number in (3, 6)
                imports = (library, symbol, 100 + number) if library else None
                payload = pe(x64, dll, imports, delayed, number == 4, delayed and not x64)
                (root / path).write_bytes(payload)
                expected_imports = []
                if library:
                    expected_imports.append({
                        "dll": library.lower(), "delayed": delayed,
                        "symbols": [{"name": symbol}, {"ordinal": 100 + number}],
                    })
                modules.append({
                    "path": path, "kind": "dll" if dll else "exe",
                    "architecture": "x64" if x64 else "x86", "entry_rva": 0x1000,
                    "image_base": 0x140000000 if x64 else 0x400000, "image_size": 0x2000,
                    "imports": expected_imports,
                })
                for target, api in ((graphics, gfx), (inputs, input_api)):
                    if api:
                        target.setdefault(api, []).append({"path": path, "dll": library.lower()})
        broken = pe(False, False, ("d3d9.dll", "Direct3DCreate9", 100))
        broken[0x340:0x600] = b"\xff" * (0x600 - 0x340)
        (root / "bin" / "bad-imports.exe").write_bytes(broken)
        (root / "bad.exe").write_bytes(b"MZ")
        (root / "not-PE.dll").write_bytes(b"NOT_PE")
        if os.name != "nt":
            (root / "cycle").symlink_to(root, target_is_directory=True)
        first, second = folder / "first", folder / "second"
        for out in (first, second):
            subprocess.run([binary, "detect", root, "--out", out], check=True)
        for name in ("manifest.json", "report.json"):
            assert (first / name).read_bytes() == (second / name).read_bytes(), name
        manifest = json.loads((first / "manifest.json").read_text())
        report = json.loads((first / "report.json").read_text())

        def evidence(table):
            return [
                {"api": api, "evidence": sorted(rows, key=lambda row: row["path"])}
                for api, rows in sorted(table.items())
            ]

        assert manifest == {
            "schema_version": 1, "modules": sorted(modules, key=lambda row: row["path"]),
            "engine": [
                {"id": "unity", "path": "Title_Data/globalgamemanagers"},
                {"id": "rpg_maker_mz", "path": "js/rmmz_core.js"},
                {"id": "renpy", "path": "renpy/__init__.py"},
                {"id": "godot", "path": "world.pck"},
                {"id": "rpg_maker_mv", "path": "www/js/rpg_core.js"},
            ],
            "graphics": evidence(graphics), "input": evidence(inputs),
        }
        assert report["modules_parsed"] == 20
        assert report["failures"] == [
            {"path": "bad.exe", "reason": "truncated PE structure"},
            {"path": "bin/bad-imports.exe", "reason": "unterminated import thunk table"},
            {"path": "not-PE.dll", "reason": "missing MZ signature"},
        ]
        assert report["engine_evidence"] == manifest["engine"]
        assert report["graphics_evidence"] == manifest["graphics"]
        assert report["input_evidence"] == manifest["input"]
        empty = folder / "empty"
        empty.mkdir()
        result = subprocess.run([binary, "detect", empty, "--out", second], capture_output=True)
        assert result.returncode == 1 and b"no supported PE" in result.stderr
        assert json.loads((second / "manifest.json").read_text())["modules"] == []
        evidence_dir.mkdir(parents=True, exist_ok=True)
        for name in ("manifest.json", "report.json"):
            (evidence_dir / name).write_bytes((first / name).read_bytes())
        print("20 PE fixtures passed, 3 malformed files reported, and both JSON outputs reproduced")


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]))
