from __future__ import annotations

import argparse
import random
import zipfile
from pathlib import Path


IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".bmp"}


def images_in(path: Path) -> list[Path]:
    return sorted(
        item
        for item in path.iterdir()
        if item.is_file() and item.suffix.lower() in IMAGE_EXTENSIONS
    )


def label_has_ball(image_path: Path, labels_dir: Path) -> bool:
    label_path = labels_dir / f"{image_path.stem}.txt"
    return label_path.exists() and bool(label_path.read_text(encoding="ascii").strip())


def take_evenly(items: list[Path], count: int) -> list[Path]:
    if count >= len(items):
        return list(items)
    return [items[(index * len(items)) // count] for index in range(count)]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build a representative 100-image MaixHub INT8 calibration ZIP."
    )
    parser.add_argument("--dataset", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--count", type=int, default=100)
    parser.add_argument(
        "--ball-count",
        type=int,
        default=None,
        help="number of positive ball images (default: 80%% of --count)",
    )
    args = parser.parse_args()

    if not 20 <= args.count <= 100:
        raise ValueError("MaixHub requires 20 to 100 calibration images")
    requested_ball_count = (
        round(args.count * 0.8) if args.ball_count is None else args.ball_count
    )
    if not 0 <= requested_ball_count <= args.count:
        raise ValueError("--ball-count must be between 0 and --count")

    image_dirs = [args.dataset / "images" / split for split in ("val", "test")]
    label_dirs = [args.dataset / "labels" / split for split in ("val", "test")]
    balls: list[Path] = []
    empty: list[Path] = []
    for image_dir, label_dir in zip(image_dirs, label_dirs):
        for image_path in images_in(image_dir):
            (balls if label_has_ball(image_path, label_dir) else empty).append(image_path)

    random.Random(20260730).shuffle(balls)
    random.Random(20260731).shuffle(empty)
    ball_count = min(len(balls), requested_ball_count)
    empty_count = min(len(empty), args.count - ball_count)
    selected = take_evenly(balls, ball_count) + take_evenly(empty, empty_count)
    if len(selected) < args.count:
        remaining = [item for item in balls + empty if item not in selected]
        selected.extend(take_evenly(remaining, args.count - len(selected)))
    if len(selected) != args.count:
        raise RuntimeError(f"only found {len(selected)} suitable calibration images")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "w", zipfile.ZIP_DEFLATED) as archive:
        for index, image_path in enumerate(selected):
            archive.write(image_path, f"{index:03d}{image_path.suffix.lower()}")

    selected_ball_count = sum(item in balls for item in selected)
    selected_empty_count = len(selected) - selected_ball_count
    print(
        f"wrote {args.output.resolve()} with {len(selected)} images "
        f"({selected_ball_count} ball, {selected_empty_count} empty)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
