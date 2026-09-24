/*
 * screensaver_par.cpp — Versiones paralelas (OpenMP) del screensaver
 * "Túnel Caleidoscópico de Interferencia". Tres variantes acumulativas (--version):
 *   v1  parallel for collapse(2) sobre los pixeles.
 *   v2  + fuentes en paralelo, reparto por filas con schedule(runtime) y reduction.
 *   v3  + tabla polar precalculada y una sola región paralela por frame.
 *
 * Sincronía: cada hilo escribe pixeles distintos del búfer y solo lee las fuentes,
 * así que no hay condiciones de carrera y no se necesita critical ni atomic. La
 * barrera implícita al final de cada "omp for" garantiza que el frame esté completo
 * (o que las fuentes estén actualizadas) antes del paso siguiente.
 *
 * Autores: Carlos Daniel Estrada Vega (20853), André Emilio Pivaral López (23574),
 *          Daniela Ramírez de León (23053)
 *
 * Compilación:
 *   g++ -O2 -std=c++17 -Wall -Wextra -fopenmp src/screensaver_par.cpp src/kaleidoscope.cpp src/motor.cpp \
 *       -o screensaver_par $(pkg-config --cflags --libs sdl2 SDL2_ttf)
 * Uso: ./screensaver_par --n 14 --version 3 --hilos 8   (--help para ver todo)
 */
#include <omp.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "kaleidoscope.hpp"
#include "motor.hpp"

// Con pocas fuentes, repartirlas entre hilos cuesta más de lo que ahorra.
constexpr int MIN_FUENTES_PARALELO = 256;

// v1 — Paralelización directa. Variables del pixel declaradas dentro del bucle
// (privadas por construcción); fuentes compartidas en solo lectura.
static long long renderV1(std::vector<Fuente> &fuentes, std::vector<uint32_t> &pixeles,
                          const Parametros &p, double t) {
    for (Fuente &f : fuentes) actualizarFuente(f, t);

    #pragma omp parallel for collapse(2) schedule(static)
    for (int y = 0; y < p.alto; ++y)
        for (int x = 0; x < p.ancho; ++x) {
            double invR, v, r;
            polaresDePixel(x, y, p, invR, v, r);
            pixeles[static_cast<size_t>(y) * p.ancho + x] = colorPixel(invR, v, r, fuentes, t);
        }
    return -1;  // v1 no calcula la métrica global
}

// v2 — Reparto por filas completas (bloques contiguos: sin falso compartimiento
// dentro de una fila) con el schedule elegido en --schedule/--chunk, fuentes en
// paralelo cuando son muchas, y brillo total del frame con reduction.
static long long renderV2(std::vector<Fuente> &fuentes, std::vector<uint32_t> &pixeles,
                          const Parametros &p, double t) {
    const int n = static_cast<int>(fuentes.size());
    #pragma omp parallel for schedule(static) if (n >= MIN_FUENTES_PARALELO)
    for (int i = 0; i < n; ++i) actualizarFuente(fuentes[i], t);

    long long suma = 0;
    #pragma omp parallel for schedule(runtime) reduction(+ : suma)
    for (int y = 0; y < p.alto; ++y) {
        uint32_t *fila = &pixeles[static_cast<size_t>(y) * p.ancho];
        for (int x = 0; x < p.ancho; ++x) {
            double invR, v, r;
            polaresDePixel(x, y, p, invR, v, r);
            fila[x] = colorPixel(invR, v, r, fuentes, t);
            suma += sumaCanales(fila[x]);
        }
    }
    return suma;
}

// v3 — r, v y 0.55/r no dependen de t: se calculan una sola vez (en paralelo) y
// cada frame solo los lee, eliminando sqrt, atan2 y fmod del bucle caliente.
static TablaPolar crearTablaPolar(const Parametros &p) {
    const size_t total = static_cast<size_t>(p.ancho) * p.alto;
    TablaPolar tabla{std::vector<double>(total), std::vector<double>(total), std::vector<double>(total)};
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < p.alto; ++y)
        for (int x = 0; x < p.ancho; ++x) {
            const size_t i = static_cast<size_t>(y) * p.ancho + x;
            polaresDePixel(x, y, p, tabla.invR[i], tabla.v[i], tabla.r[i]);
        }
    return tabla;
}

// Una sola región paralela por frame: el equipo de hilos se crea una vez y la
// barrera implícita del primer "omp for" separa la actualización de fuentes del pintado.
static long long renderV3(std::vector<Fuente> &fuentes, std::vector<uint32_t> &pixeles,
                          const Parametros &p, const TablaPolar &tabla, double t) {
    const int n = static_cast<int>(fuentes.size());
    long long suma = 0;
    #pragma omp parallel reduction(+ : suma)
    {
        #pragma omp for schedule(static)
        for (int i = 0; i < n; ++i) actualizarFuente(fuentes[i], t);
        // barrera implícita: todas las fuentes listas antes de pintar

        #pragma omp for schedule(runtime)
        for (int y = 0; y < p.alto; ++y) {
            const size_t base = static_cast<size_t>(y) * p.ancho;
            for (int x = 0; x < p.ancho; ++x) {
                const size_t i = base + x;
                pixeles[i] = colorPixel(tabla.invR[i], tabla.v[i], tabla.r[i], fuentes, t);
                suma += sumaCanales(pixeles[i]);
            }
        }
    }
    return suma;
}

// Traduce --schedule/--chunk al schedule(runtime) de v2 y v3.
static void configurarSchedule(const Parametros &p) {
    omp_sched_t tipo = omp_sched_static;
    if (p.schedule == "dynamic") tipo = omp_sched_dynamic;
    else if (p.schedule == "guided") tipo = omp_sched_guided;
    omp_set_schedule(tipo, p.chunk);  // chunk 0 = valor por defecto
}

static std::string etiquetaSchedule(const Parametros &p) {
    if (p.version == 1) return "static-collapse2";
    return p.chunk > 0 ? p.schedule + ":" + std::to_string(p.chunk) : p.schedule;
}

// Frame de la versión elegida; guarda su propio estado (tabla polar de v3).
static FuncionFrame crearFrame(int version, std::vector<Fuente> &fuentes, const Parametros &p) {
    if (version == 1)
        return [&fuentes, &p](double t, std::vector<uint32_t> &px) { return renderV1(fuentes, px, p, t); };
    if (version == 2)
        return [&fuentes, &p](double t, std::vector<uint32_t> &px) { return renderV2(fuentes, px, p, t); };
    auto tabla = std::make_shared<TablaPolar>(crearTablaPolar(p));
    return [&fuentes, &p, tabla](double t, std::vector<uint32_t> &px) {
        return renderV3(fuentes, px, p, *tabla, t);
    };
}

// --verify: un frame en t fijo con la secuencial y con cada versión paralela;
// sin -ffast-math los búferes deben ser idénticos bit a bit.
static int verificar(const Parametros &p) {
    const double T_FIJO = 3.7;
    const size_t total = static_cast<size_t>(p.ancho) * p.alto;

    std::vector<Fuente> fuentesRef = crearFuentes(p.n, static_cast<unsigned>(p.semilla));
    std::vector<uint32_t> referencia(total);
    const long long sumaRef = renderSecuencial(fuentesRef, referencia, p, T_FIJO);
    std::printf("Verificacion N=%d %dx%d t=%.1f hilos=%d schedule=%s\n", p.n, p.ancho, p.alto, T_FIJO,
                omp_get_max_threads(), etiquetaSchedule(p).c_str());
    std::printf("  v0 secuencial  hash %016llx  suma RGB %lld\n",
                static_cast<unsigned long long>(hashFNV1a(referencia)), sumaRef);

    bool todoIgual = true;
    for (int version = 1; version <= 3; ++version) {
        std::vector<Fuente> fuentes = crearFuentes(p.n, static_cast<unsigned>(p.semilla));
        std::vector<uint32_t> pixeles(total);
        const long long suma = crearFrame(version, fuentes, p)(T_FIJO, pixeles);

        size_t distintos = 0;
        int difMax = 0;
        for (size_t i = 0; i < total; ++i) {
            if (pixeles[i] == referencia[i]) continue;
            ++distintos;
            for (int c = 0; c < 24; c += 8)
                difMax = std::max(difMax, std::abs(int((pixeles[i] >> c) & 0xFF) - int((referencia[i] >> c) & 0xFF)));
        }
        todoIgual = todoIgual && distintos == 0;
        std::printf("  v%d paralela    hash %016llx  suma RGB %s  pixeles distintos %zu  dif. max canal %d  -> %s\n",
                    version, static_cast<unsigned long long>(hashFNV1a(pixeles)),
                    suma < 0 ? "n/a" : std::to_string(suma).c_str(), distintos, difMax,
                    distintos == 0 ? "IDENTICA" : "DIFERENTE");
    }
    return todoIgual ? 0 : 2;
}

int main(int argc, char *argv[]) {
    Parametros p;
    if (!leerParametros(argc, argv, p, true)) return 1;
    if (p.hilos > 0) omp_set_num_threads(p.hilos);
    configurarSchedule(p);

    if (p.verify) return verificar(p);

    std::vector<Fuente> fuentes = crearFuentes(p.n, static_cast<unsigned>(p.semilla));
    const FuncionFrame frame = crearFrame(p.version, fuentes, p);
    std::string nombre = "v" + std::to_string(p.version);
#ifdef __FAST_MATH__
    nombre += "-fast";  // build con -O3 -march=native -ffast-math
#endif
    const InfoVersion info{nombre, omp_get_max_threads(), etiquetaSchedule(p)};
    return p.bench ? ejecutarBenchmark(p, info, frame) : ejecutarInteractivo(p, info, frame);
}
