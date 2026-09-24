/*
 * screensaver_seq.cpp — Versión secuencial (v0) del screensaver
 * "Túnel Caleidoscópico de Interferencia": N fuentes de onda orbitan en el plano
 * y el color de cada pixel sale de la superposición de sus ondas.
 *
 * Autores: Carlos Daniel Estrada Vega (20853), André Emilio Pivaral López (23574),
 *          Daniela Ramírez de León (23053)
 *
 * Compilación:
 *   g++ -O2 -std=c++17 -Wall -Wextra src/screensaver_seq.cpp src/kaleidoscope.cpp src/motor.cpp \
 *       -o screensaver_seq $(pkg-config --cflags --libs sdl2 SDL2_ttf)
 * Uso: ./screensaver_seq --n 14 [--ancho 800 --alto 600 ...]   (--help para ver todo)
 */
#include "kaleidoscope.hpp"
#include "motor.hpp"

int main(int argc, char *argv[]) {
    Parametros p;
    if (!leerParametros(argc, argv, p, false)) return 1;

    std::vector<Fuente> fuentes = crearFuentes(p.n, static_cast<unsigned>(p.semilla));
    const FuncionFrame frame = [&](double t, std::vector<uint32_t> &pixeles) {
        return renderSecuencial(fuentes, pixeles, p, t);
    };

    const InfoVersion info{"v0", 1, "-"};
    return p.bench ? ejecutarBenchmark(p, info, frame) : ejecutarInteractivo(p, info, frame);
}
