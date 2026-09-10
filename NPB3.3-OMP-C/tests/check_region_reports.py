#!/usr/bin/env python3
"""Check parallel/for reports, combined equality, and nowait source ranges."""
import argparse
import hashlib
import json
from pathlib import Path
import re

from report_region_times import parse_log, TOTAL_NAMES

ROOT = Path(__file__).resolve().parents[1]
MANUAL = re.compile(r"\bNPB_(?:PARALLEL_FOR|PARALLEL|FOR)_(?:BEGIN|END)\s*\(|\bnpb_time_(?:start|stop)\s*\(")


def check_sources():
    count = 0
    for bench in TOTAL_NAMES:
        directory = ROOT / bench / "src"
        assert not list(directory.glob("region_info.*")), directory
        for source in directory.iterdir():
            if source.suffix in (".c", ".h", ".incl"):
                assert not MANUAL.search(source.read_text()), source
                count += 1
    return count


def check_manifest(path):
    manifest = json.loads(path.read_text())
    sources = {name: Path(name).read_text() for name in manifest["sources"]}
    for name, digest in manifest["source_sha256"].items():
        assert hashlib.sha256(sources[name].encode()).hexdigest() == digest, (path, "stale source", name)
    regions = manifest["regions"]
    assert [r["id"] for r in regions] == list(range(len(regions))), path
    for r in regions:
        lines = sources[r["file"]].splitlines()
        pragma = lines[r["line"] - 1].strip()
        expected = {"parallel": "parallel", "combined": "parallel for", "for": "for"}[r["kind"]]
        assert pragma.startswith("#pragma omp " + expected), (path, r)
        assert r["line"] <= r["end_line"] <= len(lines), (path, r)
        label = f"{r['function']}:{r['line']}"
        if r["nowait"]:
            label += f"-{r['end_line']} (nowait)"
            for line in r["loop_lines"]:
                assert lines[line - 1].strip().startswith("#pragma omp for"), (path, r)
        assert r["label"] == label, (path, r)
        if r["kind"] == "for":
            assert 0 <= r["parent"] < len(regions), (path, r)
            assert regions[r["parent"]]["kind"] == "parallel", (path, r)
        else:
            assert r["parent"] == -1, (path, r)
    return {r["id"]: (r["label"], r["parent"], r["kind"] == "combined") for r in regions}


def check_report(path, manifest_path):
    result = parse_log(path)
    text = path.read_text()
    assert "kernel region" not in text and "iteration region" not in text, path
    rows = result["rows"]
    assert rows and all(row["level"] in ("parallel", "for") for row in rows), path
    by_key = {(row["level"], row["region"]): row for row in rows}
    assert len(rows) == len(by_key), path
    shares = re.findall(r"  step: ([\d.]+)%", text)
    assert len(shares) == len(rows), path
    total = result["total"]
    assert total > 0, path
    table = check_manifest(manifest_path)
    expected = {}
    for key, (name, owner, combined) in table.items():
        kind = "parallel" if owner == -1 else "for"
        expected[kind, name] = None if owner == -1 else table[owner][0]
        if combined:
            expected["for", name] = name
    for row, share in zip(rows, shares):
        key = row["level"], row["region"]
        assert key in expected, (path, row)
        tolerance = 0.0005 + 100 * 0.5e-9 / total * (1 + row["seconds"] / total)
        assert abs(float(share) - row["percent_total"]) <= tolerance, (path, row)
        if row["level"] == "parallel":
            assert row["depth"] == 0 and row["parent"] == "iteration total", (path, row)
            parent_seconds = total
        else:
            assert row["depth"] == 1 and row["parent"] == expected[key], (path, row)
            parent_seconds = by_key["parallel", row["parent"]]["seconds"]
        assert row["seconds"] <= parent_seconds + 1e-9, (path, row)
        assert abs(row["percent_parent"] - 100 * row["seconds"] / parent_seconds) < 1e-8
    for name, owner, combined in table.values():
        if combined and ("parallel", name) in by_key:
            p, f = by_key["parallel", name], by_key["for", name]
            assert p["seconds"] == f["seconds"] and p["percent_total"] == f["percent_total"], (path, name)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, nargs="?")
    parser.add_argument("--build-dir", type=Path, default=ROOT / ".build")
    parser.add_argument("--class", dest="npb_class", default="S")
    args = parser.parse_args()
    print(f"PASS: {check_sources()} source files have no manual region instrumentation")
    if args.directory:
        logs = [args.directory / (bench + ".log") for bench in TOTAL_NAMES
                if (args.directory / (bench + ".log")).exists()]
        if not logs:
            parser.error("no benchmark logs found")
        for path in logs:
            manifest = args.build_dir / (path.stem + "." + args.npb_class) / "instrumented/instrumentation.json"
            result = check_report(path, manifest)
            print(f"PASS {path.stem}: {len(result['rows'])} rows; parallel parents, combined equality, percentages")


if __name__ == "__main__":
    main()
