/*
 * kaleidoscope.hpp — Núcleo matemático compartido del screensaver
 * "Túnel Caleidoscópico de Interferencia".
 *
 * Autores: Carlos Daniel Estrada Vega (20853), André Emilio Pivaral López (23574),
 *          Daniela Ramírez de León (23053)
 * Curso:   Computación Paralela y Distribuida, UVG 2026
 *
 * El núcleo es idéntico para la versión secuencial y la paralela: la comparación
 * de tiempos solo mide el efecto de OpenMP. Las funciones del bucle caliente son
 * inline para que todas las versiones se optimicen igual.
 */
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

constexpr double PI = 3.14159265358979323846;

// Fuente de onda: orbita en el plano (u,v) y emite una onda senoidal.
struct Fuente {
    double hue;              // tono base del color, [0,1)
    double frecuencia;       // frecuencia espacial
    double amplitud;         // amplitud de la onda
    double fase;             // desfase inicial
    double radioOrb;         // radio de la órbita
    double velAng;           // velocidad angular de la órbita
    double angIni;           // ángulo inicial de la órbita
    double centroU, centroV; // centro de la órbita
    double posU, posV;       // posición actual (cambia cada frame)
    double k;                // 2 * frecuencia (constante)
    double desfase;          // fase - 1.4 t (cambia cada frame)
};

// Parámetros de ejecución, todos leídos de la línea de comandos.
struct Parametros {
    int n = 0;               // cantidad de fuentes de onda (obligatorio)
    int ancho = 800;
    int alto = 600;
    int simetria = 8;        // orden de simetría del caleidoscopio
    int semilla = 2026;
    int frames = 0;          // 0 = infinito; en --bench son los frames medidos
    int version = 3;         // 1, 2 o 3 (solo paralela)
    int hilos = 0;           // 0 = lo que decida OpenMP
    int chunk = 0;           // 0 = chunk por defecto del schedule
    std::string schedule = "guided";  // el mejor en el barrido D de la bitácora
    std::string csv;         // archivo al que --bench agrega una fila
    std::string captura;     // BMP donde se guarda el último frame
    bool fullscreen = false;
    bool bench = false;
    bool verify = false;
};

// Coordenadas que solo dependen de (x,y); v3 las calcula una sola vez.
struct TablaPolar {
    std::vector<double> invR;  // 0.55 / r
    std::vector<double> v;     // ángulo doblado, [0,1]
    std::vector<double> r;     // radio normalizado
};

struct Estadisticas {
    double media, mediana, desv, minimo, maximo;
};

// ---- Funciones implementadas en kaleidoscope.cpp ----
bool leerParametros(int argc, char **argv, Parametros &p, bool esParalela);
void imprimirUso(const char *programa, bool esParalela);
std::vector<Fuente> crearFuentes(int n, unsigned semilla);
long long renderSecuencial(std::vector<Fuente> &fuentes, std::vector<uint32_t> &pixeles,
                           const Parametros &p, double t);
uint64_t hashFNV1a(const std::vector<uint32_t> &pixeles);
Estadisticas calcularEstadisticas(std::vector<double> tiempos);

// ---- Bucle caliente (inline) ----

// Movimiento orbital de la fuente en el instante t.
inline void actualizarFuente(Fuente &f, double t) {
    f.posU = f.centroU + f.radioOrb * std::cos(f.velAng * t + f.angIni);
    f.posV = f.centroV + 0.35 * std::sin(f.velAng * t * 1.3 + f.angIni);
    f.desfase = f.fase - 1.4 * t;
}

// Pixel -> polares, con el ángulo doblado en sectores (caleidoscopio).
inline void polaresDePixel(int x, int y, const Parametros &p, double &invR, double &v, double &r) {
    const double medio = p.alto / 2.0;
    const double nx = (x - p.ancho / 2.0) / medio;
    const double ny = (y - medio) / medio;
    r = std::max(std::sqrt(nx * nx + ny * ny), 1e-3);
    const double sector = 2.0 * PI / p.simetria;
    double m = std::fmod(std::atan2(ny, nx), sector);
    if (m < 0) m += sector;
    v = std::fabs(m - sector / 2) / (sector / 2);
    invR = 0.55 / r;   // proyección 1/r: profundidad del túnel
}

inline uint32_t hsvARgb(double h, double s, double val) {
    const double h6 = h * 6.0;
    const int i = static_cast<int>(h6) % 6;
    const double f = h6 - std::floor(h6);
    const double p = val * (1 - s), q = val * (1 - s * f), w = val * (1 - s * (1 - f));
    double r, g, b;
    switch (i) {
        case 0:  r = val; g = w;   b = p;   break;
        case 1:  r = q;   g = val; b = p;   break;
        case 2:  r = p;   g = val; b = w;   break;
        case 3:  r = p;   g = q;   b = val; break;
        case 4:  r = w;   g = p;   b = val; break;
        default: r = val; g = p;   b = q;   break;
    }
    const auto canal = [](double c) { return static_cast<uint32_t>(c * 255.0 + 0.5); };
    return 0xFF000000u | (canal(r) << 16) | (canal(g) << 8) | canal(b);
}

// Color de un pixel: superposición (interferencia) de las N ondas.
inline uint32_t colorPixel(double invR, double v, double r, const std::vector<Fuente> &fuentes, double t) {
    const double u = invR + 0.35 * t;
    const int n = static_cast<int>(fuentes.size());
    const Fuente *f = fuentes.data();
    double campo = 0.0, acumHue = 0.0, acumPeso = 1e-6;
#if defined(__FAST_MATH__) && defined(_OPENMP)
    #pragma omp simd reduction(+ : campo, acumHue, acumPeso)  // solo en el build -ffast-math
#endif
    for (int i = 0; i < n; ++i) {
        const double du = u - f[i].posU, dv = v - f[i].posV;
        const double onda = f[i].amplitud * std::sin(f[i].k * std::sqrt(du * du + dv * dv) + f[i].desfase);
        const double peso = std::fabs(onda);
        campo += onda;
        acumHue += peso * f[i].hue;
        acumPeso += peso;
    }
    campo /= n;
    const double hueFuentes = acumHue / acumPeso;

    const double anillos = 0.5 + 0.5 * std::sin(u * 11.0 + campo * 6.0);
    const double radios = 0.5 + 0.5 * std::sin(v * PI * 3.0 + campo * 4.0 + 0.6 * u);
    const double estruct = std::pow(anillos, 1.6) * (0.30 + 0.70 * std::pow(radios, 1.4));
    const double brillo = 0.22 + 1.05 / (1.0 + 2.2 * r * r);
    const double val = std::pow(std::clamp(estruct * (0.55 + 1.5 * brillo), 0.0, 1.0), 0.9);

    double hue = std::fmod(0.45 * hueFuentes + 0.17 * u + 0.45 * campo + 0.05, 1.0);
    if (hue < 0) hue += 1.0;
    const double sat = std::clamp(0.95 - 0.55 * val * brillo, 0.15, 1.0);
    return hsvARgb(hue, sat, val);
}

// Suma R+G+B; su total por frame es la métrica global de brillo (entera y exacta).
inline int sumaCanales(uint32_t argb) {
    return static_cast<int>(((argb >> 16) & 0xFF) + ((argb >> 8) & 0xFF) + (argb & 0xFF));
}
