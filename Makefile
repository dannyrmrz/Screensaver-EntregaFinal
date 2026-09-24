# Screensaver "Túnel Caleidoscópico de Interferencia" — Proyecto 1, CC3069 UVG 2026
#   make seq | par | par_fast | verify | bench | graficas | clean

CXX      ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra
SDL      := $(shell pkg-config --cflags --libs sdl2 SDL2_ttf)
COMUN    := src/kaleidoscope.cpp src/motor.cpp
HEADERS  := $(wildcard src/*.hpp)

.PHONY: all seq par par_fast verify bench graficas clean

all: seq par

seq: screensaver_seq
par: screensaver_par
par_fast: screensaver_par_fast

screensaver_seq: src/screensaver_seq.cpp $(COMUN) $(HEADERS)
	$(CXX) $(CXXFLAGS) src/screensaver_seq.cpp $(COMUN) -o $@ $(SDL)

screensaver_par: src/screensaver_par.cpp $(COMUN) $(HEADERS)
	$(CXX) $(CXXFLAGS) -fopenmp src/screensaver_par.cpp $(COMUN) -o $@ $(SDL)

# Variante experimental: -ffast-math cambia los resultados numéricos (ver README).
screensaver_par_fast: src/screensaver_par.cpp $(COMUN) $(HEADERS)
	$(CXX) -O3 -march=native -ffast-math -std=c++17 -Wall -Wextra -fopenmp src/screensaver_par.cpp $(COMUN) -o $@ $(SDL)

verify: par
	./screensaver_par --n 64 --verify

bench: seq par par_fast
	bash bench/run_bench.sh

graficas:
	python3 bench/plot_results.py

clean:
	rm -f screensaver_seq screensaver_par screensaver_par_fast
