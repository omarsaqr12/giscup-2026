CXX      ?= g++
CXXFLAGS ?= -O3 -std=c++17 -fopenmp -march=native -DNDEBUG -Wall -Wno-sign-compare
BIN      := giscup

all: $(BIN) test_figures

$(BIN): src/main.cpp src/coverage.cpp src/*.hpp
	$(CXX) $(CXXFLAGS) -o $@ src/main.cpp src/coverage.cpp

test_figures: tests/test_figures.cpp src/coverage.cpp src/*.hpp
	$(CXX) $(CXXFLAGS) -o $@ tests/test_figures.cpp src/coverage.cpp

# Portable build for the submission: no -march=native, in case the graders'
# machine differs from ours.
portable: src/main.cpp src/coverage.cpp src/*.hpp
	$(CXX) -O3 -std=c++17 -fopenmp -DNDEBUG -o $(BIN) src/main.cpp src/coverage.cpp

# Baseline geometry checks. Neither submission-format validation nor the
# potentially expensive input-robustness suite is included implicitly.
check: test_figures $(BIN)
	./test_figures tests/figures_groundtruth.txt
	./$(BIN) crosscheck --data data/GIS-cup-sample-dataset.geojson

# Validate the actual solution file, not a possibly missing/stale archive
# export in a shared /tmp path. Example: make check-format SUBMISSION=output.txt
# BLOCKS=9 (use BLOCKS=1 for a deliberately small fixture).
BLOCKS ?= 9
check-format:
	@test -n "$(SUBMISSION)" || { echo "Set SUBMISSION=path/to/solution.txt" >&2; exit 2; }
	@test -s "$(SUBMISSION)" || { echo "Missing or empty submission: $(SUBMISSION)" >&2; exit 2; }
	python3 tests/conformance.py "$(SUBMISSION)" "$(BLOCKS)"

# 14 dataset mutations; opt in because each variant launches a solve and verify.
check-robustness: $(BIN)
	bash tests/robustness.sh

clean:
	rm -f $(BIN) test_figures

.PHONY: all check check-format check-robustness clean portable
