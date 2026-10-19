CXX ?= g++
CXXFLAGS ?= -std=c++11 -O2 -Wall -Wextra -I../core
BIN := ../build/tests.exe

all: run

$(BIN): test.cpp $(wildcard ../core/*.h)
	@mkdir -p ../build
	$(CXX) $(CXXFLAGS) test.cpp -o $(BIN)

run: $(BIN)
	$(BIN)

.PHONY: all run
