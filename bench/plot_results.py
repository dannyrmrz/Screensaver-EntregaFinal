"""plot_results.py — Calcula speedup y eficiencia desde bench/results/crudo.csv,
escribe resultados.csv, las tablas de la bitácora (tablas.md) y las gráficas PNG.

Uso: python3 bench/plot_results.py   (desde la raíz del repositorio)
"""
import csv
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

RES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "results")
VERSIONES = ["v0", "v1", "v2", "v3", "v3-fast"]
# Paleta categórica fija por versión (v0 neutro) + marcador distinto como segunda codificación.
COLOR = {"v0": "#6b6a66", "v1": "#2a78d6", "v2": "#eb6834", "v3": "#1baf7a", "v3-fast": "#c98500"}
MARCA = {"v0": "s", "v1": "o", "v2": "^", "v3": "D", "v3-fast": "v"}
SCHED_BASE = {"-", "static", "static-collapse2"}  # schedule por defecto de cada versión


def leer_crudo():
    with open(os.path.join(RES, "crudo.csv"), newline="") as f:
        filas = list(csv.DictReader(f))
    for r in filas:
        for c in ("hilos", "N", "ancho", "alto"):
            r[c] = int(r[c])
        for c in ("t_medio_ms", "t_mediana_ms", "desv_ms", "t_min_ms", "t_max_ms", "fps"):
            r[c] = float(r[c])
    return filas


def agregar_speedup(filas):
    """Speedup = T_secuencial / T_paralelo (promedios); eficiencia = speedup / hilos."""
    ref = {(r["N"], r["ancho"], r["alto"]): r["t_medio_ms"] for r in filas if r["version"] == "v0"}
    for r in filas:
        t0 = ref.get((r["N"], r["ancho"], r["alto"]))
        r["speedup"] = t0 / r["t_medio_ms"] if t0 else float("nan")
        r["eficiencia"] = r["speedup"] / r["hilos"]
    return filas


def escribir_resultados(filas):
    cols = ["version", "hilos", "N", "ancho", "alto", "schedule", "t_medio_ms", "t_mediana_ms", "desv_ms",
            "t_min_ms", "t_max_ms", "fps", "speedup", "eficiencia", "muestras_ms"]
    with open(os.path.join(RES, "resultados.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(cols)
        for r in filas:
            w.writerow([f"{r[c]:.4f}" if isinstance(r[c], float) else r[c] for c in cols])


def seleccion(filas, **filtro):
    return [r for r in filas if all(r[k] == v for k, v in filtro.items()) and r["schedule"] in SCHED_BASE]


def estilo(ax, titulo, xlabel, ylabel):
    ax.set_title(titulo, loc="left", fontsize=12)
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    ax.grid(True, color="#e4e3df", linewidth=0.8)
    ax.set_axisbelow(True)
    for lado in ("top", "right"):
        ax.spines[lado].set_visible(False)
    ax.legend(frameon=False)


def linea(ax, xs, ys, v):
    ax.plot(xs, ys, color=COLOR[v], marker=MARCA[v], markersize=7, linewidth=2, label=v)


def graficas(filas):
    maxh = max(r["hilos"] for r in filas)
    base = dict(N=64, ancho=800, alto=600)

    # 1 y 2: speedup y eficiencia vs hilos
    for metrica, archivo, titulo in (("speedup", "speedup_vs_hilos.png", "Speedup vs hilos (N=64, 800x600)"),
                                     ("eficiencia", "eficiencia_vs_hilos.png", "Eficiencia vs hilos (N=64, 800x600)")):
        fig, ax = plt.subplots(figsize=(7, 4.5))
        hs = sorted({r["hilos"] for r in seleccion(filas, **base) if r["version"] != "v0"})
        ideal = hs if metrica == "speedup" else [1.0] * len(hs)
        ax.plot(hs, ideal, color="#a8a7a1", linestyle="--", linewidth=1.5, label="ideal")
        for v in VERSIONES[1:]:
            pts = sorted((r["hilos"], r[metrica]) for r in seleccion(filas, version=v, **base))
            if pts:
                linea(ax, *zip(*pts), v)
        ax.set_xticks(hs)
        estilo(ax, titulo, "hilos", metrica)
        fig.tight_layout()
        fig.savefig(os.path.join(RES, archivo), dpi=150)
        plt.close(fig)

    # 3: FPS vs N (escala log)
    fig, ax = plt.subplots(figsize=(7, 4.5))
    for v in VERSIONES:
        h = 1 if v == "v0" else maxh
        pts = sorted((r["N"], r["fps"]) for r in seleccion(filas, version=v, hilos=h, ancho=800, alto=600))
        if pts:
            linea(ax, *zip(*pts), v)
    ax.axhline(60, color="#d03b3b", linewidth=1, linestyle=":", label="60 FPS")
    ax.set_xscale("log", base=2)
    ns = sorted({r["N"] for r in filas if (r["ancho"], r["alto"]) == (800, 600)})
    ax.set_xticks(ns, [str(n) for n in ns])
    ax.set_yscale("log")
    estilo(ax, f"FPS vs N (800x600; paralelas con {maxh} hilos)", "N (fuentes de onda)", "FPS")
    fig.tight_layout()
    fig.savefig(os.path.join(RES, "fps_vs_n.png"), dpi=150)
    plt.close(fig)

    # 4: tiempo por frame vs resolución
    fig, ax = plt.subplots(figsize=(7, 4.5))
    for v in VERSIONES:
        h = 1 if v == "v0" else maxh
        pts = sorted((r["ancho"] * r["alto"] / 1e6, r["t_medio_ms"]) for r in seleccion(filas, version=v, hilos=h, N=64))
        if pts:
            linea(ax, *zip(*pts), v)
    ax.set_yscale("log")
    estilo(ax, f"Tiempo por frame vs resolución (N=64; paralelas con {maxh} hilos)", "megapíxeles",
           "tiempo medio por frame (ms)")
    fig.tight_layout()
    fig.savefig(os.path.join(RES, "tiempo_vs_resolucion.png"), dpi=150)
    plt.close(fig)

    # 5: comparación de schedules
    sched = [r for r in filas if r["version"] in ("v1", "v2", "v3") and r["hilos"] == maxh
             and (r["N"], r["ancho"], r["alto"]) == (64, 800, 600)]
    fig, ax = plt.subplots(figsize=(8, 4.5))
    etiquetas = [f'{r["version"]} {r["schedule"]}' for r in sched]
    ax.barh(etiquetas, [r["t_medio_ms"] for r in sched], color=[COLOR[r["version"]] for r in sched], height=0.6,
            edgecolor="white", linewidth=2)
    ax.errorbar([r["t_medio_ms"] for r in sched], etiquetas, xerr=[r["desv_ms"] for r in sched], fmt="none",
                ecolor="#52514e", capsize=3)
    ax.invert_yaxis()
    ax.set_title(f"Schedules (N=64, 800x600, {maxh} hilos); barra = media, línea = desv. estándar", loc="left")
    ax.set_xlabel("tiempo medio por frame (ms)")
    ax.grid(True, axis="x", color="#e4e3df")
    ax.set_axisbelow(True)
    for lado in ("top", "right"):
        ax.spines[lado].set_visible(False)
    fig.tight_layout()
    fig.savefig(os.path.join(RES, "schedules.png"), dpi=150)
    plt.close(fig)


def karp_flatt(s, p):
    """Fracción serial experimental: e = (1/S - 1/p) / (1 - 1/p)."""
    return (1 / s - 1 / p) / (1 - 1 / p) if p > 1 else float("nan")


def tabla(filas, titulo, amdahl=False):
    enc = "| versión | hilos | N | resolución | schedule | media (ms) | mediana | desv | mín | máx | FPS | speedup | eficiencia |"
    if amdahl:
        enc += " fracción serial (Karp-Flatt) |"
    out = [f"### {titulo}", "", enc, "|" + "---|" * (enc.count("|") - 1)]
    for r in filas:
        fila = (f'| {r["version"]} | {r["hilos"]} | {r["N"]} | {r["ancho"]}x{r["alto"]} | {r["schedule"]} | '
                f'{r["t_medio_ms"]:.2f} | {r["t_mediana_ms"]:.2f} | {r["desv_ms"]:.2f} | {r["t_min_ms"]:.2f} | '
                f'{r["t_max_ms"]:.2f} | {r["fps"]:.2f} | {r["speedup"]:.2f} | {r["eficiencia"]:.2f} |')
        if amdahl:
            e = karp_flatt(r["speedup"], r["hilos"])
            fila += f" {e:.3f} |" if e == e else " — |"
        out.append(fila)
    return "\n".join(out) + "\n"


def tablas_md(filas):
    maxh = max(r["hilos"] for r in filas)
    orden = lambda r: (VERSIONES.index(r["version"]), r["hilos"], r["N"], r["ancho"])
    a = sorted(seleccion(filas, N=64, ancho=800, alto=600), key=orden)
    b = sorted([r for r in seleccion(filas, ancho=800, alto=600) if r["hilos"] in (1, maxh)
                and (r["version"] == "v0" or r["hilos"] == maxh)], key=lambda r: (r["N"], VERSIONES.index(r["version"])))
    c = sorted([r for r in seleccion(filas, N=64) if r["version"] == "v0" or r["hilos"] == maxh],
               key=lambda r: (r["ancho"], VERSIONES.index(r["version"])))
    d = [r for r in filas if r["hilos"] == maxh and r["version"] in ("v1", "v2", "v3")
         and (r["N"], r["ancho"], r["alto"]) == (64, 800, 600)]
    partes = [tabla(a, "Barrido A — hilos (N=64, 800x600)", amdahl=True),
              tabla(b, f"Barrido B — N (800x600, {maxh} hilos)"),
              tabla(c, f"Barrido C — resolución (N=64, {maxh} hilos)"),
              tabla(d, f"Barrido D — schedules (N=64, 800x600, {maxh} hilos)")]

    ruta = os.path.join(RES, "n_max_60fps.csv")
    if os.path.exists(ruta):
        with open(ruta, newline="") as f:
            nmax = list(csv.DictReader(f))
        partes.append("### N máximo que sostiene 60 FPS (640x480)\n\n| versión | N máximo | FPS con ese N | FPS con N=1 |\n"
                      "|---|---|---|---|\n" + "\n".join(
                          f'| {r["version"]} | {r["n_max_60fps"] if r["n_max_60fps"] != "0" else "no alcanza"} | '
                          f'{r["fps_con_n_max"]} | {r["fps_con_n1"]} |' for r in nmax) + "\n")

    ruta = os.path.join(RES, "repeticion.csv")
    if os.path.exists(ruta):
        with open(ruta, newline="") as f:
            rep = list(csv.DictReader(f))
        grupos = {}
        for r in rep:
            grupos.setdefault((r["version"], r["schedule"]), []).append(float(r["t_medio_ms"]))
        t0 = sum(grupos[("v0", "-")]) / len(grupos[("v0", "-")])
        filas_rep = [f'| {v} | {s} | ' + " | ".join(f"{x:.1f}" for x in ts) +
                     f' | **{sum(ts) / len(ts):.1f}** | {t0 / (sum(ts) / len(ts)):.2f} |' for (v, s), ts in grupos.items()]
        partes.append(f"### Repetición intercalada (N=64, 800x600, {maxh} hilos, 3 rondas de 10 mediciones)\n\n"
                      "| versión | schedule | media ronda 1 (ms) | ronda 2 | ronda 3 | promedio | speedup |\n"
                      "|---|---|---|---|---|---|---|\n" + "\n".join(filas_rep) + "\n")

    muestras = ["### Mediciones individuales (ms) por configuración", "",
                "| # | versión | hilos | N | resolución | schedule | " + " | ".join(f"m{i + 1}" for i in range(10)) + " |",
                "|" + "---|" * 16]
    for i, r in enumerate(filas, 1):
        m = [f"{float(x):.1f}" for x in r["muestras_ms"].split(";")]
        muestras.append(f'| {i} | {r["version"]} | {r["hilos"]} | {r["N"]} | {r["ancho"]}x{r["alto"]} | '
                        f'{r["schedule"]} | ' + " | ".join(m[:10]) + " |")
    partes.append("\n".join(muestras) + "\n")

    with open(os.path.join(RES, "tablas.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(partes))


if __name__ == "__main__":
    filas = agregar_speedup(leer_crudo())
    escribir_resultados(filas)
    graficas(filas)
    tablas_md(filas)
    print(f"{len(filas)} configuraciones -> {RES}/resultados.csv, tablas.md y gráficas PNG")
