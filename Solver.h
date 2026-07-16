#pragma once
#include "Grid.h"
#include <chrono>

struct SolveResult {
    bool   solved = false;
    int    guesses = 0;
    int    backtracks = 0;
    int    maxDepth = 0;
    int    recursiveCalls = 0;
    double solveTimeMs = 0.0;
    Grid   solvedGrid;

    // CSP-DLX only
    // Phase 1 (CSP pre-processing, no branching): number of forced
    // (naked/hidden single) propagation steps performed before DLX
    int    cspNodes = 0;
    double cspPhaseMs = 0.0;
    // Phase 2 (DLX search): DLX's own counters and timing, kept
    int    dlxRecursiveCalls = 0;
    int    dlxGuesses = 0;
    int    dlxBacktracks = 0;
    double dlxPhaseMs = 0.0;

    void startTimer() {
        _start = std::chrono::high_resolution_clock::now();
    }
    void stopTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        solveTimeMs = std::chrono::duration<double, std::milli>(end - _start).count();
    }
private:
    std::chrono::high_resolution_clock::time_point _start;
};

class Solver {
public:
    // Algorithm 1: Backtracking + DFS
    static SolveResult solveBacktracking(const Grid& grid);

    // Algorithm 2: CSP - AC-3 + Forward Checking + MRV
    static SolveResult solveCSP(const Grid& grid);

    // Algorithm 3: Dancing Links (classic only)
    static SolveResult solveDLX(const Grid& grid);

    // Algorithm 4: Hybrid CSP-DLX (variants)
    static SolveResult solveHybrid(const Grid& grid);

    // Count solutions continues to enumerate branches past the first solution instead of stopping
    static int countSolutions(const Grid& grid, int limit);

    // Generation benchmark count solutions
    static int countSolutionsDFS(const Grid& grid, int limit, SolveResult& sr);
    static int countSolutionsCSP(const Grid& grid, int limit, SolveResult& sr);
    static int countSolutionsDLX(const Grid& grid, int limit, SolveResult& sr);
    static int countSolutionsHybrid(const Grid& grid, int limit, SolveResult& sr);
};