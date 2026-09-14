#!/usr/bin/env python3
"""Render the transposed binding snapshots as a standalone SVG (stdlib only)."""
import argparse
from html import escape
from pathlib import Path
import re
import xml.etree.ElementTree as ET


FIELDS = ("tid", "cpu", "place", "place_cpus", "allowed_cpus")


def read_results(path):
    content = path.read_text()
    host = re.search(r"^@(.+)$", content, re.M)
    blocks = []
    for match in re.finditer(r"^=== (\w+) ===\s*\n(.*?)(?=^=== |\Z)", content, re.M | re.S):
        policy, body = match.groups()
        metadata = dict(re.findall(r"(\w+)=([^\s]+)", body))
        rows = {}
        for field in FIELDS:
            row = re.search(rf"^{field}\s+(.+)$", body, re.M)
            if row is None:
                raise ValueError(f"{policy}: missing {field}")
            rows[field] = row.group(1).split()
        size = int(metadata["team_size"])
        if any(len(values) != size for values in rows.values()):
            raise ValueError(f"{policy}: row length does not match team_size")
        if len(set(rows["tid"])) != size:
            raise ValueError(f"{policy}: duplicate tid")
        blocks.append((policy, metadata, rows))
    if not blocks:
        raise ValueError("No binding snapshots found")
    return host.group(1) if host else path.stem, blocks


def render(sources, output, title=None, compare=False):
    snapshots = []
    hosts = []
    for source in sources:
        host, source_blocks = read_results(source)
        hosts.append(host)
        snapshots.extend((source, block) for block in source_blocks)
    if len(sources) > 1:
        snapshots.sort(key=lambda item: int(item[1][1]["team_size"]))
    blocks = [block for _, block in snapshots]
    host = title or (hosts[0] if len(set(hosts)) == 1 else "Combined results")
    columns, cell_width, cell_height = 8, 85, 32
    panel_heights = [76 + ((len(rows["tid"]) + columns - 1) // columns) * cell_height
                     for _, _, rows in blocks]
    width = 744
    height = 120 + sum(panel_heights) + 20 * (len(blocks) - 1) + 44
    if compare:
        paired = {}
        for source, block in snapshots:
            policy, meta, rows = block
            size = int(meta["team_size"])
            if policy not in ("close", "spread"):
                raise ValueError("Comparison layout supports close and spread only")
            if any(not 0 <= int(cpu) < 64 for cpu in rows["cpu"]):
                raise ValueError("Comparison layout requires CPU IDs in 0..63")
            if policy in paired.setdefault(size, {}):
                raise ValueError(f"Duplicate {policy} snapshot for {size} threads")
            paired[size][policy] = (source, block)
        snapshots = []
        for size in sorted(paired):
            if set(paired[size]) != {"close", "spread"}:
                raise ValueError(f"Need both close and spread for {size} threads")
            snapshots.extend(paired[size][policy] for policy in ("close", "spread"))
        panel_heights = [76 + 8 * cell_height] * len(snapshots)
        width = 1488
        height = 120 + len(paired) * (panel_heights[0] + 20) + 24
    description = ("Close is on the left and spread on the right, grouped by thread count. "
                   "Each panel has 64 CPU slots, ordered by CPU ID. Each cell shows CPU ID followed by the recorded tids; gray means no recorded thread. " if compare else
                   "Binding policies are stacked vertically. Each cell shows tid followed by CPU ID, ordered by tid. ")
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" role="img" aria-labelledby="title desc">',
             '<title id="title">OpenMP CPU occupancy</title>' if compare else '<title id="title">OpenMP tid to CPU ID</title>',
             f'<desc id="desc">{escape(description)}</desc>',
             '<style>text{font-family:DejaVu Sans,Arial,sans-serif;fill:#172b42}</style>']

    def rect(x, y, w, h, fill, stroke="none", radius=0):
        parts.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{radius}" fill="{fill}" stroke="{stroke}"/>')

    def text(x, y, value, size=14, color=None, anchor="start", weight="normal"):
        fill = f' style="fill:{color}"' if color else ""
        parts.append(f'<text x="{x}" y="{y}" font-size="{size}" text-anchor="{anchor}" font-weight="{weight}"{fill}>{escape(str(value))}</text>')

    rect(0, 0, width, height, "#f3f6fa")
    text(24, 42, f"{host} / Thread binding", 26, weight="bold")
    legend = "Each cell: CPU ID : tid  |  Gray = no recorded thread" if compare else "Each cell: tid : CPU ID"
    text(24, 71, legend, 17, weight="bold")
    source_label = f"Source: {sources[0].name}" if len(sources) == 1 else f"Sources: {len(sources)} files / {len(blocks)} snapshots"
    text(24, 94, f"{source_label}  |  CPU ID from the recorded cpu column", 13, "#607187")
    colors = {"close": "#087f8c", "spread": "#3569c8", "primary": "#bf5b16"}

    top = 120
    for panel_index, ((source, (policy, meta, rows)), panel_height) in enumerate(zip(snapshots, panel_heights)):
        left = 744 * (panel_index % 2) if compare else 0
        if compare:
            top = 120 + (panel_index // 2) * (panel_height + 20)
        color = colors.get(policy, "#7755a8")
        rect(left + 16, top, 712, panel_height, "#ffffff", "#dce4ed", 12)
        text(left + 32, top + 34, policy, 23, color, weight="bold")
        text(left + 712, top + 33,
             f'{meta["team_size"]} threads / OMP_PLACES={meta["OMP_PLACES"]}',
             13, "#607187", "end")
        records = {}
        for i, slot in enumerate(rows["cpu"] if compare else rows["tid"]):
            records.setdefault(int(slot), []).append(i)
        order = range(64) if compare else sorted(records)
        for position, slot in enumerate(order):
            indices = sorted(records.get(slot, []), key=lambda i: int(rows["tid"][i]))
            active = bool(indices)
            x = left + 32 + (position % columns) * cell_width
            y = top + 56 + (position // columns) * cell_height
            detail = " | ".join("; ".join(f"{field}={rows[field][i]}" for field in FIELDS) for i in indices) if active else f"cpu={slot}; no recorded thread"
            tooltip = f"source={source.name}; " + detail
            slot_attribute = "data-cpu" if compare else "data-tid"
            parts.append(f'<g data-policy="{policy}" data-team="{meta["team_size"]}" {slot_attribute}="{slot}" data-active="{str(active).lower()}"><title>{escape(tooltip)}</title>')
            rect(x, y, cell_width - 5, cell_height - 5, color if active else "#e3e8ef", radius=4)
            values = ",".join(rows["tid" if compare else "cpu"][i] for i in indices) or "—"
            text(x + (cell_width - 5) / 2, y + 19,
                 f'{slot}: {values}', 14,
                 "#ffffff" if active else "#788697", "middle")
            parts.append('</g>')
        top += panel_height + 20

    footer = "CPU ID = Linux logical CPU number; physical core ID is not recorded."
    if compare:
        footer += " Cells follow CPU numbering, not physical chip geometry."
    text(24, height - 18, footer, 12, "#607187")
    parts.append("</svg>")
    svg = "\n".join(parts) + "\n"
    ET.fromstring(svg)
    output.write_text(svg)
    print(f"Wrote {output}: {len(blocks)} policies, {sum(len(b[2]['tid']) for b in blocks)} thread records")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", nargs="*", type=Path, default=[Path(__file__).with_name("result.md")])
    parser.add_argument("-o", "--output", type=Path)
    parser.add_argument("--title", help="Override the host name in the figure title")
    parser.add_argument("--compare", action="store_true", help="Pair close/spread horizontally with 64 CPU ID: tid cells per panel")
    args = parser.parse_args()
    if len(args.source) > 1 and args.output is None:
        parser.error("--output is required when combining multiple source files")
    render(args.source, args.output or args.source[0].with_suffix(".svg"), args.title, args.compare)
