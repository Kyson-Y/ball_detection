from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from pathlib import Path

import onnx
import ultralytics
from ultralytics import YOLO


REQUIRED_ULTRALYTICS = "8.4.104"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Export the reviewed ball YOLO11 model for MaixHub."
    )
    parser.add_argument("--weights", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    if ultralytics.__version__ != REQUIRED_ULTRALYTICS:
        raise RuntimeError(
            f"ultralytics {REQUIRED_ULTRALYTICS} is required, "
            f"found {ultralytics.__version__}"
        )

    weights = args.weights.resolve()
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)

    model = YOLO(str(weights))
    exported = Path(
        model.export(
            format="onnx",
            imgsz=(96, 320),
            opset=17,
            simplify=True,
            dynamic=False,
            batch=1,
        )
    ).resolve()
    if exported != output:
        shutil.move(exported, output)

    graph = onnx.load(str(output))
    onnx.checker.check_model(graph)
    input_tensor = graph.graph.input[0]
    shape = [dim.dim_value for dim in input_tensor.type.tensor_type.shape.dim]
    metadata = {
        "source_weights": weights.name,
        "ultralytics": ultralytics.__version__,
        "opset": graph.opset_import[0].version,
        "input_name": input_tensor.name,
        "input_shape": shape,
        "output_names": [item.name for item in graph.graph.output],
        "sha256": sha256(output),
        "bytes": output.stat().st_size,
        "color_order": "RGB",
        "mean": [0.0, 0.0, 0.0],
        "scale": [1.0 / 255.0] * 3,
        "model_type": "yolo11",
        "labels": ["ball"],
    }
    output.with_suffix(".json").write_text(
        json.dumps(metadata, indent=2) + "\n", encoding="ascii"
    )
    print(json.dumps(metadata, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
