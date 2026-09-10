#!/usr/bin/env python3
"""Check parallel/for reports, combined equality, and nowait source ranges."""
import argparse
from pathlib import Path
import re

from report_region_times import parse_log, TOTAL_NAMES

ROOT = Path(__file__).resolve().parents[1]
ENTRY = re.compile(r'\[(R_\w+)\] = \{"([^"]+)", ([\w-]+), ([01])\}')


def region_table(bench):
    source_dir = ROOT / bench / "src"
    entries = {key: (name, parent, combined == "1") for key, name, parent, combined
               in ENTRY.findall((source_dir / "region_info.c").read_text())}
    return entries


def check_source_ranges():
    count = 0
    for bench in TOTAL_NAMES:
        entries = region_table(bench)
        for source in (ROOT / bench / "src").glob("*.c"):
            lines = source.read_text().splitlines()
            for index, line in enumerate(lines):
                begin = re.search(r"NPB_FOR_BEGIN\((R_\w+)\)", line)
                explicit = re.search(r"npb_time_start\((R_\w+)\)", line)
                match = begin or explicit
                if not match:
                    continue
                key = match[1]
                if entries[key][1] == "-1":
                    continue
                marker = "NPB_FOR_END()" if begin else f"npb_time_stop({key})"
                end = next(i for i in range(index + 1, len(lines)) if marker in lines[i])
                first = next(i for i in range(index + 1, end)
                             if re.search(r"#pragma omp for\b", lines[i]))
                name = entries[key][0]
                if "nowait" in lines[first]:
                    assert name.endswith(f":{first + 1}-{end + 1} (nowait)"), (source, key, name)
                    count += 1
                else:
                    assert "(nowait)" not in name and "-" not in name, (source, key, name)
    return count


def check_report(path):
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
    table = region_table(path.stem)
    expected = {}
    for key, (name, owner, combined) in table.items():
        kind = "parallel" if owner == "-1" else "for"
        expected[kind, name] = None if owner == "-1" else table[owner][0]
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
    args = parser.parse_args()
    print(f"PASS: {check_source_ranges()} nowait source ranges")
    if args.directory:
        logs = [args.directory / (bench + ".log") for bench in TOTAL_NAMES
                if (args.directory / (bench + ".log")).exists()]
        if not logs:
            parser.error("no benchmark logs found")
        for path in logs:
            result = check_report(path)
            print(f"PASS {path.stem}: {len(result['rows'])} rows; parallel parents, combined equality, percentages")


if __name__ == "__main__":
    main()
