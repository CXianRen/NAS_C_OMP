#!/usr/bin/env python3
"""Compare SP logs; an uncontrolled slowdown is not pure tuner overhead."""
import argparse
import csv
import json
from pathlib import Path
import re

NUMBER = r'[0-9]+(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?'


def parse_log(path):
    path = Path(path)
    text = path.read_text(errors='replace')

    def field(pattern):
        match = re.search(pattern, text, re.MULTILINE)
        return match.group(1).strip() if match else None

    precise = re.findall(rf'^\s*iteration total:\s*({NUMBER})\s+s\s*$', text, re.MULTILINE)
    wrapped = re.findall(rf'^OVERHEAD iteration_seconds=({NUMBER})\s*$', text, re.MULTILINE)
    rounded = re.findall(rf'^\s*Time in seconds\s*=\s*({NUMBER})\s*$', text, re.MULTILINE)
    values = wrapped or precise or rounded
    if len(values) != 1 or float(values[0]) <= 0:
        raise ValueError(f'{path}: expected one positive iteration time; do not concatenate runs')
    parents = {}
    for name, seconds in re.findall(rf'^parallel region\s+(\S+)\s+({NUMBER})\s+s\b', text, re.MULTILINE):
        if name in parents:
            raise ValueError(f'{path}: duplicate parent region {name}')
        parents[name] = float(seconds)
    size = field(r'^\s*Size\s*=\s*(.+)$') or field(r'^\s*Size:\s*(.+)$')
    return {
        'path': str(path), 'seconds': float(values[0]),
        'time_source': 'wrapper' if wrapped else 'iteration total' if precise else 'rounded NPB time',
        'benchmark': field(r'-\s+(\w+) Benchmark'),
        'class': field(r'^\s*Class\s*=\s*(\S+)'),
        'size': re.sub(r'\s+', '', size) if size else None,
        'iterations': field(r'^\s*Iterations\s*=\s*(\d+)'),
        'verification': field(r'^\s*Verification\s*=\s*(\S+)'),
        'startup_max_threads': field(r'^\s*Number of available threads:\s*(\d+)'),
        'final_threads': field(r'^\s*Total threads\s*=\s*(\d+)'),
        'compile_date': field(r'^\s*Compile date\s*=\s*(.+)$'),
        'command': field(r'^Command:\s*(.+)$'), 'parents': parents,
        'child_labels': re.findall(r'^\s+for region\s+(\S+)', text, re.MULTILINE),
        'offline_configs': {name: {'threads': int(n), 'mask': mask} for name, n, mask in re.findall(
            r'^Offline config region=(\S+) threads=(\d+) mask=(\S+)', text, re.MULTILINE)},
    }


def compare(baseline, candidate):
    for key in ('benchmark', 'class', 'size', 'iterations'):
        if baseline[key] is not None and candidate[key] is not None and baseline[key] != candidate[key]:
            raise ValueError(f'incomparable workload: {key}: {baseline[key]} != {candidate[key]}')
    delta = candidate['seconds'] - baseline['seconds']
    warnings = ['单次日志对比只表示耗时差，不能证明因果或稳定性；不把差值直接称为纯 offline overhead。']
    for label, item in (('baseline', baseline), ('candidate', candidate)):
        if item['verification'] != 'SUCCESSFUL':
            warnings.append(f'{label}: 未确认 Verification = SUCCESSFUL。')
        if item['time_source'] == 'rounded NPB time':
            warnings.append(f'{label}: 只有 NPB 四舍五入时间，小幅差异受打印精度限制。')
        missing = [k for k in ('benchmark', 'class', 'size', 'iterations') if item[k] is None]
        if missing:
            warnings.append(f'{label}: 缺少工作负载元数据 {", ".join(missing)}。')
    if baseline['startup_max_threads'] != candidate['startup_max_threads']:
        warnings.append('启动最大线程数不同；该数值不是初始化实际 team size 的测量。')
    if baseline['final_threads'] != candidate['final_threads']:
        warnings.append('最终线程数不同，比较同时包含并行度变化。')
    if baseline['compile_date'] != candidate['compile_date']:
        warnings.append('编译日期不同；没有同源码、同编译器和同 runtime 的控制证据。')
    if baseline['child_labels'] != candidate['child_labels']:
        warnings.append('for 子区间标签/分组不同，不逐项对齐子区间，也不将子区间与父区间相加。')
    left, right = baseline['parents'], candidate['parents']
    if set(left) != set(right):
        warnings.append('顶层 region 集合不同；region 表仅对比共有键，不能视为完整差值归因。')
    rows = []
    for key in left.keys() & right.keys():
        diff = right[key] - left[key]
        rows.append({'region': key, 'baseline_s': left[key], 'candidate_s': right[key],
                     'delta_s': diff, 'change_percent': 100 * diff / left[key] if left[key] else None,
                     'share_of_total_delta_percent': 100 * diff / delta if delta else None})
    rows.sort(key=lambda row: (-row['delta_s'], row['region']))
    decomposition = None
    if left and right:
        a, b = sum(left.values()), sum(right.values())
        decomposition = {'baseline_parent_s': a, 'candidate_parent_s': b, 'parent_delta_s': b - a,
                         'baseline_residual_s': baseline['seconds'] - a,
                         'candidate_residual_s': candidate['seconds'] - b,
                         'residual_delta_s': delta - (b - a),
                         'parent_share_of_total_delta_percent': 100 * (b - a) / delta if delta else None}
        warnings.append('total − 顶层合计是区间外残差，包含控制、串行工作和计时边界差异；不是纯 callback 成本。')
        if min(decomposition['baseline_residual_s'], decomposition['candidate_residual_s']) < -0.001:
            warnings.append('顶层累计超过 total；请检查区间重叠、时间精度或报告口径。')
    else:
        warnings.append('至少一份日志没有顶层 region 报告，无法计算区间外残差。')
    return {'baseline': baseline, 'candidate': candidate, 'delta_s': delta,
            'change_percent': 100 * delta / baseline['seconds'],
            'decomposition': decomposition, 'regions': rows, 'warnings': warnings}


def render(result):
    a, b, d = result['baseline'], result['candidate'], result['decomposition']
    lines = ['# SP 日志耗时对比', '', f"Baseline: `{a['path']}`", f"Candidate: `{b['path']}`", '',
             f"iteration total: **{a['seconds']:.9f} → {b['seconds']:.9f} s**，"
             f"差值 **{result['delta_s']:+.9f} s ({result['change_percent']:+.2f}%)**。", '',
             '| 项目 | Baseline | Candidate |', '| --- | --- | --- |']
    for key in ('class', 'size', 'iterations', 'verification', 'startup_max_threads', 'final_threads', 'compile_date', 'time_source'):
        lines.append(f"| {key} | {a[key] or 'unknown'} | {b[key] or 'unknown'} |")
    if d:
        lines += ['', '| 时间分解 | Baseline (s) | Candidate (s) | 差值 (s) |',
                  '| --- | ---: | ---: | ---: |',
                  f"| 顶层 region 合计 | {d['baseline_parent_s']:.9f} | {d['candidate_parent_s']:.9f} | {d['parent_delta_s']:+.9f} |",
                  f"| total − 顶层合计 | {d['baseline_residual_s']:.9f} | {d['candidate_residual_s']:.9f} | {d['residual_delta_s']:+.9f} |"]
    if result['regions']:
        lines += ['', '| 顶层 region | Baseline (s) | Candidate (s) | 差值 (s) | 变化 | 占总差 |',
                  '| --- | ---: | ---: | ---: | ---: | ---: |']
        for row in result['regions']:
            percent = 'n/a' if row['change_percent'] is None else f"{row['change_percent']:+.2f}%"
            share = 'n/a' if row['share_of_total_delta_percent'] is None else f"{row['share_of_total_delta_percent']:.2f}%"
            lines.append(f"| {row['region']} | {row['baseline_s']:.9f} | {row['candidate_s']:.9f} | {row['delta_s']:+.9f} | {percent} | {share} |")
    lines += ['', '注意：', ''] + [f'- {warning}' for warning in result['warnings']]
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=Path)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('--out', type=Path, help='write comparison.md, regions.csv and comparison.json')
    args = parser.parse_args()
    try:
        result = compare(parse_log(args.baseline), parse_log(args.candidate))
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    report = render(result)
    if args.out:
        args.out.mkdir(parents=True, exist_ok=True)
        (args.out / 'comparison.md').write_text(report)
        (args.out / 'comparison.json').write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n')
        with (args.out / 'regions.csv').open('w', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=['region', 'baseline_s', 'candidate_s', 'delta_s', 'change_percent', 'share_of_total_delta_percent'])
            writer.writeheader()
            writer.writerows(result['regions'])
    print(report, end='')


if __name__ == '__main__':
    main()
