CXX = mpic++
BUILD ?= release
UNAME := $(shell uname -s)
COMPILER := $(shell $(CXX) --version 2>/dev/null | head -1)
CPPFLAGS += -Iinclude -Ithird_party/stb -DOMPI_SKIP_MPICXX=1 -DMPICH_SKIP_MPICXX=1
WARNINGS := -Wall -Wextra -Wpedantic -Wshadow -Wconversion
CXXFLAGS += -std=c++17 $(WARNINGS)
ifeq ($(UNAME),Darwin)
MACOSX_DEPLOYMENT_TARGET ?= $(shell sw_vers -productVersion)
export MACOSX_DEPLOYMENT_TARGET
ifneq (,$(findstring clang,$(COMPILER)))
OMP_PREFIX ?= $(shell brew --prefix libomp 2>/dev/null)
CXXFLAGS += -Xpreprocessor -fopenmp -I$(OMP_PREFIX)/include
LDFLAGS += -L$(OMP_PREFIX)/lib -Wl,-rpath,$(OMP_PREFIX)/lib -lomp
else
CXXFLAGS += -fopenmp
LDFLAGS += -fopenmp
endif
else
CXXFLAGS += -fopenmp
LDFLAGS += -fopenmp
endif
ifeq ($(BUILD),release)
CXXFLAGS += -O3
else ifeq ($(BUILD),sanitize)
CXXFLAGS += -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address,undefined
else
CXXFLAGS += -O0 -g
endif
OBJ := build/$(BUILD)
BIN := bin/$(BUILD)
CORE := $(addprefix $(OBJ)/,image.o codecs.o filter.o pixel.o reference.o)
RUNTIME := $(addprefix $(OBJ)/,execution.o options.o)
PROGRAMS := parallel_filter generate_image test_unit
.PHONY: all test unit integration style demo benchmark benchmark-quick debug release sanitize clean
all: $(addprefix $(BIN)/,$(PROGRAMS))
	@mkdir -p bin
	@for name in $(PROGRAMS); do ln -sf $(BUILD)/$$name bin/$$name; done
	@ln -sf ../scripts/benchmark.py bin/benchmark
	@ln -sf ../scripts/test_integration.py bin/test_correctness
$(BIN)/parallel_filter: $(OBJ)/main.o $(CORE) $(RUNTIME) | $(BIN)
	$(CXX) $^ $(LDFLAGS) -o $@
$(BIN)/generate_image: $(OBJ)/generate_image.o $(OBJ)/image.o $(OBJ)/codecs.o | $(BIN)
	$(CXX) $^ $(LDFLAGS) -o $@
$(BIN)/test_unit: $(OBJ)/test_unit.o $(OBJ)/allocation_audit.o $(CORE) | $(BIN)
	$(CXX) $^ $(LDFLAGS) -o $@
$(OBJ)/%.o: src/%.cpp | $(OBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(OBJ)/codecs.o: src/codecs.cpp | $(OBJ)
	$(CXX) $(CPPFLAGS) $(filter-out $(WARNINGS),$(CXXFLAGS)) -w -MMD -MP -c $< -o $@
$(OBJ) $(BIN):
	mkdir -p $@
unit: all
	$(BIN)/test_unit
integration: all
	python3 scripts/test_integration.py --bin $(BIN)
style:
	python3 scripts/check_style.py
test: style unit integration
demo: all
	mkdir -p data/demo
	$(BIN)/generate_image data/demo/input.png --width 670 --height 591
	$(BIN)/parallel_filter --input data/demo/input.png --output data/demo/blur.png \
		--mode openmp --threads 4 --iterations 10 --verify
	$(BIN)/parallel_filter --input data/demo/input.png --output data/demo/pixel.png \
		--mode openmp --threads 4 --filter pixel --block-size 8 --colors 32 --verify
benchmark: all
	python3 scripts/benchmark.py --bin $(BIN)
benchmark-quick: all
	python3 scripts/benchmark.py --bin $(BIN) --quick
debug release sanitize:
	$(MAKE) BUILD=$@ all
clean:
	rm -rf build bin
-include $(wildcard $(OBJ)/*.d)
