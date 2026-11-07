"""Enforce the dependency and source restrictions for the M0 build."""

from pathlib import Path
import re
import subprocess
import tomllib

packages = tomllib.loads(Path("Cargo.lock").read_text())["package"]
workspace_packages = {"pf", "pf-detect", "pf-decode", "pf-ir-spike"}
decoder_packages = {"zydis", "bitflags", "cmake", "cc", "find-msvc-tools", "shlex"}
assert {package["name"] for package in packages if "source" not in package} == workspace_packages
assert {package["name"] for package in packages if "source" in package} == decoder_packages
for package in packages:
    if "source" in package:
        assert package["source"] == "registry+https://github.com/rust-lang/crates.io-index"
        assert re.fullmatch(r"[0-9a-f]{64}", package["checksum"])
paths = subprocess.check_output(["git", "ls-files", "-z"]).decode().split("\0")
model_suffixes = {".gguf", ".ggml", ".onnx", ".safetensors", ".pt", ".pth", ".tflite"}
service = re.compile(
    r"api\.(?:openai|anthropic)\.com|generativelanguage\.googleapis\.com|"
    r"\b(?:from|import)\s+(?:openai|anthropic|transformers|ollama|google\.genai)\b",
    re.IGNORECASE,
)
for name in filter(None, paths):
    path = Path(name)
    assert path.suffix.lower() not in model_suffixes, f"model file: {path}"
    if path.suffix in {".rs", ".c", ".h", ".py", ".sh", ".toml", ".yml", ".yaml"}:
        if path != Path(__file__).resolve().relative_to(Path.cwd()):
            assert not service.search(path.read_text()), f"AI dependency or endpoint: {path}"
