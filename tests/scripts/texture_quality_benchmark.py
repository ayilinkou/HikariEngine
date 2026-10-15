#!/usr/bin/env python3
"""Run TC17's manual review with isolated sources/outputs and the shared Release cooker."""

import argparse
import datetime
import hashlib
import json
import os
import pathlib
import platform
import shutil
import statistics
import subprocess
import time


ROOT = pathlib.Path(__file__).resolve().parents[2]
CASES = {
    "color": "models/pbr_case/textures/13_-_Default_baseColor.png",
    "normal": "models/pbr_case/textures/13_-_Default_normal.png",
    "packed": "models/pbr_case/textures/13_-_Default_metallicRoughness.png",
    "cube": "textures/skybox/sky.cubemap.json",
}
VARIANTS = [("auto-q1", "auto", 1.0), ("auto-q05", "auto", 0.5), ("bc1-q1", "bc1", 1.0)]


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def run(command, log):
    start = time.perf_counter()
    with log.open("w", encoding="utf-8") as stream:
        completed = subprocess.run([str(value) for value in command], stdout=stream, stderr=subprocess.STDOUT)
    elapsed = (time.perf_counter() - start) * 1000
    if completed.returncode:
        raise RuntimeError(f"Command failed with exit {completed.returncode}; see {log}")
    return elapsed


def copy_source(case, directory, compression):
    original = ROOT / "content" / CASES[case]
    source = directory / original.name
    shutil.copy2(original, source)
    settings_path = source if case == "cube" else source.with_name(source.name + ".texture.json")
    settings = json.loads((original if case == "cube" else original.with_name(original.name + ".texture.json")).read_text())
    if compression != "auto":
        if settings["usage"] != "color" or settings["alphaMode"] != "opaque":
            raise RuntimeError("BC1 comparison requires authored opaque color usage")
        settings["format"] = compression
    if case == "cube":
        for face in settings["faces"].values():
            shutil.copy2(original.parent / face, directory / face)
    write_json(settings_path, settings)
    return source


def review_page(output, summary):
    template = ROOT / "tests/tools/texture/quality_review.html"
    payload = json.dumps(summary).replace("<", "\\u003c")
    output.joinpath("review.html").write_text(
        template.read_text(encoding="utf-8").replace("__REVIEW_DATA__", payload),
        encoding="utf-8",
    )


def render_existing(args, output):
    """Add rendered evidence without rewriting the original timing/decoded reports."""
    summary = json.loads((output / "results.json").read_text(encoding="utf-8"))
    summary["renderUtc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    summary["renderProbe"] = str(args.probe)
    summary["renderProbeSha256"] = hashlib.sha256(args.probe.read_bytes()).hexdigest()
    for result in summary["results"]:
        if result["case"] not in args.cases:
            continue
        directory = output / result["case"] / result["variant"]
        outputs = list((directory / "cooked").rglob("*.ktx2"))
        if len(outputs) != 1:
            raise RuntimeError(f"Expected exactly one preserved cooked texture: {directory}")
        source = directory / "source" / pathlib.Path(CASES[result["case"]]).name
        print(f'Rendering {result["case"]} / {result["variant"]}...', flush=True)
        report = directory / "render-probe.json"
        run([args.probe, outputs[0], report, source, directory / "previews",
             args.backend, "--render-only"], directory / "render-probe.log")
        result["renderReview"] = json.loads(report.read_text(encoding="utf-8"))
        if result["renderReview"]["validationErrors"]:
            raise RuntimeError(f"Rendering produced validation errors: {directory}")
        write_json(output / "render-results.json", summary)
        review_page(output, summary)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cooker", type=pathlib.Path)
    parser.add_argument("--probe", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True,
                        help="New empty results directory, or existing results with --render-existing")
    parser.add_argument("--backend", choices=["Vulkan", "D3D12"], default="Vulkan")
    parser.add_argument("--cases", nargs="+", choices=list(CASES), default=list(CASES))
    parser.add_argument("--render-existing", action="store_true",
                        help="Render preserved outputs; keep original timings and decoded previews")
    args = parser.parse_args()
    args.probe = args.probe.resolve(strict=True)
    output = args.output.resolve()
    if args.render_existing:
        if not (output / "results.json").is_file():
            parser.error("--render-existing needs an existing results.json in --output")
        render_existing(args, output)
        print(f"Review: {output / 'review.html'}", flush=True)
        return
    if args.cooker is None:
        parser.error("--cooker is required for new measurements")
    args.cooker = args.cooker.resolve(strict=True)
    if output.exists() and any(output.iterdir()):
        parser.error("--output must be empty; previous measurements are never overwritten")
    output.mkdir(parents=True, exist_ok=True)
    summary = {"host": platform.platform(), "cpu": platform.processor(), "logicalCpus": os.cpu_count(),
               "utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "cooker": str(args.cooker), "cookerSha256": hashlib.sha256(args.cooker.read_bytes()).hexdigest(),
               "probe": str(args.probe),
               "probeSha256": hashlib.sha256(args.probe.read_bytes()).hexdigest(),
               "backend": args.backend,
               "method": "One cold cook per case/variant, three warm checks; cold-to-GPU is the sum of the CLI cook and a separate CPU/GPU load, excluding device creation. OS caches are not cleared. Rendering and decoding are excluded.",
               "results": []}
    for case in args.cases:
        for variant, compression, quality in VARIANTS:
            if compression == "bc1" and case not in ("color", "cube"):
                continue
            directory = output / case / variant
            source_root = directory / "source"
            cooked_root = directory / "cooked"
            source_root.mkdir(parents=True)
            source = copy_source(case, source_root, compression)
            command = [args.cooker, "cook", source, "--source-root", source_root,
                       "--cooked-root", cooked_root, "--quality", str(quality)]
            print(f"Cooking {case} / {variant}...", flush=True)
            cold = run(command, directory / "cold.log")
            outputs = list(cooked_root.rglob("*.ktx2"))
            if len(outputs) != 1 or "Cooked:" not in (directory / "cold.log").read_text():
                raise RuntimeError(f"Cold cook did not publish exactly one KTX2: {directory}")
            warm_times = []
            for iteration in range(3):
                log = directory / f"warm-{iteration}.log"
                warm_times.append(run(command, log))
                if "Unchanged:" not in log.read_text():
                    raise RuntimeError(f"Warm request unexpectedly recooked: {log}")
            record_path = outputs[0].with_suffix(".cook.json")
            record = json.loads(record_path.read_text())
            if record["encodingQuality"] != quality:
                raise RuntimeError("Cook record does not report the requested quality")
            run([args.probe, outputs[0], directory / "probe.json", source, directory / "previews", args.backend],
                directory / "probe.log")
            probe = json.loads((directory / "probe.json").read_text())
            result = {"case": case, "variant": variant, "quality": quality, "coldCookMs": cold,
                      "warmCookMedianMs": statistics.median(warm_times), "warmCookSamplesMs": warm_times,
                      "coldCookToGpuReadyMs": cold + probe["cpuLoadMs"] + probe["gpuCreateUploadFlushMs"],
                      "encoderVersions": record["versions"], "probe": probe}
            summary["results"].append(result)
            write_json(directory / "result.json", result)
            write_json(output / "results.json", summary)
            review_page(output, summary)
            print(f'{case} / {variant}: cold-to-GPU {result["coldCookToGpuReadyMs"] / 1000:.3f}s, '
                  f'warm check {result["warmCookMedianMs"]:.2f}ms, '
                  f'{probe["fileBytes"] / 1048576:.2f} MiB, base RGB PSNR {probe["previews"]["baseRgbPsnrDb"]}', flush=True)
    print(f"Review: {output / 'review.html'}", flush=True)


if __name__ == "__main__":
    main()
