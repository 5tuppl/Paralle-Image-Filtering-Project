CXX := mpic++
CXXFLAGS := -O3 -std=c++17 -Wall -Wextra -Wpedantic -fopenmp
# Disable deprecated MPI C++ bindings; this project uses the modern C MPI API from C++.
CPPFLAGS := -Iinclude -DOMPI_SKIP_MPICXX=1 -DMPICH_SKIP_MPICXX=1
LDFLAGS := -fopenmp

FILTER_TARGET := bin/parallel_filter
GENERATOR_TARGET := bin/generate_image
BENCHMARK_TARGET := bin/benchmark
TEST_TARGET := bin/test_correctness
FILTER_OBJECTS := build/main.o build/image.o build/filter.o

.PHONY: all clean test benchmark benchmark-quick

all: $(FILTER_TARGET) $(GENERATOR_TARGET) $(BENCHMARK_TARGET)

$(FILTER_TARGET): $(FILTER_OBJECTS) | bin
	$(CXX) $(FILTER_OBJECTS) $(LDFLAGS) -o $@

$(GENERATOR_TARGET): build/generate_image.o build/image.o | bin
	$(CXX) $^ $(LDFLAGS) -o $@

$(BENCHMARK_TARGET): build/benchmark.o build/image.o | bin
	$(CXX) $^ $(LDFLAGS) -o $@

$(TEST_TARGET): build/test_correctness.o | bin
	$(CXX) $^ -o $@

build/%.o: src/%.cpp include/image.h include/filter.h | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

build bin:
	mkdir -p $@

test: all $(TEST_TARGET)
	bin/test_correctness

benchmark: all
	bin/benchmark

benchmark-quick: all
	bin/benchmark --quick

clean:
	rm -rf build bin data/generated_*.ppm tests/input_*.ppm tests/output_*.ppm
