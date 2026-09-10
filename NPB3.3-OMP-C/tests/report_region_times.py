#!/usr/bin/env python3
"""Convert BENCH.log timing reports into region_times.md and region_times.csv."""
import argparse
import csv
import json
from pathlib import Path
import re


TOTAL_NAMES = {
    "BT": "total", "SP": "total", "LU": "total", "CG": "benchmk",
    "MG": "benchmk", "FT": "total", "EP": "Total time",
    "IS": "Benchmarking", "UA": "total", "DC": "Benchmark Time",
}
ROW = re.compile(r"^( *)(iteration|kernel|parallel|for) region (.+?)\s+([\d.eE+-]+) s"
                 r"(?:  step: [\d.eE+-]+%)?((?:  \[[^\]\n]+\])*)[ \t]*$",
                 re.MULTILINE)


def percent(seconds, denominator):
    return 100 * seconds / denominator if denominator else None


def parse_log(path):
    text = path.read_text(errors="replace")
    verification = re.findall(r"^\s*Verification\s*=\s*(.+)$", text, re.MULTILINE)
    reported_class = re.search(r"^\s*Class\s*=\s*(\S+)", text, re.MULTILINE)
    result = {"benchmark": path.stem, "verification": verification[-1].strip()
              if verification else "未记录", "class": reported_class[1]
              if reported_class else "未记录", "rows": [], "total": None}
    if "time report" not in text:
        return result
    report = text.rsplit("time report", 1)[1]
    iteration_total = re.search(r"^iteration total: ([\d.eE+-]+) s$",
                                report, re.MULTILINE)
    records = ROW.findall(report)
    parallel_only = not any(row[1] in ("iteration", "kernel") for row in records)
    if iteration_total:
        result["total"] = float(iteration_total[1])
    kernel = parallel = None
    tree = bool(re.search(r"^iteration region ", report, re.MULTILINE))
    stack = []
    for indent, level, name, seconds, notes in records:
        # Older logs label the inclusive iteration timer as a kernel.
        if level == "kernel" and name == TOTAL_NAMES[path.stem]:
            level = "iteration"
        row = {"benchmark": path.stem, "level": level, "region": name,
               "seconds": float(seconds), "scope": notes.strip(),
               "depth": len(indent) // 4}
        if parallel_only:
            if level == "parallel":
                if indent:
                    raise ValueError(f"{path}: parallel must be at the top level: {name}")
                parallel = row
            elif level != "for" or len(indent) != 4 or parallel is None:
                raise ValueError(f"{path}: for region has no parallel parent: {name}")
            row["parallel"] = parallel["region"]
            row["parent"] = "iteration total" if level == "parallel" else parallel["region"]
            row["parent_level"] = "total" if level == "parallel" else "parallel"
            row["parent_seconds"] = result["total"] if level == "parallel" else parallel["seconds"]
            result["rows"].append(row)
            continue
        if tree:
            while stack and stack[-1][0] >= len(indent):
                stack.pop()
            if level != "iteration" and (not stack or len(indent) != stack[-1][0] + 4):
                raise ValueError(f"{path}: invalid enclosing scope: {name}")
            parent = stack[-1][1] if stack else None
            row["parent"] = parent["region"] if parent else ""
            row["parent_level"] = parent["level"] if parent else ""
            row["parent_seconds"] = parent["seconds"] if parent else None
            ancestors = [item for _, item in stack] + [row]
            row["kernel"] = next((item["region"] for item in reversed(ancestors)
                                  if item["level"] in ("kernel", "iteration")), "")
            row["parallel"] = next((item["region"] for item in reversed(ancestors)
                                    if item["level"] == "parallel"), "")
            stack.append((len(indent), row))
            result["rows"].append(row)
            if level == "iteration":
                result["total"] = row["seconds"]
            continue
        if level in ("iteration", "kernel"):
            kernel, parallel = row, None
        elif kernel is None or (level == "for" and parallel is None):
            raise ValueError(f"{path}: {level} region has no parent: {name}")
        row["kernel"] = kernel["region"]
        row["parallel"] = name if level == "parallel" else (
            parallel["region"] if level == "for" else "")
        row["parent"] = kernel["region"] if level == "parallel" else (
            parallel["region"] if level == "for" else (
                "" if level == "iteration" else TOTAL_NAMES[path.stem]))
        row["parent_seconds"] = kernel["seconds"] if level == "parallel" else (
            parallel["seconds"] if level == "for" else None)
        row["parent_level"] = (kernel["level"] if level == "parallel" else
                               "parallel" if level == "for" else
                               "iteration" if level == "kernel" else "")
        result["rows"].append(row)
        if level == "parallel":
            parallel = row
        if level == "iteration":
            result["total"] = row["seconds"]
    if iteration_total:
        result["total"] = float(iteration_total[1])
    for row in result["rows"]:
        row["percent_total"] = percent(row["seconds"], result["total"])
        row["percent_parent"] = None if row["level"] == "iteration" else percent(
            row["seconds"], result["total"] if not tree and row["level"] == "kernel"
            else row["parent_seconds"])
    return result


def fmt(value, suffix=""):
    return "—" if value is None else f"{value:.3f}{suffix}"


def cell(value):
    return str(value).replace("|", "\\|").replace("\n", " ")


def largest(result, level):
    rows = [row for row in result["rows"] if row["level"] == level]
    if not rows:
        return "—"
    row = max(rows, key=lambda item: item["seconds"])
    return f'{row["region"]} ({fmt(row["percent_total"], "%")})'


def write_reports(directory, results):
    lines = ["# NAS region 耗时占比", ""]
    metadata = directory / "metadata.json"
    values = {}
    if metadata.exists():
        values = json.loads(metadata.read_text())
        configuration = [f"{key}={values[key]}" for key in
                         ("threads", "OMP_PLACES", "OMP_PROC_BIND", "OMP_DYNAMIC")
                         if key in values]
        if "class" in values:
            lines += [f'问题规模：Class {cell(values["class"])}。', ""]
        lines += [f'环境：{cell(values.get("cpu", ""))}；'
                  + "；".join(map(cell, configuration)) + "。", ""]
        for key, label in (("iterations", "迭代设置"), ("scope", "范围")):
            if key in values:
                lines += [f"{label}：{cell(values[key])}。", ""]
        lines += ["[完整运行配置与命令](metadata.json)。", ""]
    lines += [
        "占总时间（平均 time step 占比）= region 累计耗时 / 正式 iteration 累计耗时。"
        "父级比例：parallel / iteration total，for / 所属 parallel。",
        "",
    ]
    if ("reduced" in str(values.get("class", "")).lower()
            or "NPB_NITER=" in str(values.get("iterations", ""))):
        lines += [
            "本次缩短迭代的条目（见上方 iterations）不再对应标准迭代次数的参考结果。"
            "其 UNSUCCESSFUL 或 Class U 按原日志照录，不据此判定数值回归；"
            "EP / IS 保留完整 Class B 工作量，仍按标准参考结果验证。",
            "",
        ]
        if not any(result["benchmark"] == "DC" for result in results):
            lines += ["DC 不含本次所需的 iteration / omp for 层级，本次不运行。", ""]
    lines += [
        "| Benchmark | iteration 总时间 (s) | 数值验证 | 最大 parallel（占总时间） | 最大 for（占总时间） |",
        "| --- | ---: | --- | --- | --- |",
    ]
    for result in results:
        values = [result["benchmark"], fmt(result["total"]), result["verification"],
                  largest(result, "parallel"), largest(result, "for")]
        lines.append("| " + " | ".join(map(cell, values)) + " |")
    lines += [
        "",
        "parallel 与 for 的时间有重叠；combined parallel for 在两层显示同一份耗时，不能相加。",
        "nowait 名称中的起止行号对应首个 nowait pragma 与现有计时结束标记，连续 nowait 组仍累计为一项。",
        "只由 master 读钟；nowait 组在既定代码边界结束，不增加同步，因此不代表所有线程完成该组的耗时。",
        "",
    ]
    for result in results:
        bench = result["benchmark"]
        lines += [f"## {bench}", "", f"原始日志：[{bench}.log]({bench}.log)。"
                  f"总时间优先取 `iteration total`，旧日志取 `{TOTAL_NAMES[bench]}`。"
                  f'日志 Class: `{result["class"]}`；数值验证: `{result["verification"]}`。', ""]
        if not result["rows"]:
            lines += ["日志没有 time report，无法计算占比。", ""]
            continue
        if result["total"] is None:
            lines += ["日志缺少 iteration 总耗时，占总时间一列留空。", ""]
        lines += [
            "| 层级 | Region | 父级 | 累计耗时 (s) | 占总时间 | 占父级 |",
            "| --- | --- | --- | ---: | ---: | ---: |",
        ]
        for row in result["rows"]:
            values = [row["level"], row["region"],
                      row["parent"] if row["parent_level"] == "total" else
                      (row["parent_level"] + " " + row["parent"]).strip(),
                      f'{row["seconds"]:.9f}', fmt(row["percent_total"], "%"),
                      fmt(row["percent_parent"], "%")]
            lines.append("| " + " | ".join(map(cell, values)) + " |")
        lines.append("")
    (directory / "region_times.md").write_text("\n".join(lines))
    columns = ["benchmark", "level", "parallel", "region", "parent",
               "seconds", "percent_total", "percent_parent", "parent_level", "depth"]
    with (directory / "region_times.csv").open("w", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=columns, extrasaction="ignore")
        writer.writeheader()
        for result in results:
            writer.writerows(result["rows"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="directory containing BENCH.log files")
    directory = parser.parse_args().directory
    logs = [directory / f"{bench}.log" for bench in TOTAL_NAMES
            if (directory / f"{bench}.log").is_file()]
    if not logs:
        parser.error("no benchmark logs found")
    write_reports(directory, [parse_log(path) for path in logs])
    print(directory / "region_times.md")
    print(directory / "region_times.csv")


if __name__ == "__main__":
    main()
