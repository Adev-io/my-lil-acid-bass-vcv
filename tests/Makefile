CXX ?= g++
# the plugin's own flags (Rack's compile.mk), with its -march per CPU so the tests also run on Apple Silicon
MARCH := $(if $(filter x86_64 amd64,$(shell uname -m)),-march=nehalem,-march=armv8-a+fp+simd)
CXXFLAGS ?= -std=c++11 -O3 -funsafe-math-optimizations $(MARCH) -Wall -Wextra -I../core
BIN := ../build/tests.exe

all: run

$(BIN): test.cpp $(wildcard ../core/*.h)
	@mkdir -p ../build
	$(CXX) $(CXXFLAGS) test.cpp -o $(BIN)

run: $(BIN)
	$(BIN)

.PHONY: all run
