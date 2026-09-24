<div class="caratula">

**Universidad del Valle de Guatemala**
Facultad de Ingeniería
Departamento de Ciencias de la Computación
Computación Paralela y Distribuida — Semestre 2, 2026

<br>

# Proyecto 1 — Entrega final
## Screensaver "Túnel Caleidoscópico de Interferencia": versión secuencial y paralelización con OpenMP

<br>

**Integrantes**
Carlos Daniel Estrada Vega — 20853
André Emilio Pivaral López — 23574
Daniela Ramírez de León — 23053

<br>

Guatemala, 23 de septiembre de 2026

</div>

<div class="indice">

## Índice

1. Introducción
2. Antecedentes
   2.1 OpenMP y el modelo de memoria compartida
   2.2 Metodología PCAM
   2.3 Speedup, eficiencia y ley de Amdahl
   2.4 SDL 2
3. Cuerpo
   3.1 Descripción del screensaver
   3.2 Física y trigonometría del modelo
   3.3 Diseño con PCAM
   3.4 Versión secuencial (v0)
   3.5 Versiones paralelas (v1, v2, v3)
   3.6 Protección de la memoria compartida y sincronía
   3.7 Programación defensiva y manejo de recursos
   3.8 Verificación de correctitud
   3.9 Metodología de medición
   3.10 Resultados
   3.11 Análisis
4. Conclusiones
5. Recomendaciones
6. Referencias
7. Apéndices — Anexo 1 (diagrama de flujo), Anexo 2 (catálogo de funciones), Anexo 3 (bitácora de pruebas), capturas

</div>

## 1. Introducción

El objetivo de este proyecto es diseñar un programa secuencial con potencial de paralelización y luego acelerarlo con OpenMP de forma iterativa, midiendo en cada paso el speedup y la eficiencia obtenidos. El programa elegido es un *screensaver* llamado "Túnel Caleidoscópico de Interferencia": N fuentes de onda orbitan en un plano, y el color de cada píxel se obtiene de la superposición de sus N ondas, después de aplicar transformaciones polares que producen un túnel con simetría de caleidoscopio.

La elección no es casual. El color de cada píxel se calcula de forma independiente, pero cuesta O(N) operaciones trigonométricas, por lo que un frame de 800 × 600 con N = 64 exige unos 30 millones de evaluaciones de onda. Es un problema intensivo en cómputo, con paralelismo de datos casi perfecto, en el que el cálculo ocurre deliberadamente en el CPU (y no en la GPU mediante *shaders*) para que haya carga que paralelizar con OpenMP.

Este informe describe el modelo físico y matemático, el diseño de la solución con la metodología PCAM, las tres versiones paralelas construidas sobre la secuencial, los mecanismos de sincronía empleados y los resultados de las pruebas de rendimiento: speedup, eficiencia, FPS alcanzados y el N máximo que se puede sostener a 60 FPS.

## 2. Antecedentes

### 2.1 OpenMP y el modelo de memoria compartida

OpenMP es una interfaz de programación para paralelismo en memoria compartida, basada en directivas de compilador, rutinas de biblioteca y variables de entorno. Su modelo de ejecución es *fork-join*: el programa inicia con un hilo, y al encontrar una región `parallel` se crea un equipo de hilos que la ejecuta; al terminar la región, los hilos se sincronizan y sigue solo el hilo principal. Según la especificación, al final de toda construcción de reparto de trabajo (como `for`) hay una barrera implícita, salvo que se indique `nowait`[^omp-barrier]; esto garantiza que todos los hilos terminen un `for` antes de continuar.

Las construcciones usadas en este proyecto son:

- `parallel for`: reparte las iteraciones de un ciclo entre los hilos del equipo.
- `collapse(n)`: fusiona n ciclos anidados en un único espacio de iteraciones.
- `schedule(static | dynamic | guided | runtime)`: define cómo se reparten las iteraciones; con `runtime` la decisión se toma al ejecutar mediante `omp_set_schedule`.
- `reduction(+ : var)`: cada hilo acumula en una copia privada y al final las copias se combinan.
- `simd`: pide al compilador vectorizar el ciclo.

Chapman, Jost y van der Pas destacan que OpenMP permite paralelizar de forma incremental: se parte de un programa secuencial correcto y se agregan directivas una por una[^chapman], lo que es exactamente la estrategia iterativa que pide el proyecto.

### 2.2 Metodología PCAM

Foster propone diseñar algoritmos paralelos en cuatro etapas: **P**articionamiento (dividir el cómputo y los datos en tareas pequeñas), **C**omunicación (identificar qué datos necesita cada tarea de otras), **A**glomeración (agrupar tareas para reducir el costo de comunicación y de administración) y **M**apeo (asignar los grupos a procesadores)[^foster]. En la sección 3.3 se aplica al screensaver.

### 2.3 Speedup, eficiencia y ley de Amdahl

Si T₁ es el tiempo de la versión secuencial y Tₚ el de la paralela con p hilos:

- **Speedup:** S(p) = T₁ / Tₚ
- **Eficiencia:** E(p) = S(p) / p

La ley de Amdahl acota el speedup cuando una fracción f del trabajo es serial: S(p) ≤ 1 / (f + (1 − f)/p)[^amdahl]. Invirtiéndola se obtiene la **fracción serial experimental** de Karp y Flatt, e = (1/S − 1/p) / (1 − 1/p), que permite estimar f a partir del speedup medido: si e crece con p, la pérdida de eficiencia no viene del código serial sino del overhead de paralelización o de límites del hardware[^pacheco].

### 2.4 SDL 2

Simple DirectMedia Layer es una biblioteca multiplataforma en C que da acceso de bajo nivel a video, audio y entrada[^sdl]. Se usa solo para abrir la ventana, recibir eventos de teclado y subir a la tarjeta gráfica el arreglo de píxeles ya calculado mediante una textura de *streaming* (`SDL_TEXTUREACCESS_STREAMING`). La documentación de SDL advierte que las funciones de renderizado deben llamarse desde el hilo principal[^sdl-render], por lo que ninguna llamada a SDL ocurre dentro de una región paralela. El texto de FPS se dibuja con la extensión SDL_ttf.

## 3. Cuerpo

### 3.1 Descripción del screensaver

El programa recibe como parámetro principal **N, la cantidad de fuentes de onda**. Cada fuente tiene un tono de color, frecuencia, amplitud, fase y una órbita (centro, radio, velocidad angular y ángulo inicial) generados de forma pseudoaleatoria con `std::mt19937` a partir de una semilla configurable. En cada frame:

1. Las fuentes avanzan en sus órbitas.
2. Para cada píxel se calculan coordenadas polares, se dobla el ángulo para producir la simetría de caleidoscopio y se transforma el radio en profundidad de túnel.
3. Se suman las N ondas en ese punto (interferencia) y el campo resultante determina anillos, radios, brillo y tono.
4. El color HSV se convierte a ARGB y se escribe en el búfer, que luego se sube a la ventana con una sola llamada a `SDL_UpdateTexture`.

El resultado (ver capturas del apéndice) es un túnel poligonal simétrico con anillos que avanzan hacia un centro brillante, en bandas de color que cambian a medida que las ondas se refuerzan o se cancelan. En la esquina superior izquierda se muestran los FPS, N, la resolución, la versión, los hilos y el tiempo de cálculo del frame.

### 3.2 Física y trigonometría del modelo

**Movimiento orbital.** La fuente i se mueve en una órbita elíptica deformada:

  posU = centroU + radioOrb · cos(velAng · t + angIni)
  posV = centroV + 0.35 · sin(1.3 · velAng · t + angIni)

**Coordenadas polares.** Cada píxel (x, y) se normaliza respecto al centro de la pantalla y se convierte a polares: r = √(nx² + ny²), θ = atan2(ny, nx).

**Simetría de caleidoscopio.** Con sector = 2π / simetría, el ángulo se reduce a un sector, m = θ mod sector, y se refleja respecto a su mitad: v = |m − sector/2| / (sector/2). Así, todos los sectores muestran la misma imagen reflejada.

**Proyección de túnel.** La transformación u = 0.55 / r + 0.35 t convierte la distancia al centro en profundidad: los píxeles cercanos al centro corresponden a zonas "lejanas" del túnel, y el término 0.35 t hace que el túnel avance con el tiempo.

**Superposición de ondas.** En el punto (u, v), la distancia a la fuente i es dᵢ = √((u − posUᵢ)² + (v − posVᵢ)²) y su onda es

  ondaᵢ = amplitudᵢ · sin(2 · frecuenciaᵢ · dᵢ − 1.4 t + faseᵢ)

El campo es el promedio de las N ondas. Donde las ondas llegan en fase se refuerzan (campo grande, anillos brillantes) y donde llegan en oposición se cancelan (zonas oscuras). Esta es la **interacción real entre los N elementos**: cada píxel depende de todas las fuentes a la vez. El tono resultante es el promedio de los tonos de las fuentes ponderado por |ondaᵢ|, de modo que la fuente que más contribuye en un punto domina su color.

**Luminosidad.** Los anillos (0.5 + 0.5 sin(11u + 6·campo)) y radios (0.5 + 0.5 sin(3πv + 4·campo + 0.6u)) forman la estructura, y el brillo 0.22 + 1.05 / (1 + 2.2 r²) hace que el punto de fuga sea más luminoso.

### 3.3 Diseño con PCAM

**Particionamiento.** Se usó descomposición de dominio sobre el búfer de píxeles: la tarea mínima es calcular el color de un píxel. Hay ancho × alto tareas (480 000 a 800 × 600), cada una con costo O(N). Una segunda partición, mucho más pequeña, es la actualización de las N órbitas.

**Comunicación.** Las tareas de píxel no se comunican entre sí: cada una lee las N fuentes (datos compartidos de solo lectura) y escribe un único elemento del búfer. La única dependencia es que las fuentes deben estar actualizadas antes de pintar, y que el frame debe estar completo antes de subirlo a la ventana. Ambas se resuelven con barreras. La métrica global de brillo requiere combinar un valor por tarea, lo que se resuelve con una reducción.

**Aglomeración.** Un píxel es demasiado poco trabajo para repartirlo por separado. En v1 se aglomeran bloques contiguos de píxeles (`collapse(2)` con `static`); desde v2, **filas completas**, que tienen costo casi uniforme y ocupan memoria contigua, con lo que el falso compartimiento solo puede darse en las fronteras entre bloques de hilos.

**Mapeo.** Con carga uniforme, el mapeo estático (bloques de filas consecutivas por hilo) es el de menor overhead; se compararon además `dynamic` y `guided` con distintos tamaños de bloque (barrido D).

### 3.4 Versión secuencial (v0)

`screensaver_seq` usa el mismo núcleo matemático (`kaleidoscope.hpp/.cpp`) que la paralela; las funciones del bucle caliente son `inline` y se compilan igual en ambos programas, así que la única diferencia medida es OpenMP. El frame secuencial (`renderSecuencial`) actualiza las N órbitas y recorre los píxeles con dos ciclos anidados. El motor común (`motor.cpp`) abre la ventana con renderer acelerado (con respaldo por software), crea la textura ARGB8888 y dibuja el texto de FPS con SDL_ttf.

### 3.5 Versiones paralelas

Las tres variantes son acumulativas y se eligen con `--version`:

**v1 — Paralelización directa.**

```cpp
#pragma omp parallel for collapse(2) schedule(static)
for (int y = 0; y < p.alto; ++y)
    for (int x = 0; x < p.ancho; ++x) {
        double invR, v, r;                        // privadas por construcción
        polaresDePixel(x, y, p, invR, v, r);
        pixeles[y * p.ancho + x] = colorPixel(invR, v, r, fuentes, t);
    }
```

**v2 — Scheduling y región paralela.** Reparte filas completas con `schedule(runtime)` (se elige con `--schedule` y `--chunk`), agrega la métrica global de brillo con `reduction(+:suma)` y paraleliza la actualización de fuentes cuando N ≥ 256 (con menos fuentes, crear el equipo cuesta más que lo que se ahorra).

**v3 — Menos trabajo por píxel.** r, v y 0.55/r solo dependen de (x, y), así que se calculan una sola vez en una tabla (llenada con `parallel for`) y cada frame solo los lee: se eliminan `sqrt`, `atan2` y `fmod` del bucle caliente. Además, cada frame abre **una sola** región paralela que contiene dos `omp for` (fuentes y píxeles), en lugar de dos regiones:

```cpp
#pragma omp parallel reduction(+ : suma)
{
    #pragma omp for schedule(static)
    for (int i = 0; i < n; ++i) actualizarFuente(fuentes[i], t);
    // barrera implícita: todas las fuentes listas antes de pintar
    #pragma omp for schedule(runtime)
    for (int y = 0; y < p.alto; ++y) { /* lee la tabla, pinta y acumula suma */ }
}
```

**v3-fast.** La misma v3 compilada con `-O3 -march=native -ffast-math`. Con esas opciones se activa un `#pragma omp simd reduction(...)` en el ciclo sobre las N fuentes, y GCC vectoriza `sin` y `sqrt` con instrucciones AVX2. Se documenta aparte porque `-ffast-math` permite reordenar operaciones de punto flotante y, en general, cambia los resultados numéricos.

Para mantener la comparación honesta, ciertas constantes por fuente (`k = 2·frecuencia` y `desfase = fase − 1.4t`) se precalculan una vez por frame **en todas las versiones**, incluida la secuencial; multiplicar por 2 es exacto en punto flotante, por lo que la salida no cambia.

### 3.6 Protección de la memoria compartida y sincronía

| Dato | Clasificación | Justificación |
|---|---|---|
| Variables del píxel (`invR`, `v`, `r`, `campo`, …) | privadas | Declaradas dentro del cuerpo del ciclo o de funciones `inline` |
| `fuentes` | compartida, solo lectura durante el pintado | Se escribe solo en la fase de actualización, separada por una barrera |
| `pixeles` | compartida, escritura en índices disjuntos | La iteración (x, y) solo escribe `pixeles[y·ancho + x]` |
| `tabla` (v3) | compartida, solo lectura | Se llena una vez antes del primer frame |
| `suma` | `reduction(+)` | Cada hilo acumula en una copia privada; OpenMP las combina al final |

Por lo anterior no existen condiciones de carrera y **no se necesitan `critical`, `atomic` ni locks**. Los mecanismos de sincronía que sí se usan son:

1. **Barrera implícita al final de cada `omp for`**: en v3 separa la actualización de fuentes del pintado; en todas las versiones garantiza que el frame esté completo antes de `SDL_UpdateTexture`.
2. **Reducción** (`reduction(+:suma)`) para la métrica global de brillo. Como la suma es entera, el resultado paralelo es exactamente igual al secuencial y sirve como verificación.
3. **Fin de la región paralela (join)**: tras él solo continúa el hilo principal, que es el único que llama a SDL.

### 3.7 Programación defensiva y manejo de recursos

Todos los parámetros entran por línea de comandos; no hay valores fijos en el código. Una única función, `leerEntero`, valida cada entero con `strtol`, revisa que el puntero final llegue al terminador (rechaza `12abc`, `3.5` o cadenas vacías), detecta desbordamiento con `errno` y comprueba el rango. Se rechazan `--n 0`, valores negativos, resoluciones menores a 640 × 480, schedules desconocidos, argumentos desconocidos, `--bench` con menos de 10 frames y `--csv` sin `--bench`; en cada caso se imprime un mensaje específico y el uso, y el programa termina con código 1 sin fallar.

Los recursos SDL se encapsulan en la clase `Ventana` (RAII): cada `SDL_Create*` se verifica y reporta con `SDL_GetError()`, y el destructor libera en orden inverso a la creación incluso si la creación se interrumpió. La memoria de píxeles, fuentes y tablas usa `std::vector`, y la tabla de v3 vive en un `shared_ptr` capturado por la función del frame, de modo que no hay `new`/`delete` manuales.

### 3.8 Verificación de correctitud

El modo `--verify` fija t = 3.7 y la semilla, calcula un frame con la versión secuencial y con v1, v2 y v3, y compara los búferes. Resultados (detalle en el Anexo 3):

| Configuración | Schedule | Hash FNV-1a v0 | v1, v2, v3 | Píxeles distintos | Dif. máx. por canal |
|---|---|---|---|---|---|
| N=64, 800 × 600, simetría 8 | static, dynamic,4, guided | e811ea536f4172b1 | mismo hash | 0 | 0 |
| N=16, 1280 × 720, simetría 12, semilla 7 | static | aba0778f851b1612 | mismo hash | 0 | 0 |
| N=32, 800 × 600 | guided | eae9492a6c34c7ae | mismo hash | 0 | 0 |

La suma entera R+G+B calculada con `reduction` en v2 y v3 coincide exactamente con la secuencial (por ejemplo, 93 422 142 para N = 64).

Las tres versiones paralelas son **idénticas bit a bit** a la secuencial en todas las configuraciones probadas, lo que confirma que no hay variables compartidas que debieran ser privadas.

### 3.9 Metodología de medición

- **Equipo:** AMD Ryzen 5 7520U (4 núcleos físicos, 8 hilos lógicos con SMT, 2.8–4.3 GHz), Windows 11 con WSL 2 (Ubuntu), g++ 15.2 con `-O2`, SDL 2.32. La laptop estuvo conectada a la corriente durante las pruebas.
- **Protocolo:** `--bench` corre sin ventana y mide solo el cálculo del frame con `std::chrono::steady_clock`, sin incluir la presentación. En cada configuración se descartan 5 frames de calentamiento y se miden **10 frames**; se reportan media, mediana, desviación estándar, mínimo, máximo y FPS equivalente (1000 / media).
- **Speedup y eficiencia** se calculan con el **promedio** de los tiempos, contra la versión secuencial v0 con el mismo N y la misma resolución.
- **Barridos:** A) hilos 1, 2, 4, 6 y 8 con N = 64 a 800 × 600; B) N = 4, 16, 64, 256 y 1024 a 800 × 600 con 8 hilos; C) resoluciones 640 × 480, 800 × 600, 1280 × 720 y 1920 × 1080 con N = 64; D) schedules; E) búsqueda del N máximo a 60 FPS en 640 × 480 (duplicando N y luego por bisección).

### 3.10 Resultados

El detalle completo (66 configuraciones × 10 mediciones, con media, mediana, desviación, mínimo y máximo) está en el Anexo 3. Aquí se resumen los resultados principales.

**Barrido A — speedup y eficiencia contra el número de hilos** (N = 64, 800 × 600, promedio de 10 mediciones; T<sub>v0</sub> = 1444 ms):

| Hilos | v1 speedup | v1 eficiencia | v2 speedup | v2 eficiencia | v3 speedup | v3 eficiencia |
|---|---|---|---|---|---|---|
| 1 | 0.93 | 0.93 | 0.91 | 0.91 | 1.04 | 1.04 |
| 2 | 1.94 | 0.97 | 2.31 | 1.15 | 2.10 | 1.05 |
| 4 | 4.61 | 1.15 | 3.90 | 0.97 | 3.99 | 1.00 |
| 6 | 5.00 | 0.83 | 5.13 | 0.85 | 5.46 | 0.91 |
| 8 | 4.50 | 0.56 | 6.06 | 0.76 | 5.81 | 0.73 |

**Repetición intercalada** (3 rondas de 10 mediciones cada una, N = 64, 800 × 600, 8 hilos), para separar diferencias reales del ruido de la máquina:

| Versión | Schedule | Promedio (ms) | Speedup | Eficiencia |
|---|---|---|---|---|
| v0 | — | 1555.6 | 1.00 | 1.00 |
| v1 | static, collapse(2) | 219.2 | 7.10 | 0.89 |
| v3 | static | 199.3 | 7.81 | 0.98 |
| v3 | dynamic, 4 | 185.0 | 8.41 | 1.05 |
| v3 | guided | 187.5 | 8.30 | 1.04 |

**Barrido B — speedup contra N** (800 × 600, 8 hilos):

| N | T<sub>v0</sub> (ms) | v1 | v2 | v3 | v3-fast |
|---|---|---|---|---|---|
| 4 | 242 | 4.55 | 5.36 | 6.74 | 9.31 |
| 16 | 536 | 5.26 | 5.50 | 8.23 | 17.69 |
| 64 | 1444 | 4.50 | 6.06 | 5.81 | 10.42 |
| 256 | 6698 | 6.23 | 6.73 | 6.50 | 16.48 |
| 1024 | 28 979 | 10.13 | 8.59 | 8.18 | 21.41 |

**Barrido C — resolución** (N = 64, 8 hilos): el tiempo crece en proporción al número de píxeles (v0: 1057 ms a 640 × 480 y 6842 ms a 1920 × 1080). A 1920 × 1080 los speedups fueron 7.28 (v1), 7.74 (v2), 7.91 (v3) y 19.87 (v3-fast).

**Barrido D — schedules** (v2 y v3, N = 64, 800 × 600, 8 hilos): `guided` fue el más rápido en ambas versiones (198 ms en v2 y 178 ms en v3, contra 238 y 249 ms con `static`). `dynamic` con chunk 1, 4 y 16 quedó en medio.

**N máximo que sostiene 60 FPS** (640 × 480, el canvas mínimo; búsqueda por duplicación y bisección):

| Versión | N máximo a ≥ 60 FPS | FPS con ese N | FPS con N = 1 |
|---|---|---|---|
| v0 secuencial | no alcanza | — | 7.9 |
| v1 (8 hilos) | no alcanza | — | 58.7 |
| v2 (8 hilos) | no alcanza | — | 56.7 |
| v3 (8 hilos) | **3** | 69.5 | 91.3 |
| v3-fast (8 hilos) | **16** | 60.7 | 117.3 |

![Speedup vs hilos](../bench/results/speedup_vs_hilos.png)

![Eficiencia vs hilos](../bench/results/eficiencia_vs_hilos.png)

![FPS vs N](../bench/results/fps_vs_n.png)

![Tiempo por frame vs resolución](../bench/results/tiempo_vs_resolucion.png)

### 3.11 Análisis

**Escalamiento casi lineal hasta 4 hilos.** Con 2 y 4 hilos las tres versiones obtienen speedups de 1.9–2.3 y 3.9–4.6, con eficiencias cercanas a 1. Es lo esperado para un problema con paralelismo de datos: los píxeles son independientes, no hay secciones críticas y la única parte serial dentro del cálculo es la actualización de las N órbitas, que cuesta N evaluaciones de `sin`/`cos` frente a N × 480 000 del pintado. Algunas eficiencias superan ligeramente 1 (p. ej. 1.15); se deben a la variabilidad de la línea base secuencial, cuya desviación estándar llegó a 21 % de la media (frecuencia del procesador que cambia con la temperatura y la carga de Windows).

**Ley de Amdahl y fracción serial.** La fracción serial de Karp-Flatt medida con 4 hilos es casi nula (0.009 en v2 y 0.001 en v3), y sube a 0.05 con 8 hilos. Si la parte serial fuera la causa, *e* sería constante; como crece con p, la pérdida de eficiencia viene del hardware y no del código serial. Con f ≈ 0.01, Amdahl predice un máximo de 1/(0.01 + 0.99/8) = 7.5 con 8 hilos, cercano a lo que se obtuvo en la repetición (7.1–8.4). En el modo interactivo sí aparece una parte serial importante: la subida de la textura y la presentación (SDL, hilo principal). Con v3, N = 14 y 8 hilos, el cálculo tomó 59.6 ms y la pantalla mostró 13.2 FPS (75.8 ms por frame), así que ~16 ms por frame son seriales; eso equivale a f ≈ 16 / (16 + 8·59.6) ≈ 3 % y limita el speedup del programa completo a ~1/f ≈ 30 aunque hubiera infinitos hilos.

**Caída de eficiencia de 4 a 8 hilos: SMT.** El procesador tiene 4 núcleos físicos; los hilos 5 a 8 comparten núcleo con otro hilo (hyperthreading/SMT) y por lo tanto sus unidades de ejecución. Aun así, el speedup siguió subiendo hasta ~6–8, lo que indica que un solo hilo no satura su núcleo: el ciclo sobre las N fuentes es una cadena de operaciones dependientes (`sqrt` → `sin` → acumulación) limitada por latencia, y el segundo hilo SMT aprovecha las unidades que quedan ociosas. Por eso la eficiencia "por hilo lógico" baja a 0.7–0.9, pero la eficiencia por núcleo físico sigue siendo alta. El ancho de banda de memoria no es un límite: cada frame escribe menos de 2 MB y todo lo demás cabe en caché.

**El speedup mejora al aumentar N.** En el barrido B, v2 pasa de 5.4 (N = 4) a 8.6 (N = 1024). Con N grande, cada píxel hace mucho más trabajo por la misma cantidad de overhead fijo (crear el equipo de hilos, repartir filas, barrera final y desbalance del último bloque), así que la razón cómputo/overhead crece. Además, el ciclo de ondas es justamente la parte limitada por latencia que más se beneficia de SMT. El 10.1 de v1 con N = 1024 supera el número de hilos lógicos y no es físicamente posible como speedup "puro": muestra el ruido de la línea base (un frame secuencial de 29 s acumula variaciones de frecuencia); en la repetición controlada ningún speedup pasó de 8.4.

**Mejora iterativa v1 → v2 → v3.**

- *v1* con 1 hilo es 7 % más lenta que la secuencial: `collapse(2)` obliga a recuperar (x, y) del índice fusionado con divisiones en cada iteración. Con 8 hilos quedó por debajo de v2 y v3 (219 ms contra 185–199 ms en la repetición).
- *v2* reparte filas completas, que son bloques contiguos de memoria; su ventaja sobre v1 es modesta pero constante, y su `reduction` confirma la correctitud.
- *v3* elimina `sqrt`, `atan2` y `fmod` por píxel. Esa ganancia es fija por píxel, mientras que el trabajo de las ondas crece con N: por eso v3 es 20 % más rápida que v2 con N = 4 y 33 % con N = 16, pero con N ≥ 64 la diferencia queda dentro del ruido. Es la versión que permite llegar a 60 FPS.
- *v3-fast* (misma v3 con `-O3 -march=native -ffast-math`) es 2.2–2.7 veces más rápida que v3 con N ≥ 64 porque GCC vectoriza `sin` y `sqrt` con AVX2 (4 fuentes por instrucción). Su "speedup" de 10–21 mezcla dos efectos distintos, paralelización y vectorización, por eso su eficiencia supera 1.

**Schedules.** Aunque todas las filas hacen la misma cantidad de operaciones, `guided` y `dynamic,4` resultaron 6–7 % más rápidos que `static` en la repetición, y `guided` también fue el mejor en el barrido D. La explicación es que el costo real de las filas no es idéntico (`pow` y `sin` tardan distinto según el valor de entrada) y, sobre todo, que la laptop ejecuta otros procesos: con `static`, un hilo interrumpido por el sistema operativo retrasa todo el frame, porque la barrera final espera al más lento; con reparto dinámico los demás hilos absorben su trabajo. `guided` empieza con bloques grandes (poco overhead) y termina con bloques pequeños (buen balance), así que se dejó como valor por defecto (`--schedule guided`). `dynamic` con chunk 1 no fue desastroso, como sí ocurre con ciclos baratos, porque cada fila cuesta cientos de microsegundos y pedir la siguiente es despreciable en comparación.

**N máximo a 60 FPS.** Calcular el color en el CPU es caro: incluso con N = 1, cada píxel cuesta unos 490 ns en la versión secuencial (`atan2`, tres `pow`, varios `sin` y la conversión HSV), así que v0 no pasa de 7.9 FPS a 640 × 480 y nunca llega a 60 FPS. v1 y v2 se quedan justo debajo (57–59 FPS con N = 1). v3, al quitar la trigonometría polar del bucle, sostiene 60 FPS hasta **N = 3**, y v3-fast hasta **N = 16**. Para valores mayores de N el programa sigue siendo fluido en términos relativos: con N = 64 a 800 × 600, v3 da ~5.3 FPS contra 0.69 de la secuencial (7.8 veces más).

## 4. Conclusiones

1. La versión secuencial y las tres versiones paralelas despliegan el screensaver correctamente. `--verify` mostró que v1, v2 y v3 producen búferes **idénticos bit a bit** a la secuencial con todos los schedules probados, lo que confirma que la clasificación de variables (privadas, compartidas de solo lectura, escritura en índices disjuntos y `reduction`) es correcta y que no se necesitan `critical` ni `atomic`.
2. La paralelización con OpenMP escaló casi linealmente hasta los 4 núcleos físicos (speedup 3.9–4.6, eficiencia ≈ 1) y llegó a speedups de 7.1–8.4 con 8 hilos lógicos (eficiencia 0.89–1.05) en la repetición controlada, gracias a que SMT aprovecha las unidades ociosas de un cálculo limitado por latencia.
3. La fracción serial del cálculo es menor al 1 %; la pérdida de eficiencia con más hilos se explica por el hardware (SMT) y por el ruido del sistema operativo, no por código serial. En el modo interactivo, la presentación con SDL (serial por diseño) representa ~3 % del frame y es el siguiente límite según la ley de Amdahl.
4. El speedup aumenta con N porque crece la razón entre cómputo y overhead fijo por frame.
5. Las mejoras iterativas funcionaron en el régimen esperado: repartir filas (v2) y usar `guided` mejoró el balance frente a `collapse(2)`; precalcular la tabla polar (v3) redujo el tiempo 20–33 % con N pequeño. Solo v3 y v3-fast alcanzan 60 FPS a 640 × 480, con N máximo de 3 y 16 respectivamente.

## 5. Recomendaciones

1. Medir en un equipo de escritorio con frecuencia fija (o con el modo de energía en "Máximo rendimiento") y con menos procesos en segundo plano: la desviación de la línea base secuencial (hasta 21 %) produjo algunos speedups aparentes mayores que el número de hilos.
2. Reportar también la mediana o el mínimo de las mediciones, que son menos sensibles a interrupciones del sistema operativo que el promedio.
3. Para aumentar N a 60 FPS, reducir el trabajo por píxel: guardar las fuentes como estructura de arreglos (SoA) para vectorizar sin `-ffast-math`, reemplazar las tres llamadas a `pow` por tablas o aproximaciones, o calcular a menor resolución y escalar la textura.
4. Usar `-ffast-math` solo después de comparar con `--verify`: en estas pruebas no cambió ningún píxel, pero no hay garantía para otras semillas o compiladores.
5. Explorar una región `parallel` persistente entre frames (el hilo maestro hace las llamadas a SDL entre dos barreras) para eliminar la creación del equipo de hilos en cada frame, y superponer el cálculo del frame siguiente con la presentación del actual (doble búfer).

## 6. Referencias

[^omp-barrier]: OpenMP Architecture Review Board. (2021). *OpenMP Application Programming Interface, Version 5.2*, sección 11.5 "Worksharing-Loop Constructs". https://www.openmp.org/specifications/

[^chapman]: Chapman, B., Jost, G. y van der Pas, R. (2008). *Using OpenMP: Portable Shared Memory Parallel Programming*. MIT Press, cap. 1.

[^foster]: Foster, I. (1995). *Designing and Building Parallel Programs*. Addison-Wesley, cap. 2 "Designing Parallel Algorithms". https://www.mcs.anl.gov/~itf/dbpp/

[^amdahl]: Amdahl, G. M. (1967). Validity of the single processor approach to achieving large scale computing capabilities. *AFIPS Conference Proceedings*, 30, 483–485.

[^pacheco]: Pacheco, P. y Malensek, M. (2022). *An Introduction to Parallel Programming* (2.ª ed.). Morgan Kaufmann, cap. 2 y 5.

[^sdl]: SDL Project. (2026). *Simple DirectMedia Layer*. https://www.libsdl.org/

[^sdl-render]: SDL Wiki. (2026). *CategoryRender*: las funciones de renderizado solo deben llamarse desde el hilo principal. https://wiki.libsdl.org/SDL2/CategoryRender

<!-- NOTAS -->

**Bibliografía**

- Amdahl, G. M. (1967). Validity of the single processor approach to achieving large scale computing capabilities. *AFIPS Conference Proceedings*, 30, 483–485.
- Chapman, B., Jost, G. y van der Pas, R. (2008). *Using OpenMP: Portable Shared Memory Parallel Programming*. MIT Press.
- Foster, I. (1995). *Designing and Building Parallel Programs*. Addison-Wesley. https://www.mcs.anl.gov/~itf/dbpp/
- Karp, A. H. y Flatt, H. P. (1990). Measuring parallel processor performance. *Communications of the ACM*, 33(5), 539–543.
- OpenMP Architecture Review Board. (2021). *OpenMP Application Programming Interface, Version 5.2*. https://www.openmp.org/specifications/
- Pacheco, P. y Malensek, M. (2022). *An Introduction to Parallel Programming* (2.ª ed.). Morgan Kaufmann.
- SDL Project. (2026). *Simple DirectMedia Layer* y *SDL2 Wiki*. https://www.libsdl.org/ — https://wiki.libsdl.org/SDL2/

## 7. Apéndices

- **Anexo 1 — Diagrama de flujo:** `anexo1_diagrama_flujo.png` (programa completo) y `anexo1_diagrama_frame.png` (cálculo de un frame y sincronía).
- **Anexo 2 — Catálogo de funciones:** `anexo2_catalogo.md`.
- **Anexo 3 — Bitácora de pruebas:** `anexo3_bitacora.md`, con las 10 mediciones de cada prueba, speedups, eficiencias, gráficas y capturas.
