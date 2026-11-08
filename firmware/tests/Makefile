CXX ?= g++
CC ?= gcc
CFLAGS ?= -std=gnu99 -O0 -g -D_GNU_SOURCE -DPIKA_CONFIG_ENABLE
CXXFLAGS ?= -std=c++17 -O0 -g
PYK = ../third_party/pikapython
PYINC = -I../headers -I.. -I$(PYK) -I$(PYK)/pikascript-core -I$(PYK)/pikascript-api
INCLUDES = -Istubs -I../headers -I../Core -I.. $(PYINC)
PYOBJDIR = pika_build

PIKA_C = $(wildcard $(PYK)/pikascript-core/*.c) \
         $(wildcard $(PYK)/pikascript-api/*.c) \
         $(wildcard $(PYK)/pikascript-lib/PikaStdLib/*.c) \
         $(wildcard $(PYK)/pikascript-lib/math/*.c) \
         $(wildcard $(PYK)/pikascript-lib/time/*.c) \
         $(wildcard $(PYK)/pikascript-lib/random/*.c) \
         $(wildcard $(PYK)/pikascript-lib/pika_libc/*.c)

PIKA_OBJS = $(patsubst %.c,$(PYOBJDIR)/%.o,$(notdir $(PIKA_C))) \
            $(PYOBJDIR)/pika_wrapper.o $(PYOBJDIR)/py_draw.o $(PYOBJDIR)/py_gfx.o \
            $(PYOBJDIR)/py_turtle.o $(PYOBJDIR)/py_plot.o

OBJS = test_complex test_matrix test_phase2 test_proba test_editor test_arithmetic test_stats test_calculus test_cas test_python test_pymods

SAN = -fsanitize=address,undefined -fno-omit-frame-pointer

all: $(OBJS)

test_complex: test_complex.cpp ../Core/Evaluator.cpp ../Core/stack.cpp ../Core/Matrix.cpp
	$(CXX) $(CXXFLAGS) -I../headers -I../Core ../Core/Evaluator.cpp ../Core/stack.cpp ../Core/Matrix.cpp test_complex.cpp -lm -o $@

test_matrix: test_matrix.cpp ../Core/Evaluator.cpp ../Core/stack.cpp ../Core/Matrix.cpp
	$(CXX) $(CXXFLAGS) -I../headers -I../Core ../Core/Evaluator.cpp ../Core/stack.cpp ../Core/Matrix.cpp test_matrix.cpp -lm -o $@

test_phase2: test_phase2.cpp ../Core/Evaluator.cpp ../Core/stack.cpp ../Core/Matrix.cpp
	$(CXX) $(CXXFLAGS) -I../headers -I../Core ../Core/Evaluator.cpp ../Core/stack.cpp ../Core/Matrix.cpp test_phase2.cpp -lm -o $@

test_proba: test_proba.cpp ../Core/Evaluator.cpp ../Core/stack.cpp ../Core/Matrix.cpp
	$(CXX) $(CXXFLAGS) -I../headers -I../Core ../Core/Evaluator.cpp ../Core/stack.cpp ../Core/Matrix.cpp test_proba.cpp -lm -o $@

test_arithmetic: test_arithmetic.cpp ../Core/Evaluator.cpp ../Core/stack.cpp ../Core/Matrix.cpp
	$(CXX) $(CXXFLAGS) -I../headers -I../Core ../Core/Evaluator.cpp ../Core/stack.cpp ../Core/Matrix.cpp test_arithmetic.cpp -lm -o $@

test_stats: test_stats.cpp ../Core/Stats.cpp ../Core/Exact.cpp ../Core/Evaluator.cpp ../Core/stack.cpp ../Core/Matrix.cpp
	$(CXX) $(CXXFLAGS) -I../headers -I../Core ../Core/Stats.cpp ../Core/Exact.cpp ../Core/Evaluator.cpp ../Core/stack.cpp ../Core/Matrix.cpp test_stats.cpp -lm -o $@

test_stats_asan: test_stats.cpp ../Core/Stats.cpp ../Core/Exact.cpp ../Core/Evaluator.cpp ../Core/stack.cpp ../Core/Matrix.cpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../headers -I../Core ../Core/Stats.cpp ../Core/Exact.cpp ../Core/Evaluator.cpp ../Core/stack.cpp ../Core/Matrix.cpp test_stats.cpp -lm -o $@

test_calculus: test_calculus.cpp ../Core/Calculus.cpp ../Core/Evaluator.cpp ../Core/stack.cpp ../Core/Matrix.cpp
	$(CXX) $(CXXFLAGS) -I../headers -I../Core ../Core/Calculus.cpp ../Core/Evaluator.cpp ../Core/stack.cpp ../Core/Matrix.cpp test_calculus.cpp -lm -o $@

test_cas: test_cas.cpp ../Core/Cas.cpp ../Core/Evaluator.cpp ../Core/stack.cpp ../Core/Matrix.cpp
	$(CXX) $(CXXFLAGS) -I../headers -I../Core ../Core/Cas.cpp ../Core/Evaluator.cpp ../Core/stack.cpp ../Core/Matrix.cpp test_cas.cpp -lm -o $@

test_cas_asan: test_cas.cpp ../Core/Cas.cpp ../Core/Evaluator.cpp ../Core/stack.cpp ../Core/Matrix.cpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../headers -I../Core ../Core/Cas.cpp ../Core/Evaluator.cpp ../Core/stack.cpp ../Core/Matrix.cpp test_cas.cpp -lm -o $@

test_calculus_asan: test_calculus.cpp ../Core/Calculus.cpp ../Core/Evaluator.cpp ../Core/stack.cpp ../Core/Matrix.cpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../headers -I../Core ../Core/Calculus.cpp ../Core/Evaluator.cpp ../Core/stack.cpp ../Core/Matrix.cpp test_calculus.cpp -lm -o $@

# The editor test links widgets.cpp against the real headers (ui.h/display.h)
# and provides host stubs itself; it must not use stubs/ui.h, whose static
# inline display helpers clash with the real declarations.
test_editor: test_editor.cpp ../Core/widgets.cpp
	$(CXX) $(CXXFLAGS) -I../headers -I../Core -I.. ../Core/widgets.cpp test_editor.cpp -o $@

test_editor_asan: test_editor.cpp ../Core/widgets.cpp
	$(CXX) $(CXXFLAGS) $(SAN) -I../headers -I../Core -I.. ../Core/widgets.cpp test_editor.cpp -o $@

$(PYOBJDIR):
	mkdir -p $(PYOBJDIR)

$(PYOBJDIR)/%.o: $(PYK)/pikascript-core/%.c | $(PYOBJDIR)
	$(CC) $(CFLAGS) $(PYINC) -c $< -o $@

$(PYOBJDIR)/%.o: $(PYK)/pikascript-api/%.c | $(PYOBJDIR)
	$(CC) $(CFLAGS) $(PYINC) -c $< -o $@

$(PYOBJDIR)/%.o: $(PYK)/pikascript-lib/PikaStdLib/%.c | $(PYOBJDIR)
	$(CC) $(CFLAGS) $(PYINC) -c $< -o $@

$(PYOBJDIR)/%.o: $(PYK)/pikascript-lib/math/%.c | $(PYOBJDIR)
	$(CC) $(CFLAGS) $(PYINC) -c $< -o $@

$(PYOBJDIR)/%.o: $(PYK)/pikascript-lib/time/%.c | $(PYOBJDIR)
	$(CC) $(CFLAGS) $(PYINC) -c $< -o $@

$(PYOBJDIR)/%.o: $(PYK)/pikascript-lib/random/%.c | $(PYOBJDIR)
	$(CC) $(CFLAGS) $(PYINC) -c $< -o $@

$(PYOBJDIR)/%.o: $(PYK)/pikascript-lib/pika_libc/%.c | $(PYOBJDIR)
	$(CC) $(CFLAGS) $(PYINC) -c $< -o $@

$(PYOBJDIR)/pika_wrapper.o: ../Core/pika_wrapper.c ../headers/py_gfx.h | $(PYOBJDIR)
	$(CC) $(CFLAGS) $(PYINC) -c $< -o $@

$(PYOBJDIR)/py_draw.o: ../Core/py_draw.c ../headers/pika_wrapper.h ../headers/py_gfx.h | $(PYOBJDIR)
	$(CC) $(CFLAGS) $(PYINC) -c $< -o $@

$(PYOBJDIR)/py_gfx.o: ../Core/py_gfx.c ../headers/pika_wrapper.h ../headers/py_gfx.h | $(PYOBJDIR)
	$(CC) $(CFLAGS) $(PYINC) -c $< -o $@

$(PYOBJDIR)/py_turtle.o: ../Core/py_turtle.c ../headers/pika_wrapper.h ../headers/py_gfx.h | $(PYOBJDIR)
	$(CC) $(CFLAGS) $(PYINC) -c $< -o $@

$(PYOBJDIR)/py_plot.o: ../Core/py_plot.c ../headers/pika_wrapper.h ../headers/py_gfx.h | $(PYOBJDIR)
	$(CC) $(CFLAGS) $(PYINC) -c $< -o $@

test_python: test_python.cpp ../Apps/python.cpp ../Core/pika_wrapper.c stubs/host_input.cpp $(PIKA_OBJS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) ../Apps/python.cpp stubs/host_input.cpp test_python.cpp $(PIKA_OBJS) -lm -o $@

# Modules turtle et matplotlib.pyplot : dessin dans une image en memoire.
test_pymods: test_pymods.cpp stubs/fb_draw.c $(PIKA_OBJS)
	$(CC) $(CFLAGS) -I../headers -c stubs/fb_draw.c -o $(PYOBJDIR)/fb_draw.o
	$(CXX) $(CXXFLAGS) -I../headers test_pymods.cpp $(PYOBJDIR)/fb_draw.o $(PIKA_OBJS) -lm -o $@

run: $(OBJS)
	./test_complex
	./test_matrix
	./test_phase2
	./test_proba
	./test_arithmetic
	./test_stats
	./test_calculus
	./test_cas
	./test_editor
	./test_python
	./test_pymods

san: test_editor_asan test_stats_asan test_calculus_asan test_cas_asan
	ASAN_OPTIONS=detect_leaks=0 ./test_editor_asan
	ASAN_OPTIONS=detect_leaks=0 ./test_stats_asan
	ASAN_OPTIONS=detect_leaks=0 ./test_calculus_asan
	ASAN_OPTIONS=detect_leaks=0 ./test_cas_asan

clean:
	rm -f $(OBJS) test_editor_asan test_stats_asan test_calculus_asan
	rm -rf $(PYOBJDIR)

.PHONY: all run san clean
