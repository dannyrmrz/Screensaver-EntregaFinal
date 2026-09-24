#!/usr/bin/env bash
# run_bench.sh — Batería completa de pruebas (bitácora del Anexo 3).
# Cada configuración: 5 frames de calentamiento + MEDICIONES frames cronometrados.
# Uso: bash bench/run_bench.sh   (desde la raíz, después de "make seq par par_fast")
set -euo pipefail
cd "$(dirname "$0")/.."

RES=bench/results
CSV=$RES/crudo.csv
LOG=$RES/bitacora.log
MEDICIONES=${MEDICIONES:-10}
MAX_HILOS=$(nproc)
HILOS="1 2 4 6 $MAX_HILOS"
# A, B, C y la búsqueda de 60 FPS usan static; el barrido D compara los schedules
# (de ahí salió guided como default del programa).
SCHED="--schedule static"
export SDL_VIDEODRIVER=dummy   # --bench no abre ventana; esto evita depender de un display

mkdir -p "$RES"
rm -f "$CSV" "$LOG" "$RES/busqueda_60fps.csv" "$RES/repeticion.csv"

# corre <binario> <args...>: agrega una fila al CSV y la salida al log
corre() {
    local bin=$1; shift
    "./$bin" --bench --frames "$MEDICIONES" --csv "$CSV" "$@" | tee -a "$LOG"
}

{
    echo "== Equipo: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | xargs), $MAX_HILOS hilos lógicos"
    echo "== Compilador: $(g++ --version | head -1)"
    echo "== Fecha: $(date '+%Y-%m-%d %H:%M')"
} | tee "$LOG"

echo "== Verificación de correctitud" | tee -a "$LOG"
for sched in "static" "dynamic --chunk 4" "guided"; do
    ./screensaver_par --n 64 --verify --schedule $sched | tee -a "$LOG"
done
./screensaver_par --n 16 --ancho 1280 --alto 720 --simetria 12 --semilla 7 --verify | tee -a "$LOG"
echo "-- build -ffast-math (comparar el hash de v0 con el build normal):" | tee -a "$LOG"
./screensaver_par_fast --n 64 --verify | tee -a "$LOG" || true
./screensaver_par_fast --n 16 --ancho 1280 --alto 720 --simetria 12 --semilla 7 --verify | tee -a "$LOG" || true

echo "== Barrido A: hilos (N=64, 800x600)" | tee -a "$LOG"
corre screensaver_seq --n 64
for h in $HILOS; do
    for v in 1 2 3; do corre screensaver_par --n 64 --version $v --hilos "$h" $SCHED; done
    corre screensaver_par_fast --n 64 --version 3 --hilos "$h" $SCHED
done

echo "== Barrido B: N (800x600, $MAX_HILOS hilos)" | tee -a "$LOG"
for n in 4 16 256 1024; do   # N=64 ya se midió en el barrido A
    corre screensaver_seq --n "$n"
    for v in 1 2 3; do corre screensaver_par --n "$n" --version $v --hilos "$MAX_HILOS" $SCHED; done
    corre screensaver_par_fast --n "$n" --version 3 --hilos "$MAX_HILOS" $SCHED
done

echo "== Barrido C: resolución (N=64, $MAX_HILOS hilos)" | tee -a "$LOG"
for res in "640 480" "1280 720" "1920 1080"; do   # 800x600 ya se midió
    set -- $res
    corre screensaver_seq --n 64 --ancho "$1" --alto "$2"
    for v in 1 2 3; do corre screensaver_par --n 64 --version $v --hilos "$MAX_HILOS" --ancho "$1" --alto "$2" $SCHED; done
    corre screensaver_par_fast --n 64 --version 3 --hilos "$MAX_HILOS" --ancho "$1" --alto "$2" $SCHED
done

echo "== Barrido D: schedule (N=64, 800x600, $MAX_HILOS hilos)" | tee -a "$LOG"
for v in 2 3; do
    for sched in "static --chunk 8" "dynamic --chunk 1" "dynamic --chunk 4" "dynamic --chunk 16" "guided"; do
        corre screensaver_par --n 64 --version $v --hilos "$MAX_HILOS" --schedule $sched
    done
done

# N máximo a >= 60 FPS en 640x480: duplica N hasta fallar y luego bisecta.
echo "== Búsqueda de N máximo a 60 FPS (640x480)" | tee -a "$LOG"
fps_con_n() {
    local bin=$1 n=$2; shift 2
    "./$bin" --bench --frames "$MEDICIONES" --csv "$RES/busqueda_60fps.csv" --n "$n" --ancho 640 --alto 480 "$@" \
        | tee -a "$LOG" | grep -o '[0-9.]* FPS' | cut -d' ' -f1
}
cumple() { awk -v f="$1" 'BEGIN { exit !(f >= 60) }'; }
echo "version,n_max_60fps,fps_con_n_max,fps_con_n1" > "$RES/n_max_60fps.csv"
buscar() {
    local etiqueta=$1 bin=$2; shift 2
    local fps1 bajo=0 alto=1 fpsBajo=0 fps
    fps1=$(fps_con_n "$bin" 1 "$@")
    if cumple "$fps1"; then
        bajo=1; fpsBajo=$fps1; alto=2
        while fps=$(fps_con_n "$bin" "$alto" "$@") && cumple "$fps" && [ "$alto" -lt 4096 ]; do
            bajo=$alto; fpsBajo=$fps; alto=$((alto * 2))
        done
        while [ $((alto - bajo)) -gt 1 ]; do
            local medio=$(((bajo + alto) / 2))
            fps=$(fps_con_n "$bin" "$medio" "$@")
            if cumple "$fps"; then bajo=$medio; fpsBajo=$fps; else alto=$medio; fi
        done
    fi
    echo "$etiqueta,$bajo,$fpsBajo,$fps1" | tee -a "$RES/n_max_60fps.csv" "$LOG"
}
buscar v0 screensaver_seq
for v in 1 2 3; do buscar "v$v" screensaver_par --version $v --hilos "$MAX_HILOS" $SCHED; done
buscar v3-fast screensaver_par_fast --version 3 --hilos "$MAX_HILOS" $SCHED

echo "== Repetición intercalada de schedules (v3, N=64, 800x600), 3 rondas" | tee -a "$LOG"
for ronda in 1 2 3; do
    for sched in "static" "static --chunk 8" "dynamic --chunk 4" "dynamic --chunk 16" "guided"; do
        ./screensaver_par --n 64 --version 3 --hilos "$MAX_HILOS" --bench --frames "$MEDICIONES" --csv "$RES/repeticion.csv" --schedule $sched | tee -a "$LOG"
    done
    ./screensaver_par --n 64 --version 1 --hilos "$MAX_HILOS" --bench --frames "$MEDICIONES" --csv "$RES/repeticion.csv" | tee -a "$LOG"
    ./screensaver_seq --n 64 --bench --frames "$MEDICIONES" --csv "$RES/repeticion.csv" | tee -a "$LOG"
done

echo "== Listo: $CSV, $LOG" | tee -a "$LOG"
