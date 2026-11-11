"""Small dependency-free SVG plots; all points are supplied by saved benchmark summaries."""
from html import escape

COLORS = {"serial": "#59636e", "openmp": "#067d68", "mpi": "#a64c19", "hybrid": "#6355b4"}


def chart(path, title, ylabel, rows, metric, ideal=None):
    width, height = 880, 520
    left, right, top, bottom = 85, 825, 75, 425
    workers = sorted({int(row["workers"]) for row in rows})
    maximum = max(float(row[metric]) for row in rows) * 1.12
    if ideal is not None:
        maximum = max(maximum, max(ideal(worker) for worker in workers) * 1.12)
    if metric == "seconds":
        maximum = max(maximum, max(float(row["max_seconds"]) for row in rows) * 1.05)
    maximum = maximum or 1
    span = max(1, max(workers) - min(workers))
    x = lambda value: left + (right - left) * (value - min(workers)) / span
    y = lambda value: bottom - (bottom - top) * value / maximum
    svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
           f'viewBox="0 0 {width} {height}" role="img" aria-label="{escape(title)}">',
           '<rect width="100%" height="100%" fill="#fafbfc"/>',
           '<g font-family="sans-serif" font-size="13" fill="#233044">',
           f'<text x="35" y="32" font-size="21" font-weight="bold">{escape(title)}</text>',
           '<text x="35" y="53" fill="#59636e">Median of measured samples; prepared input '
           'to completed output</text>']
    for tick in range(6):
        value = maximum * tick / 5
        yy = y(value)
        svg += [f'<path d="M{left},{yy} H{right}" stroke="#dce2e8"/>',
                f'<text x="{left - 10}" y="{yy + 4}" text-anchor="end">{value:.3g}</text>']
    for worker in workers:
        svg.append(f'<text x="{x(worker)}" y="449" text-anchor="middle">{worker}</text>')
    svg += [f'<text x="455" y="478" text-anchor="middle">Workers (ranks × threads)</text>',
            f'<text transform="translate(20 270) rotate(-90)" text-anchor="middle">'
            f'{escape(ylabel)}</text>']
    if ideal:
        svg.append('<text x="700" y="53" fill="#59636e">Dashed: ideal</text>')
        points = ' '.join(f'{x(worker)},{y(ideal(worker))}' for worker in workers)
        svg.append(f'<polyline points="{points}" fill="none" stroke="#8993a0" '
                   'stroke-dasharray="5 5"/>')
    for position, (mode, color) in enumerate(COLORS.items()):
        selected = sorted((row for row in rows if row["mode"] == mode),
                          key=lambda row: int(row["workers"]))
        points = ' '.join(f'{x(int(row["workers"]))},{y(float(row[metric]))}' for row in selected)
        if not selected:
            continue
        svg.append(f'<polyline points="{points}" fill="none" stroke="{color}" stroke-width="2.5"/>')
        for row in selected:
            xx, yy = x(int(row["workers"])), y(float(row[metric]))
            if metric == "seconds":
                low, high = y(float(row["min_seconds"])), y(float(row["max_seconds"]))
                svg.append(f'<path d="M{xx},{low} V{high}" stroke="{color}" opacity="0.5"/>')
            svg.append(f'<circle cx="{xx}" cy="{yy}" r="4" fill="{color}"><title>'
                       f'{mode}: {float(row[metric]):.6g}</title></circle>')
        svg.append(f'<text x="{130 + position * 175}" y="507" fill="{color}">{mode}</text>')
    path.write_text('\n'.join(svg + ['</g></svg>']))


def make_graphs(directory, summaries):
    for filter_name in ("blur", "pixel"):
        target = directory / filter_name
        target.mkdir(exist_ok=True)
        selected = [row for row in summaries if row["filter"] == filter_name]
        sizes = sorted({int(row["width"]) for row in selected if row["experiment"] == "strong"})
        for size in sizes:
            rows = [row for row in selected if row["experiment"] == "strong"
                    and int(row["width"]) == size]
            prefix = f"{filter_name.title()} · {size}×{size}"
            chart(target / f"strong_scaling_time_{size}.svg", prefix + " · time",
                  "Seconds (whiskers: min–max)", rows, "seconds")
            chart(target / f"speedup_{size}.svg", prefix + " · speedup", "Serial / parallel", rows,
                  "speedup", ideal=lambda worker: worker)
            chart(target / f"efficiency_{size}.svg", prefix + " · efficiency",
                  "Speedup / workers", rows,
                  "efficiency", ideal=lambda worker: 1)
        rows = [row for row in selected if row["experiment"] == "weak"]
        chart(target / "weak_scalability.svg", filter_name.title() + " · weak scaling",
              "Time / same-mode one-worker time", rows, "normalized_time", ideal=lambda worker: 1)
