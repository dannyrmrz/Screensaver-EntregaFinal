/*
 * motor.hpp — Ciclo de ejecución común a la versión secuencial y la paralela:
 * ventana SDL con FPS en pantalla y modo benchmark sin ventana.
 *
 * Autores: Carlos Daniel Estrada Vega (20853), André Emilio Pivaral López (23574),
 *          Daniela Ramírez de León (23053)
 */
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "kaleidoscope.hpp"

// Calcula un frame completo en el instante t; devuelve la suma R+G+B (o -1 si no la calcula).
using FuncionFrame = std::function<long long(double t, std::vector<uint32_t> &pixeles)>;

// Datos de la versión que se ejecuta, para el texto en pantalla y el CSV.
struct InfoVersion {
    std::string nombre;    // "v0", "v1", "v2", "v3"
    int hilos;
    std::string schedule;  // "-", "static", "dynamic,4", ...
};

int ejecutarInteractivo(const Parametros &p, const InfoVersion &info, const FuncionFrame &frame);
int ejecutarBenchmark(const Parametros &p, const InfoVersion &info, const FuncionFrame &frame);
