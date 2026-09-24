/*
 * kaleidoscope.cpp — Validación de argumentos, creación de fuentes, frame
 * secuencial de referencia (v0) y utilidades de medición.
 *
 * Autores: Carlos Daniel Estrada Vega (20853), André Emilio Pivaral López (23574),
 *          Daniela Ramírez de León (23053)
 */
#include "kaleidoscope.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <random>

// Única función de validación: entero bien formado y dentro de [minimo, maximo].
static bool leerEntero(const char *nombre, const char *texto, long minimo, long maximo, int &destino) {
    if (texto == nullptr) {
        std::fprintf(stderr, "Error: %s requiere un valor.\n", nombre);
        return false;
    }
    char *fin = nullptr;
    errno = 0;
    const long valor = std::strtol(texto, &fin, 10);
    if (fin == texto || *fin != '\0' || errno == ERANGE) {
        std::fprintf(stderr, "Error: %s debe ser un entero, se recibió \"%s\".\n", nombre, texto);
        return false;
    }
    if (valor < minimo || valor > maximo) {
        std::fprintf(stderr, "Error: %s = %ld fuera de rango [%ld, %ld].\n", nombre, valor, minimo, maximo);
        return false;
    }
    destino = static_cast<int>(valor);
    return true;
}

void imprimirUso(const char *programa, bool esParalela) {
    std::fprintf(stderr,
                 "Uso: %s --n <N> [--ancho W] [--alto H] [--simetria S] [--semilla X]\n"
                 "          [--fullscreen] [--frames F] [--captura archivo.bmp]\n"
                 "          [--bench] [--csv archivo]%s\n\n"
                 "  --n         fuentes de onda          1 - 4096 (obligatorio)\n"
                 "  --ancho     ancho de ventana         640 - 3840 (800)\n"
                 "  --alto      alto de ventana          480 - 2160 (600)\n"
                 "  --simetria  orden del caleidoscopio  3 - 24 (8)\n"
                 "  --semilla   semilla pseudoaleatoria  0 - 1000000 (2026)\n"
                 "  --frames    frames a ejecutar        0 = infinito; en --bench >= 10 (20)\n",
                 programa,
                 esParalela ? "\n          [--version 1|2|3] [--hilos H] [--schedule static|dynamic|guided]"
                              " [--chunk C] [--verify]" : "");
    if (esParalela)
        std::fprintf(stderr,
                     "  --version   variante paralela        1 - 3 (3)\n"
                     "  --hilos     hilos OpenMP             1 - 256 (todos)\n"
                     "  --schedule  reparto de filas v2/v3   static | dynamic | guided (guided)\n"
                     "  --chunk     filas por bloque         0 - 4096 (0 = por defecto)\n"
                     "  --verify    compara v1-v3 contra la secuencial en t = 3.7\n");
}

bool leerParametros(int argc, char **argv, Parametros &p, bool esParalela) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const char *valor = (i + 1 < argc) ? argv[i + 1] : nullptr;
        bool ok = true;
        bool usaValor = true;

        if (arg == "--n")             ok = leerEntero("--n", valor, 1, 4096, p.n);
        else if (arg == "--ancho")    ok = leerEntero("--ancho", valor, 640, 3840, p.ancho);
        else if (arg == "--alto")     ok = leerEntero("--alto", valor, 480, 2160, p.alto);
        else if (arg == "--simetria") ok = leerEntero("--simetria", valor, 3, 24, p.simetria);
        else if (arg == "--semilla")  ok = leerEntero("--semilla", valor, 0, 1000000, p.semilla);
        else if (arg == "--frames")   ok = leerEntero("--frames", valor, 0, 1000000, p.frames);
        else if (arg == "--csv" || arg == "--captura") {
            if (!valor) { std::fprintf(stderr, "Error: %s requiere un archivo.\n", arg.c_str()); ok = false; }
            else (arg == "--csv" ? p.csv : p.captura) = valor;
        }
        else if (esParalela && arg == "--version") ok = leerEntero("--version", valor, 1, 3, p.version);
        else if (esParalela && arg == "--hilos")   ok = leerEntero("--hilos", valor, 1, 256, p.hilos);
        else if (esParalela && arg == "--chunk")   ok = leerEntero("--chunk", valor, 0, 4096, p.chunk);
        else if (esParalela && arg == "--schedule") {
            ok = valor && (!std::strcmp(valor, "static") || !std::strcmp(valor, "dynamic") ||
                           !std::strcmp(valor, "guided"));
            if (ok) p.schedule = valor;
            else std::fprintf(stderr, "Error: --schedule debe ser static, dynamic o guided.\n");
        }
        else {
            usaValor = false;
            if (arg == "--fullscreen")                p.fullscreen = true;
            else if (arg == "--bench")                p.bench = true;
            else if (esParalela && arg == "--verify") p.verify = true;
            else {
                if (arg != "--help" && arg != "-h")
                    std::fprintf(stderr, "Error: argumento desconocido \"%s\".\n", arg.c_str());
                ok = false;
            }
        }
        if (!ok) { imprimirUso(argv[0], esParalela); return false; }
        if (usaValor) ++i;
    }

    if (p.n == 0) {
        std::fprintf(stderr, "Error: --n es obligatorio.\n");
        imprimirUso(argv[0], esParalela);
        return false;
    }
    if (p.bench) {
        if (p.frames == 0) p.frames = 20;
        if (p.frames < 10) {
            std::fprintf(stderr, "Error: --bench requiere al menos 10 frames medidos.\n");
            return false;
        }
    } else if (!p.csv.empty()) {
        std::fprintf(stderr, "Error: --csv solo tiene sentido junto con --bench.\n");
        return false;
    }
    return true;
}

std::vector<Fuente> crearFuentes(int n, unsigned semilla) {
    std::mt19937 gen(semilla);
    std::uniform_real_distribution<double> U(0.0, 1.0);
    std::vector<Fuente> fuentes(n);
    for (Fuente &f : fuentes) {
        // Orden fijo de las llamadas a U: misma semilla -> mismas fuentes.
        f.hue = U(gen);
        f.frecuencia = 1.5 + 3.0 * U(gen);
        f.amplitud = 0.6 + 0.4 * U(gen);
        f.fase = 2 * PI * U(gen);
        f.radioOrb = 0.3 + 1.1 * U(gen);
        f.velAng = 0.2 + 0.7 * U(gen);
        f.angIni = 2 * PI * U(gen);
        f.centroU = 3.0 * U(gen);
        f.centroV = U(gen);
        f.k = 2.0 * f.frecuencia;
        actualizarFuente(f, 0.0);
    }
    return fuentes;
}

// v0: frame completo en un solo hilo. Devuelve la suma R+G+B del frame.
long long renderSecuencial(std::vector<Fuente> &fuentes, std::vector<uint32_t> &pixeles,
                           const Parametros &p, double t) {
    for (Fuente &f : fuentes) actualizarFuente(f, t);

    long long suma = 0;
    for (int y = 0; y < p.alto; ++y)
        for (int x = 0; x < p.ancho; ++x) {
            double invR, v, r;
            polaresDePixel(x, y, p, invR, v, r);
            const uint32_t c = colorPixel(invR, v, r, fuentes, t);
            pixeles[static_cast<size_t>(y) * p.ancho + x] = c;
            suma += sumaCanales(c);
        }
    return suma;
}

uint64_t hashFNV1a(const std::vector<uint32_t> &pixeles) {
    uint64_t h = 14695981039346656037ull;
    for (uint32_t px : pixeles)
        for (int b = 0; b < 4; ++b) {
            h ^= (px >> (8 * b)) & 0xFF;
            h *= 1099511628211ull;
        }
    return h;
}

Estadisticas calcularEstadisticas(std::vector<double> tiempos) {
    std::sort(tiempos.begin(), tiempos.end());
    const size_t n = tiempos.size();
    Estadisticas e{};
    e.media = std::accumulate(tiempos.begin(), tiempos.end(), 0.0) / n;
    e.mediana = (n % 2) ? tiempos[n / 2] : (tiempos[n / 2 - 1] + tiempos[n / 2]) / 2;
    double sc = 0;
    for (double x : tiempos) sc += (x - e.media) * (x - e.media);
    e.desv = n > 1 ? std::sqrt(sc / (n - 1)) : 0.0;  // desviación muestral
    e.minimo = tiempos.front();
    e.maximo = tiempos.back();
    return e;
}
