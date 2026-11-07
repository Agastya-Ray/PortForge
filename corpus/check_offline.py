"""Enforce the dependency and source restrictions for the M0 build."""

from pathlib import Path
import re
import subprocess
import tomllib

packages = tomllib.loads(Path("Cargo.lock").read_text())["package"]
assert {package["name"] for package in packages} == {"pf", "pf-detect", "pf-ir-spike"}
assert all("source" not in package for package in packages)
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
