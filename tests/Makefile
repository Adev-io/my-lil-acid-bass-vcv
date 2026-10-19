CXX ?= g++
CXXFLAGS ?= -std=c++11 -O3 -funsafe-math-optimizations -march=nehalem -Wall -Wextra -I../core   # the plugin's own flags
BIN := ../build/tests.exe

all: run

$(BIN): test.cpp $(wildcard ../core/*.h)
	@mkdir -p ../build
	$(CXX) $(CXXFLAGS) test.cpp -o $(BIN)

run: $(BIN)
	$(BIN)

.PHONY: all run
