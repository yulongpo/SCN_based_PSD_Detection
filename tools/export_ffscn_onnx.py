#!/usr/bin/env python3
"""Export the pinned FFSCN 17th-order checkpoint to a dynamic-width ONNX model."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys

import numpy as np
import torch

EXPECTED_SHA256 = "7ec171094decd4557abaaf3e295b0fd7397196c67a9249d5593aef00db885da6"
DEFAULT_WEIGHTS = Path(r"\\192.168.1.100\huanghao\coding\FFSCN_train2\runs\NFFT17_down13_resume_epoch83\train_4\weights\model_epoch_0155.pth")
DEFAULT_PROJECT = Path(r"\\192.168.1.100\huanghao\coding\FFSCN_train2")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


class RawOutputs(torch.nn.Module):
    def __init__(self, model: torch.nn.Module) -> None:
        super().__init__()
        self.model = model

    def forward(self, spectrum: torch.Tensor):
        result = self.model(spectrum)
        return result["hm"], result["bw"], result["off"]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--weights", type=Path, default=DEFAULT_WEIGHTS)
    parser.add_argument("--training-project", type=Path, default=DEFAULT_PROJECT)
    parser.add_argument("--output", type=Path, default=Path("models/ffscn_17.onnx"))
    parser.add_argument("--report", type=Path, default=Path("models/ffscn_17_export_report.json"))
    args = parser.parse_args()

    weights = args.weights.resolve(strict=True)
    source = args.training_project.resolve(strict=True)
    actual_hash = sha256(weights)
    if actual_hash.lower() != EXPECTED_SHA256:
        raise RuntimeError(f"Pinned checkpoint hash mismatch: {actual_hash}")

    os.environ.setdefault("MKL_THREADING_LAYER", "SEQUENTIAL")
    torch.set_num_threads(min(torch.get_num_threads(), 8))
    sys.path.insert(0, str(source))
    from ffscn.models.loading import load_model  # noqa: PLC0415

    model = load_model(weights, down_nums=13, input_length=131072, device="cpu")
    # The legacy model uses this flag (independent of Module.eval()) to return
    # raw hm/bw/off tensors instead of Python-side decoded bands.
    model.is_training = True
    wrapper = RawOutputs(model).eval()
    sample = torch.randn(1, 1, 10, 131072, dtype=torch.float32)
    with torch.inference_mode():
        raw = wrapper(sample)
    expected = (32768, 32768, 32768)
    if tuple(value.shape[-1] for value in raw) != expected:
        raise RuntimeError(f"Unexpected network output shapes: {[tuple(x.shape) for x in raw]}")
    if not all(torch.isfinite(value).all().item() for value in raw):
        raise RuntimeError("FFSCN forward produced NaN/Inf before export.")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    torch.onnx.export(
        wrapper,
        sample,
        str(args.output),
        export_params=True,
        opset_version=17,
        do_constant_folding=True,
        input_names=["spectrum"],
        output_names=["hm", "bw", "off"],
        dynamic_axes={
            "spectrum": {3: "frequency_bins"},
            "hm": {3: "output_bins"},
            "bw": {3: "output_bins"},
            "off": {3: "output_bins"},
        },
        dynamo=False,
    )

    import onnx  # noqa: PLC0415
    import onnxruntime as ort  # noqa: PLC0415

    graph = onnx.load(str(args.output))
    onnx.checker.check_model(graph)
    session = ort.InferenceSession(str(args.output), providers=["CPUExecutionProvider"])
    comparisons = []
    for width in (8192, 131072):
        source_input = np.random.default_rng(20260923 + width).standard_normal(
            (1, 1, 10, width), dtype=np.float32
        )
        with torch.inference_mode():
            torch_outputs = wrapper(torch.from_numpy(source_input))
        onnx_outputs = session.run(["hm", "bw", "off"], {"spectrum": source_input})
        entry = {"inputWidth": width, "outputs": []}
        for name, actual, expected_tensor in zip(("hm", "bw", "off"), onnx_outputs, torch_outputs):
            expected_np = expected_tensor.detach().cpu().numpy()
            max_abs = float(np.max(np.abs(actual - expected_np)))
            max_rel = float(np.max(np.abs(actual - expected_np) / np.maximum(np.abs(expected_np), 1e-8)))
            passed = bool(np.allclose(actual, expected_np, atol=1e-4, rtol=1e-3))
            entry["outputs"].append({"name": name, "shape": list(actual.shape),
                                     "maxAbs": max_abs, "maxRel": max_rel, "passed": passed})
            if not passed:
                raise RuntimeError(f"PyTorch/ONNX mismatch at N={width}, output={name}, maxAbs={max_abs}")
        comparisons.append(entry)

    report = {
        "checkpoint": str(weights),
        "checkpointSha256": actual_hash,
        "trainingSource": str(source),
        "architecture": {"downNums": 13, "backbone": "MobileNetV3 Large",
                         "neck": "FPN", "neckChannels": 64, "scale": 4},
        "contract": {"input": "spectrum float32 [1,1,10,N]", "outputs": ["hm", "bw", "off"],
                     "outputShape": "[1,1,1,N/4]", "opset": 17,
                     "dynamicWidth": [8192, 131072]},
        "onnxSha256": sha256(args.output),
        "comparisons": comparisons,
        "torch": torch.__version__, "onnx": onnx.__version__, "onnxruntime": ort.__version__,
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
