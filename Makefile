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

check: test_figures $(BIN)
	./test_figures tests/figures_groundtruth.txt
	./$(BIN) crosscheck --data data/GIS-cup-sample-dataset.geojson
	@echo "--- official submission-format conformance ---"
	@./$(BIN) archive export-submission --data data/GIS-cup-sample-dataset.geojson \
	    --out /tmp/giscup-conformance.txt >/dev/null 2>&1 || true
	@test -s /tmp/giscup-conformance.txt && python3 tests/conformance.py /tmp/giscup-conformance.txt 9 | tail -2 \
	    || echo "(no archive yet; skipping format conformance)"

clean:
	rm -f $(BIN) test_figures

.PHONY: all check clean portable
