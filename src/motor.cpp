/*
 * motor.cpp — Ventana SDL (RAII), ciclo interactivo con FPS y modo benchmark.
 * Todas las llamadas a SDL ocurren en el hilo principal y fuera de toda región
 * paralela: el renderer de SDL no es thread-safe.
 *
 * Autores: Carlos Daniel Estrada Vega (20853), André Emilio Pivaral López (23574),
 *          Daniela Ramírez de León (23053)
 */
#define SDL_MAIN_HANDLED
#include "motor.hpp"

#include <SDL.h>
#include <SDL_ttf.h>

#include <chrono>
#include <cstdio>

namespace {

constexpr int FRAMES_CALENTAMIENTO = 5;      // descartados antes de medir
constexpr double DT_BENCH = 1.0 / 60.0;      // paso de tiempo fijo en --bench
constexpr double PERIODO_FPS = 0.5;          // segundos entre actualizaciones del FPS
const char *const FUENTES_TTF[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    "C:/Windows/Fonts/consola.ttf",
    "/System/Library/Fonts/Menlo.ttc",
};

// Recursos SDL de la ventana; se liberan en orden inverso a su creación.
class Ventana {
public:
    Ventana() = default;
    Ventana(const Ventana &) = delete;
    Ventana &operator=(const Ventana &) = delete;

    ~Ventana() {
        if (texTexto) SDL_DestroyTexture(texTexto);
        if (fuente) TTF_CloseFont(fuente);
        if (ttfIniciado) TTF_Quit();
        if (textura) SDL_DestroyTexture(textura);
        if (render) SDL_DestroyRenderer(render);
        if (ventana) SDL_DestroyWindow(ventana);
        if (sdlIniciado) SDL_Quit();
    }

    bool iniciar(const Parametros &p) {
        ancho = p.ancho;
        SDL_SetMainReady();
        if (SDL_Init(SDL_INIT_VIDEO) != 0) return fallo("SDL_Init");
        sdlIniciado = true;

        const Uint32 banderas = SDL_WINDOW_SHOWN | (p.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        ventana = SDL_CreateWindow("Screensaver - Tunel Caleidoscopico", SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED, p.ancho, p.alto, banderas);
        if (!ventana) return fallo("SDL_CreateWindow");

        render = SDL_CreateRenderer(ventana, -1, SDL_RENDERER_ACCELERATED);
        if (!render) render = SDL_CreateRenderer(ventana, -1, SDL_RENDERER_SOFTWARE);
        if (!render) return fallo("SDL_CreateRenderer");
        SDL_RenderSetLogicalSize(render, p.ancho, p.alto);

        textura = SDL_CreateTexture(render, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                    p.ancho, p.alto);
        if (!textura) return fallo("SDL_CreateTexture");

        // El texto es opcional: sin SDL_ttf o sin fuente, el FPS va en el título.
        if (TTF_Init() == 0) {
            ttfIniciado = true;
            for (const char *ruta : FUENTES_TTF)
                if ((fuente = TTF_OpenFont(ruta, std::max(14, p.alto / 40)))) break;
        }
        if (!fuente) std::fprintf(stderr, "Aviso: sin fuente TTF, el FPS se muestra en el título.\n");
        return true;
    }

    // false cuando el usuario cierra la ventana o presiona ESC / Q.
    bool procesarEventos() {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return false;
            if (e.type == SDL_KEYDOWN &&
                (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_q)) return false;
        }
        return true;
    }

    void mostrar(const std::vector<uint32_t> &pixeles, const std::string &texto, const std::string &captura) {
        SDL_UpdateTexture(textura, nullptr, pixeles.data(), ancho * static_cast<int>(sizeof(uint32_t)));
        SDL_RenderClear(render);
        SDL_RenderCopy(render, textura, nullptr, nullptr);
        dibujarTexto(texto);
        if (!captura.empty()) guardarCaptura(captura);
        SDL_RenderPresent(render);
    }

private:
    bool fallo(const char *funcion) {
        std::fprintf(stderr, "Error en %s: %s\n", funcion, SDL_GetError());
        return false;
    }

    void dibujarTexto(const std::string &texto) {
        if (texto != ultimoTexto) {
            ultimoTexto = texto;
            if (!fuente) { SDL_SetWindowTitle(ventana, texto.c_str()); return; }
            if (texTexto) SDL_DestroyTexture(texTexto);
            texTexto = nullptr;
            if (SDL_Surface *s = TTF_RenderUTF8_Blended(fuente, texto.c_str(), SDL_Color{255, 255, 255, 255})) {
                texTexto = SDL_CreateTextureFromSurface(render, s);
                rectTexto = SDL_Rect{10, 8, s->w, s->h};
                SDL_FreeSurface(s);
            }
        }
        if (!texTexto) return;
        SDL_Rect fondo{rectTexto.x - 6, rectTexto.y - 4, rectTexto.w + 12, rectTexto.h + 8};
        SDL_SetRenderDrawBlendMode(render, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(render, 0, 0, 0, 170);
        SDL_RenderFillRect(render, &fondo);
        SDL_RenderCopy(render, texTexto, nullptr, &rectTexto);
    }

    void guardarCaptura(const std::string &ruta) {
        int w, h;
        SDL_GetRendererOutputSize(render, &w, &h);
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
        if (!s) { fallo("SDL_CreateRGBSurfaceWithFormat"); return; }
        if (SDL_RenderReadPixels(render, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) != 0 ||
            SDL_SaveBMP(s, ruta.c_str()) != 0)
            fallo("captura");
        else
            std::printf("Captura guardada en %s\n", ruta.c_str());
        SDL_FreeSurface(s);
    }

    int ancho = 0;
    bool sdlIniciado = false, ttfIniciado = false;
    SDL_Window *ventana = nullptr;
    SDL_Renderer *render = nullptr;
    SDL_Texture *textura = nullptr;
    TTF_Font *fuente = nullptr;
    SDL_Texture *texTexto = nullptr;
    SDL_Rect rectTexto{};
    std::string ultimoTexto;
};

double msDesde(std::chrono::steady_clock::time_point inicio) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - inicio).count();
}

}  // namespace

int ejecutarInteractivo(const Parametros &p, const InfoVersion &info, const FuncionFrame &frame) {
    Ventana ventana;
    if (!ventana.iniciar(p)) return 1;

    std::vector<uint32_t> pixeles(static_cast<size_t>(p.ancho) * p.alto);
    const auto inicio = std::chrono::steady_clock::now();
    auto marcaFps = inicio;
    int framesPeriodo = 0, cuadro = 0;
    double calculoMs = 0.0;
    char texto[160] = "FPS --";

    for (; p.frames == 0 || cuadro < p.frames; ++cuadro) {
        if (!ventana.procesarEventos()) break;

        const double t = msDesde(inicio) / 1000.0;
        const auto antes = std::chrono::steady_clock::now();
        frame(t, pixeles);
        calculoMs = msDesde(antes);

        ++framesPeriodo;
        const double periodo = msDesde(marcaFps) / 1000.0;
        if (periodo >= PERIODO_FPS) {
            std::snprintf(texto, sizeof texto, "FPS %.1f | N=%d | %dx%d | %s | %d hilo%s | calculo %.1f ms",
                          framesPeriodo / periodo, p.n, p.ancho, p.alto, info.nombre.c_str(), info.hilos,
                          info.hilos == 1 ? "" : "s", calculoMs);
            framesPeriodo = 0;
            marcaFps = std::chrono::steady_clock::now();
        }
        const bool ultimo = p.frames > 0 && cuadro == p.frames - 1;
        ventana.mostrar(pixeles, texto, ultimo ? p.captura : "");
    }

    const double total = msDesde(inicio) / 1000.0;
    std::printf("%d frames en %.2f s -> %.2f FPS promedio (%s)\n", cuadro, total, cuadro / total, texto);
    return 0;
}

int ejecutarBenchmark(const Parametros &p, const InfoVersion &info, const FuncionFrame &frame) {
    std::vector<uint32_t> pixeles(static_cast<size_t>(p.ancho) * p.alto);
    std::vector<double> tiempos;

    // Solo se cronometra el cálculo del frame; no hay ventana ni presentación.
    for (int i = 0; i < FRAMES_CALENTAMIENTO + p.frames; ++i) {
        const auto antes = std::chrono::steady_clock::now();
        frame(i * DT_BENCH, pixeles);
        const double ms = msDesde(antes);
        if (i >= FRAMES_CALENTAMIENTO) tiempos.push_back(ms);
    }

    const Estadisticas e = calcularEstadisticas(tiempos);
    const double fps = 1000.0 / e.media;
    std::printf("[bench] %s hilos=%d schedule=%s N=%d %dx%d | media %.3f ms | mediana %.3f | desv %.3f"
                " | min %.3f | max %.3f | %.2f FPS\n        muestras (ms):",
                info.nombre.c_str(), info.hilos, info.schedule.c_str(), p.n, p.ancho, p.alto,
                e.media, e.mediana, e.desv, e.minimo, e.maximo, fps);
    for (double ms : tiempos) std::printf(" %.3f", ms);
    std::printf("\n");

    if (p.csv.empty()) return 0;
    FILE *archivo = std::fopen(p.csv.c_str(), "a");
    if (!archivo) {
        std::perror(("No se pudo abrir " + p.csv).c_str());
        return 1;
    }
    std::fseek(archivo, 0, SEEK_END);
    if (std::ftell(archivo) == 0)
        std::fprintf(archivo, "version,hilos,N,ancho,alto,schedule,t_medio_ms,t_mediana_ms,desv_ms,"
                              "t_min_ms,t_max_ms,fps,muestras_ms\n");
    std::fprintf(archivo, "%s,%d,%d,%d,%d,%s,%.4f,%.4f,%.4f,%.4f,%.4f,%.3f,", info.nombre.c_str(), info.hilos,
                 p.n, p.ancho, p.alto, info.schedule.c_str(), e.media, e.mediana, e.desv, e.minimo, e.maximo, fps);
    for (size_t i = 0; i < tiempos.size(); ++i) std::fprintf(archivo, "%s%.4f", i ? ";" : "", tiempos[i]);
    std::fprintf(archivo, "\n");
    std::fclose(archivo);
    return 0;
}
