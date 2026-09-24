# Anexo 2 — Catálogo de funciones

Proyecto 1 · Screensaver "Túnel Caleidoscópico de Interferencia" · Computación Paralela y Distribuida, UVG 2026

Integrantes: Carlos Daniel Estrada Vega (20853), André Emilio Pivaral López (23574), Daniela Ramírez de León (23053)

El código está dividido en cinco archivos:

| Archivo | Contenido |
|---|---|
| `src/kaleidoscope.hpp` | Estructuras de datos y funciones del bucle caliente (`inline`), compartidas por todas las versiones |
| `src/kaleidoscope.cpp` | Validación de argumentos, creación de fuentes, frame secuencial v0, hash y estadísticas |
| `src/motor.hpp/.cpp` | Ventana SDL (RAII), ciclo interactivo con FPS y modo benchmark |
| `src/screensaver_seq.cpp` | Programa secuencial (v0) |
| `src/screensaver_par.cpp` | Programa paralelo: v1, v2, v3 y modo `--verify` |

---

## 1. Estructuras y clases

### `struct Fuente` — `kaleidoscope.hpp`
**Propósito:** representa una de las N fuentes de onda. Cada fuente orbita en el plano (u, v) y emite una onda senoidal; la superposición de todas define el color de cada píxel.

| Campo | Tipo | Uso |
|---|---|---|
| `hue` | `double` | Tono base del color, en [0, 1) |
| `frecuencia` | `double` | Frecuencia espacial de la onda |
| `amplitud` | `double` | Amplitud de la onda |
| `fase` | `double` | Desfase inicial |
| `radioOrb`, `velAng`, `angIni` | `double` | Radio, velocidad angular y ángulo inicial de la órbita |
| `centroU`, `centroV` | `double` | Centro de la órbita |
| `posU`, `posV` | `double` | Posición actual (se recalcula cada frame) |
| `k` | `double` | 2 · frecuencia (constante, se calcula una vez) |
| `desfase` | `double` | fase − 1.4 t (se recalcula cada frame) |

### `struct Parametros` — `kaleidoscope.hpp`
**Propósito:** agrupa todos los parámetros de ejecución leídos de la línea de comandos. No hay valores fijos en el código: cada campo tiene un valor por defecto que el usuario puede cambiar.

| Campo | Tipo | Uso |
|---|---|---|
| `n` | `int` | Cantidad de fuentes de onda (obligatorio, 1–4096) |
| `ancho`, `alto` | `int` | Resolución del canvas (mín. 640×480) |
| `simetria` | `int` | Orden de simetría del caleidoscopio (3–24) |
| `semilla` | `int` | Semilla del generador pseudoaleatorio |
| `frames` | `int` | Frames a ejecutar (0 = infinito); en `--bench`, frames medidos |
| `version`, `hilos`, `chunk` | `int` | Variante paralela, hilos OpenMP y tamaño de bloque |
| `schedule` | `std::string` | `static`, `dynamic` o `guided` |
| `csv`, `captura` | `std::string` | Archivo de resultados del benchmark y BMP de captura |
| `fullscreen`, `bench`, `verify` | `bool` | Modos de ejecución |

### `struct TablaPolar` — `kaleidoscope.hpp`
**Propósito:** guarda por píxel los valores que solo dependen de (x, y) y no del tiempo, para que v3 no los recalcule cada frame.

| Campo | Tipo | Uso |
|---|---|---|
| `invR` | `std::vector<double>` | 0.55 / r (profundidad del túnel) |
| `v` | `std::vector<double>` | Ángulo doblado, en [0, 1] |
| `r` | `std::vector<double>` | Radio normalizado |

### `struct Estadisticas` — `kaleidoscope.hpp`
**Propósito:** resumen estadístico de una serie de tiempos. Campos `double`: `media`, `mediana`, `desv` (desviación estándar muestral), `minimo`, `maximo`, todos en milisegundos.

### `struct InfoVersion` — `motor.hpp`
**Propósito:** describe la versión que se ejecuta para el texto en pantalla y el CSV. Campos: `nombre` (`std::string`, "v0"…"v3"), `hilos` (`int`), `schedule` (`std::string`).

### `using FuncionFrame` — `motor.hpp`
**Propósito:** tipo `std::function<long long(double t, std::vector<uint32_t>& pixeles)>`. Es la interfaz común entre el motor y cada versión: recibe el instante `t`, llena el búfer y devuelve la suma R+G+B del frame (o −1 si la versión no la calcula).

### `class Ventana` — `motor.cpp`
**Propósito:** encapsula los recursos SDL (ventana, renderer, textura, fuente TTF y textura de texto) con RAII. El constructor no crea nada; `iniciar` crea y verifica cada recurso; el destructor los libera en orden inverso a su creación, aunque la creación se haya interrumpido a la mitad. No se puede copiar (evita liberar dos veces).

| Método | Entradas | Salidas | Descripción |
|---|---|---|---|
| `iniciar` | `p: const Parametros&` — resolución y pantalla completa | `bool` — `true` si todo se creó | Inicializa SDL, crea la ventana y un renderer acelerado (o software si falla), la textura de streaming ARGB8888 y carga una fuente TTF. Si la fuente no carga, el FPS se mostrará en el título. Reporta errores con `SDL_GetError()`. |
| `procesarEventos` | — | `bool` — `false` si hay que salir | Vacía la cola de eventos; devuelve `false` ante `SDL_QUIT`, ESC o Q. |
| `mostrar` | `pixeles: const std::vector<uint32_t>&` — frame calculado; `texto: const std::string&` — línea de FPS; `captura: const std::string&` — ruta BMP o vacío | — | Sube el búfer con una sola `SDL_UpdateTexture`, lo copia al renderer, dibuja el texto, guarda la captura si se pidió y presenta. |
| `dibujarTexto` (privado) | `texto: const std::string&` | — | Regenera la textura del texto solo si cambió y la dibuja sobre un fondo semitransparente; sin fuente, usa `SDL_SetWindowTitle`. |
| `guardarCaptura` (privado) | `ruta: const std::string&` | archivo BMP | Lee el contenido del renderer con `SDL_RenderReadPixels` y lo guarda con `SDL_SaveBMP`. |
| `fallo` (privado) | `funcion: const char*` | `bool` (siempre `false`) | Imprime el nombre de la función SDL que falló y `SDL_GetError()`. |

---

## 2. Validación de argumentos (programación defensiva) — `kaleidoscope.cpp`

### `leerEntero`
| | Nombre | Tipo | Uso |
|---|---|---|---|
| Entrada | `nombre` | `const char*` | Nombre del argumento, para el mensaje de error |
| Entrada | `texto` | `const char*` | Texto a convertir (puede ser `nullptr` si faltó el valor) |
| Entrada | `minimo`, `maximo` | `long` | Rango válido, inclusivo |
| Salida | `destino` | `int&` | Valor convertido (solo se escribe si es válido) |
| Salida | retorno | `bool` | `true` si el valor es válido |

**Descripción:** única función de validación de enteros. Usa `strtol` y revisa el puntero final (rechaza `"12abc"`, `""`, `"3.5"`), `errno == ERANGE` (desbordamiento) y el rango. Cada falla imprime un mensaje específico.

### `leerParametros`
| | Nombre | Tipo | Uso |
|---|---|---|---|
| Entrada | `argc`, `argv` | `int`, `char**` | Argumentos del programa |
| Entrada | `esParalela` | `bool` | Habilita `--version`, `--hilos`, `--schedule`, `--chunk`, `--verify` |
| Salida | `p` | `Parametros&` | Parámetros leídos |
| Salida | retorno | `bool` | `false` si hubo error (el programa termina con código 1) |

**Descripción:** recorre los argumentos; los enteros pasan por `leerEntero` con su rango, `--schedule` se compara contra los tres valores válidos, y cualquier argumento desconocido produce el mensaje de uso. Al final revisa que `--n` esté presente, que `--bench` tenga al menos 10 frames medidos (20 por defecto) y que `--csv` solo se use con `--bench`.

### `imprimirUso`
**Entradas:** `programa` (`const char*`, nombre del ejecutable), `esParalela` (`bool`). **Salida:** texto en `stderr`. **Descripción:** imprime la sintaxis, los rangos y los valores por defecto de cada parámetro.

---

## 3. Modelo y cálculo del frame

### `crearFuentes` — `kaleidoscope.cpp`
| | Nombre | Tipo | Uso |
|---|---|---|---|
| Entrada | `n` | `int` | Cantidad de fuentes |
| Entrada | `semilla` | `unsigned` | Semilla del `std::mt19937` |
| Salida | retorno | `std::vector<Fuente>` | Fuentes inicializadas |

**Descripción:** genera los parámetros de cada fuente con una distribución uniforme U(0,1) según la especificación (p. ej. `frecuencia = 1.5 + 3.0·U`). El orden de las llamadas es fijo, así que la misma semilla produce siempre las mismas fuentes: es lo que permite comparar la salida secuencial con la paralela.

### `actualizarFuente` — `kaleidoscope.hpp` (inline)
**Entradas:** `f` (`Fuente&`), `t` (`double`, segundos). **Salida:** `f.posU`, `f.posV`, `f.desfase` actualizados. **Descripción:** movimiento orbital con trigonometría: `posU = centroU + radioOrb·cos(velAng·t + angIni)`, `posV = centroV + 0.35·sin(1.3·velAng·t + angIni)`; también precalcula `fase − 1.4t` para no repetirlo por píxel.

### `polaresDePixel` — `kaleidoscope.hpp` (inline)
| | Nombre | Tipo | Uso |
|---|---|---|---|
| Entrada | `x`, `y` | `int` | Coordenadas del píxel |
| Entrada | `p` | `const Parametros&` | Resolución y simetría |
| Salida | `invR` | `double&` | 0.55 / r |
| Salida | `v` | `double&` | Ángulo doblado en [0, 1] |
| Salida | `r` | `double&` | Radio normalizado (mínimo 1e-3) |

**Descripción:** normaliza el píxel respecto al centro, lo convierte a polares con `sqrt` y `atan2`, dobla el ángulo con `fmod` dentro de un sector de `2π/simetria` (simetría de caleidoscopio) y aplica la proyección `1/r` que da la profundidad del túnel.

### `colorPixel` — `kaleidoscope.hpp` (inline)
| | Nombre | Tipo | Uso |
|---|---|---|---|
| Entrada | `invR`, `v`, `r` | `double` | Coordenadas del píxel (de `polaresDePixel` o de la tabla) |
| Entrada | `fuentes` | `const std::vector<Fuente>&` | Fuentes de onda (solo lectura) |
| Entrada | `t` | `double` | Tiempo |
| Salida | retorno | `uint32_t` | Color ARGB8888 |

**Descripción:** núcleo del screensaver. Calcula `u = 0.55/r + 0.35t`, suma las N ondas `amplitud·sin(k·d + desfase)` (d = distancia del píxel a la fuente: **interferencia**), pondera el tono de cada fuente por |onda|, construye anillos y radios, aplica un brillo que crece hacia el punto de fuga y convierte HSV a RGB. Todas sus variables son locales, así que es seguro llamarla desde varios hilos a la vez. En el build `-ffast-math` el bucle sobre N lleva `#pragma omp simd`.

### `hsvARgb` — `kaleidoscope.hpp` (inline)
**Entradas:** `h`, `s`, `val` (`double`, en [0, 1]). **Salida:** `uint32_t` ARGB con alfa 255. **Descripción:** conversión estándar HSV → RGB por sextantes del círculo de color, redondeando cada canal a 0–255.

### `sumaCanales` — `kaleidoscope.hpp` (inline)
**Entrada:** `argb` (`uint32_t`). **Salida:** `int`, R + G + B. **Descripción:** su total por frame es la métrica global de brillo; al ser entera, la suma paralela con `reduction` da exactamente el mismo valor que la secuencial.

### `renderSecuencial` (v0) — `kaleidoscope.cpp`
| | Nombre | Tipo | Uso |
|---|---|---|---|
| Entrada/salida | `fuentes` | `std::vector<Fuente>&` | Se actualizan sus posiciones |
| Salida | `pixeles` | `std::vector<uint32_t>&` | Frame calculado |
| Entrada | `p`, `t` | `const Parametros&`, `double` | Parámetros y tiempo |
| Salida | retorno | `long long` | Suma R+G+B del frame |

**Descripción:** actualiza las N órbitas y recorre todos los píxeles en un solo hilo. Es la referencia de tiempo y de correctitud.

### `renderV1` — `screensaver_par.cpp`
**Entradas/salidas:** las mismas que `renderSecuencial`; devuelve −1 (no calcula la métrica). **Descripción:** actualiza las fuentes en el hilo principal y paraleliza el bucle de píxeles con `#pragma omp parallel for collapse(2) schedule(static)`. Las variables del píxel se declaran dentro del bucle (privadas), las fuentes son de solo lectura y cada iteración escribe un índice distinto, por lo que no se necesita `critical` ni `atomic`.

### `renderV2` — `screensaver_par.cpp`
**Entradas/salidas:** las mismas que `renderSecuencial`. **Descripción:** actualiza las fuentes con `parallel for` cuando N ≥ 256 (`MIN_FUENTES_PARALELO`), reparte filas completas con `schedule(runtime)` (elegido con `--schedule`/`--chunk`) y acumula el brillo con `reduction(+:suma)`.

### `crearTablaPolar` — `screensaver_par.cpp`
**Entrada:** `p` (`const Parametros&`). **Salida:** `TablaPolar`. **Descripción:** llena en paralelo (`parallel for schedule(static)`) las tablas `invR`, `v` y `r` llamando a `polaresDePixel`, una sola vez al iniciar.

### `renderV3` — `screensaver_par.cpp`
**Entradas/salidas:** las de `renderSecuencial` más `tabla` (`const TablaPolar&`). **Descripción:** abre una sola región `parallel reduction(+:suma)` por frame; dentro, un `omp for` actualiza las fuentes y, tras su barrera implícita, otro `omp for schedule(runtime)` pinta las filas leyendo la tabla, sin `sqrt`, `atan2` ni `fmod` por píxel.

### `configurarSchedule` — `screensaver_par.cpp`
**Entrada:** `p` (`const Parametros&`). **Salida:** estado de OpenMP. **Descripción:** traduce `--schedule` y `--chunk` a `omp_set_schedule`, que usan v2 y v3 vía `schedule(runtime)`. Chunk 0 = valor por defecto.

### `etiquetaSchedule` — `screensaver_par.cpp`
**Entrada:** `p`. **Salida:** `std::string` como `"static"`, `"dynamic:4"` o `"static-collapse2"`. **Descripción:** texto del schedule para la consola y el CSV.

### `crearFrame` — `screensaver_par.cpp`
| | Nombre | Tipo | Uso |
|---|---|---|---|
| Entrada | `version` | `int` | 1, 2 o 3 |
| Entrada | `fuentes`, `p` | `std::vector<Fuente>&`, `const Parametros&` | Estado capturado por referencia |
| Salida | retorno | `FuncionFrame` | Función que calcula un frame con esa versión |

**Descripción:** devuelve una lambda que llama a la versión elegida; para v3 crea la tabla polar y la guarda en un `shared_ptr` dentro de la lambda, de modo que se libera sola cuando la lambda deja de existir.

### `verificar` — `screensaver_par.cpp`
**Entrada:** `p` (`const Parametros&`). **Salida:** `int` (0 si las tres versiones son idénticas a la secuencial, 2 si no) y un reporte en consola. **Descripción:** calcula un frame en t = 3.7 con v0 y con v1, v2, v3 (cada una con fuentes nuevas de la misma semilla) y reporta píxeles distintos, la diferencia máxima por canal y el hash FNV-1a de cada búfer.

---

## 4. Motor de ejecución — `motor.cpp`

### `ejecutarInteractivo`
| | Nombre | Tipo | Uso |
|---|---|---|---|
| Entrada | `p` | `const Parametros&` | Resolución, frames, captura |
| Entrada | `info` | `const InfoVersion&` | Versión e hilos para el texto |
| Entrada | `frame` | `const FuncionFrame&` | Cálculo del frame |
| Salida | retorno | `int` | Código de salida (1 si SDL falla) |

**Descripción:** crea la `Ventana`; en cada vuelta procesa eventos, calcula el frame con `t` = segundos transcurridos, mide el tiempo de cálculo y cada 0.5 s actualiza el texto "FPS | N | resolución | versión | hilos | cálculo ms"; luego muestra el frame desde el hilo principal. Al salir imprime el FPS promedio.

### `ejecutarBenchmark`
**Entradas:** las mismas que `ejecutarInteractivo`. **Salidas:** `int` (1 si no pudo abrir el CSV), línea de resultados en consola y una fila en el CSV. **Descripción:** sin ventana; ejecuta 5 frames de calentamiento y luego `p.frames` frames con `t = i/60`, cronometrando solo el cálculo con `std::chrono::steady_clock`. Calcula media, mediana, desviación, mínimo, máximo y FPS equivalente, imprime todas las muestras y agrega la fila al CSV (con encabezado si el archivo es nuevo).

### `msDesde`
**Entrada:** `inicio` (`steady_clock::time_point`). **Salida:** `double`, milisegundos transcurridos.

---

## 5. Utilidades — `kaleidoscope.cpp`

### `hashFNV1a`
**Entrada:** `pixeles` (`const std::vector<uint32_t>&`). **Salida:** `uint64_t`. **Descripción:** hash FNV-1a de 64 bits sobre los bytes del búfer; dos frames idénticos tienen el mismo hash.

### `calcularEstadisticas`
**Entrada:** `tiempos` (`std::vector<double>`, copia que se ordena). **Salida:** `Estadisticas`. **Descripción:** media, mediana, desviación estándar muestral (divisor n − 1), mínimo y máximo.

---

## 6. Programas principales

### `main` — `screensaver_seq.cpp`
**Entradas:** `argc`, `argv`. **Salida:** código de salida. **Descripción:** valida los parámetros, crea las fuentes, envuelve `renderSecuencial` en una `FuncionFrame` y ejecuta el modo interactivo o `--bench`.

### `main` — `screensaver_par.cpp`
**Entradas:** `argc`, `argv`. **Salida:** código de salida. **Descripción:** valida los parámetros, fija los hilos y el schedule de OpenMP, atiende `--verify`, crea el frame de la versión elegida y ejecuta el modo interactivo o `--bench`. En el build `-ffast-math` la versión se etiqueta con el sufijo `-fast`.

## 7. Scripts

| Script | Entradas | Salidas | Descripción |
|---|---|---|---|
| `bench/run_bench.sh` | Ejecutables compilados; variable `MEDICIONES` (10 por defecto) | `bench/results/crudo.csv`, `bitacora.log`, `n_max_60fps.csv`, `busqueda_60fps.csv` | Verificación de correctitud y barridos de hilos, N, resolución y schedule; búsqueda (duplicación + bisección) del N máximo a 60 FPS. |
| `bench/plot_results.py` | `crudo.csv`, `n_max_60fps.csv` | `resultados.csv`, `tablas.md`, gráficas PNG | Calcula speedup = T₀ / Tₚ (promedios) y eficiencia = speedup / p, la fracción serial de Karp-Flatt, y genera las tablas y gráficas. |
