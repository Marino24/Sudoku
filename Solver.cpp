#include "Solver.h"
#include "DLX.h"
#include <queue>

// Removes val from all 20 peers' candidate sets
static bool propagateToPeers(Grid& g, int k, int val) {
    for (int i = 0; i < 20; i++) {
        int peer = Grid::peers[k][i];
        if (!g.eliminate(peer, val)) return false;
    }
    return true;
}

// Plain MRV: find empty cell with fewest candidates. Returns -1 if none
static int mrvCell(const Grid& g) {
    int best = -1, bestN = 10;
    for (int k = 0; k < 81; k++) {
        if (g.cells[k].value != 0) continue;
        int n = g.cells[k].candidateCount();
        if (n < bestN) { bestN = n; best = k; }
    }
    return best;
}

static bool cellInAnyConstraint(const Grid& g, int k) {
    for (auto& c : g.constraints)
        if (c->involves(k)) return true;
    return false;
}

// Constraint-aware MRV: ties are broken in favor of a cell that belongs to a variant
static int mrvCellConstraintAware(const Grid& g) {
    int best = -1, bestN = 10;
    bool bestConstrained = false;
    for (int k = 0; k < 81; k++) {
        if (g.cells[k].value != 0) continue;
        int n = g.cells[k].candidateCount();
        if (n > bestN) continue;

        if (n < bestN) {
            best = k;
            bestN = n;
            bestConstrained = cellInAnyConstraint(g, k);
        }
        else { // n == bestN: only move on a constrained tie-breaker
            if (!bestConstrained && cellInAnyConstraint(g, k)) {
                best = k;
                bestConstrained = true;
            }
        }
    }
    return best;
}

// Variant constraint gate after a branch's propagation has settled
static bool checkConstraints(Grid& g) {
    for (auto& c : g.constraints)
        if (!c->onAssign(g, 0, 0)) return false;
    return true;
}

// Sum of candidate counts as a signal for the CSP/Hybrid propagation fixed point
static int totalCandidates(const Grid& g) {
    int total = 0;
    for (int k = 0; k < 81; k++) total += g.cells[k].candidateCount();
    return total;
}

// Algorithm 1 – Backtracking + DFS
static bool eliminateWeak(Grid& g, int cell, int val) {
    if (!g.cells[cell].hasCandidate(val)) return true;
    g.cells[cell].removeCandidate(val);
    return g.cells[cell].candidateCount() > 0;
}

static bool propagateWeak(Grid& g, int k, int val) {
    for (int i = 0; i < 20; i++)
        if (!eliminateWeak(g, Grid::peers[k][i], val)) return false;
    return true;
}

static bool seedWeak(Grid& g) {
    for (int k = 0; k < 81; k++)
        if (g.cells[k].value != 0)
            if (!propagateWeak(g, k, g.cells[k].value)) return false;
    return true;
}

static bool weakBranch(Grid& branch, int k, int d) {
    branch.cells[k].value = d;
    branch.cells[k].setOnlyCandidate(d);

    if (!propagateWeak(branch, k, d)) return false;

    for (auto& c : branch.constraints)          // single pass, no fixed point
        if (!c->prune(branch)) return false;

    return checkConstraints(branch);
}

static bool dfsRecursive(Grid& g, SolveResult& sr, int depth) {
    sr.recursiveCalls++;
    sr.maxDepth = std::max(sr.maxDepth, depth);

    int k = mrvCellConstraintAware(g);
    if (k == -1) return true;

    for (int d = 1; d <= 9; d++) {
        if (!g.cells[k].hasCandidate(d)) continue;
        sr.guesses++;

        Grid branch = g;
        if (weakBranch(branch, k, d) && dfsRecursive(branch, sr, depth + 1)) {
            g = branch;
            return true;
        }
        sr.backtracks++;
    }
    return false;
}

SolveResult Solver::solveBacktracking(const Grid& grid) {
  
    SolveResult sr;
    sr.solvedGrid = grid;
    sr.startTimer();
    sr.solved = seedWeak(sr.solvedGrid) && dfsRecursive(sr.solvedGrid, sr, 0);
    sr.stopTimer();
    return sr;
}

static void countDFSRecursive(Grid& g, int limit, int& count,
    SolveResult& sr, int depth) {
    if (count >= limit) return;
    sr.recursiveCalls++;
    sr.maxDepth = std::max(sr.maxDepth, depth);

    int k = mrvCellConstraintAware(g);
    if (k == -1) { count++; return; }

    for (int d = 1; d <= 9; d++) {
        if (count >= limit) return;
        if (!g.cells[k].hasCandidate(d)) continue;
        sr.guesses++;

        Grid branch = g;
        if (weakBranch(branch, k, d))
            countDFSRecursive(branch, limit, count, sr, depth + 1);
        else
            sr.backtracks++;
    }
}

int Solver::countSolutionsDFS(const Grid& grid, int limit, SolveResult& sr) {
    int count = 0;
    Grid g = grid;
    if (!seedWeak(g)) return 0;
    countDFSRecursive(g, limit, count, sr, 0);
    return count;
}

// Algorithm 2 – CSP: AC-3 + Forward Checking 
static bool hiddenSingles(Grid& g, int k, int val) {
    for (int g3 = 0; g3 < 3; g3++) {
        int grp = Grid::cellGroups[k][g3];
        int count = 0, last = -1;
        for (int j = 0; j < 9; j++) {
            int p = Grid::groups[grp][j];
            if (g.cells[p].hasCandidate(val)) { count++; last = p; }
        }
        if (count == 0) return false; // val has no home in this group
        if (count == 1 && g.cells[last].value == 0) {
            // Force assign - will trigger further propagation
            g.cells[last].value = val;
            g.cells[last].setOnlyCandidate(val);
            if (!propagateToPeers(g, last, val)) return false;
            if (!hiddenSingles(g, last, val)) return false;
        }
    }
    return true;
}

static bool ac3Propagate(Grid& g, int startK, int assignedVal) {
    // Queue of (cell, value_removed) pairs to process
    std::queue<std::pair<int, int>> q;

    // Seed queue: remove assignedVal from all peers of startK
    for (int i = 0; i < 20; i++) {
        int peer = Grid::peers[startK][i];
        if (!g.cells[peer].hasCandidate(assignedVal)) continue;

        g.cells[peer].removeCandidate(assignedVal);
        int n = g.cells[peer].candidateCount();

        if (n == 0) return false; // Forward check: contradiction

        if (n == 1 && g.cells[peer].value == 0) {
            // Naked single found - propagate it
            int v = g.cells[peer].singleCandidate();
            g.cells[peer].value = v;
            q.push({ peer, v });
        }
    }

    // Check hidden singles for assignedVal across groups
    if (!hiddenSingles(g, startK, assignedVal)) return false;

    // Process queue: propagate all naked singles found
    while (!q.empty()) {
        auto [cell, val] = q.front(); q.pop();

        for (int i = 0; i < 20; i++) {
            int peer = Grid::peers[cell][i];
            if (!g.cells[peer].hasCandidate(val)) continue;

            g.cells[peer].removeCandidate(val);
            int n = g.cells[peer].candidateCount();

            if (n == 0) return false;

            if (n == 1 && g.cells[peer].value == 0) {
                int v = g.cells[peer].singleCandidate();
                g.cells[peer].value = v;
                q.push({ peer, v });
            }
        }

        if (!hiddenSingles(g, cell, val)) return false;
    }

    return true;
}

static bool constraintPrune(Grid& g, SolveResult* sr = nullptr) {
    bool changed = true;
    while (changed) {
        int before = totalCandidates(g);

        for (auto& c : g.constraints)
            if (!c->prune(g)) return false;

        for (int k = 0; k < 81; k++) {
            if (g.cells[k].value != 0) continue;
            int n = g.cells[k].candidateCount();
            if (n == 0) return false;
            if (n == 1) {
                int v = g.cells[k].singleCandidate();
                g.cells[k].value = v;
                if (sr) sr->cspNodes++;
                if (!ac3Propagate(g, k, v)) return false;
            }
        }

        int after = totalCandidates(g);
        changed = (after != before);
    }
    return true;
}

// Givens are never removed from their peers at load time
static bool cspSeed(Grid& g) {
    for (int k = 0; k < 81; k++)
        if (g.cells[k].value != 0 && !ac3Propagate(g, k, g.cells[k].value))
            return false;
    return constraintPrune(g) && checkConstraints(g);
}

static bool propagateFull(Grid& g, int k, int val) {
    if (!ac3Propagate(g, k, val)) return false;
    return constraintPrune(g);
}

static bool cspRecursive(Grid& g, SolveResult& sr, int depth) {
    sr.recursiveCalls++;
    sr.maxDepth = std::max(sr.maxDepth, depth);

    int k = mrvCellConstraintAware(g);
    if (k == -1) return true; // Solved

    for (int d = 1; d <= 9; d++) {
        if (!g.cells[k].hasCandidate(d)) continue;

        sr.guesses++;

        Grid branch = g;
        branch.cells[k].value = d;
        branch.cells[k].setOnlyCandidate(d);

        // AC-3 + forward checking + variant constraint pruning
        if (propagateFull(branch, k, d) && checkConstraints(branch)) {
            if (cspRecursive(branch, sr, depth + 1)) {
                g = branch;
                return true;
            }
        }

        sr.backtracks++;
    }
    return false;
}

SolveResult Solver::solveCSP(const Grid& grid) {
    SolveResult sr;
    sr.solvedGrid = grid;
    sr.startTimer();
    sr.solved = cspSeed(sr.solvedGrid) && cspRecursive(sr.solvedGrid, sr, 0);
    sr.stopTimer();
    return sr;
}

static void countRecursive(Grid& g, int limit, int& count) {
    if (count >= limit) return;

    int k = mrvCellConstraintAware(g);
    if (k == -1) { count++; return; }

    for (int d = 1; d <= 9; d++) {
        if (count >= limit) return;
        if (!g.cells[k].hasCandidate(d)) continue;

        Grid branch = g;
        branch.cells[k].value = d;
        branch.cells[k].setOnlyCandidate(d);

        if (propagateFull(branch, k, d) && checkConstraints(branch))
            countRecursive(branch, limit, count);
    }
}

int Solver::countSolutions(const Grid& grid, int limit) {
    int count = 0;
    Grid g = grid;
    if (!cspSeed(g)) return 0;
    countRecursive(g, limit, count);
    return count;
}

static void countCSPRecursiveTracked(Grid& g, int limit, int& count,
    SolveResult& sr) {
    if (count >= limit) return;

    sr.recursiveCalls++;

    int k = mrvCellConstraintAware(g);
    if (k == -1) { count++; return; }

    for (int d = 1; d <= 9; d++) {
        if (count >= limit) return;
        if (!g.cells[k].hasCandidate(d)) continue;

        sr.guesses++;

        Grid branch = g;
        branch.cells[k].value = d;
        branch.cells[k].setOnlyCandidate(d);

        if (propagateFull(branch, k, d) && checkConstraints(branch))
            countCSPRecursiveTracked(branch, limit, count, sr);
        else
            sr.backtracks++;
    }
}

int Solver::countSolutionsCSP(const Grid& grid, int limit, SolveResult& sr) {
    int count = 0;
    Grid g = grid;
    if (!cspSeed(g)) return 0;
    countCSPRecursiveTracked(g, limit, count, sr);
    return count;
}


// Algorithm 3 – Dancing Links
SolveResult Solver::solveDLX(const Grid& grid) {
    SolveResult sr;
    sr.solvedGrid = grid;
    sr.startTimer();

    DLX dlx;
    dlx.build(grid);
    sr.solved = dlx.solve(sr.solvedGrid, sr);

    sr.stopTimer();
    return sr;
}

int Solver::countSolutionsDLX(const Grid& grid, int limit, SolveResult& sr) {
    DLX dlx;
    dlx.build(grid);
    return dlx.countSolutions(limit, sr);
}


// Algorithm 4 – CSP-DLX
//
// Phase 1 (CSP pre-processing): run the same propagation machinery as
// Algorithm 2 - AC-3 + forward checking + hidden singles + variant constraint pruning - to a fixed point, 
// WITHOUT any guessing/branching
//
// Phase 2 (DLX search): build the exact-cover matrix from the
// propagated grid and run classic DLX (Algorithm X). Each
// DLX placement is gated by the grid's registered
// constraints via a persistent shadow grid, kept incrementally in
// sync with DLX's own search path through onPlaceCheck (commit +
// check) and onUnplace (undo on backtrack)

static bool cspPreprocess(Grid& g, SolveResult& sr) {
    for (int k = 0; k < 81; k++) {
        if (g.cells[k].value == 0) continue;
        sr.cspNodes++;
        if (!ac3Propagate(g, k, g.cells[k].value)) return false;
    }

    // Variant constraint pruning, interleaved with classic
    // naked-single finalization, to a fixed point
    if (!constraintPrune(g, &sr)) return false;

    return checkConstraints(g);
}

SolveResult Solver::solveHybrid(const Grid& grid) {
    SolveResult sr;
    sr.solvedGrid = grid;
    sr.startTimer();

    Grid g = grid;

    // Phase 1: CSP pre-processing
    auto cspStart = std::chrono::high_resolution_clock::now();
    bool preOk = cspPreprocess(g, sr);
    auto cspEnd = std::chrono::high_resolution_clock::now();
    sr.cspPhaseMs = std::chrono::duration<double, std::milli>(cspEnd - cspStart).count();

    if (!preOk) {
        sr.solved = false;
        sr.stopTimer();
        return sr;
    }

    if (g.isSolved() && checkConstraints(g)) {
        sr.solved = true;
        sr.solvedGrid = g;
        sr.stopTimer();
        return sr;
    }

    // Phase 2: DLX search
    Grid shadow = g;

    DLX dlx;
    dlx.build(g);
    
    dlx.onPlaceCheck = [&](int cell, int digit) -> bool {
        shadow.cells[cell].value = digit;
        return checkConstraints(shadow);
        };

    dlx.onUnplace = [&](int cell, int /*digit*/) {
        shadow.cells[cell].value = 0;
        };

    SolveResult dlxSr;
    dlxSr.solvedGrid = g;
    dlxSr.startTimer();
    bool solved = dlx.solve(dlxSr.solvedGrid, dlxSr);
    dlxSr.stopTimer();

    sr.dlxRecursiveCalls = dlxSr.recursiveCalls;
    sr.dlxGuesses = dlxSr.guesses;
    sr.dlxBacktracks = dlxSr.backtracks;
    sr.dlxPhaseMs = dlxSr.solveTimeMs;

    sr.solved = solved;
    if (solved) sr.solvedGrid = dlxSr.solvedGrid;

    sr.stopTimer();
    return sr;
}

int Solver::countSolutionsHybrid(const Grid& grid, int limit, SolveResult& sr) {
    Grid g = grid;

    // Phase 1: CSP pre-processing
    if (!cspPreprocess(g, sr)) return 0;

    if (g.isSolved() && checkConstraints(g)) return 1;

    // Phase 2: DLX enumeration
    Grid shadow = g;

    DLX dlx;
    dlx.build(g);
    dlx.onPlaceCheck = [&](int cell, int digit) -> bool {
        shadow.cells[cell].value = digit;
        return checkConstraints(shadow);
        };
    dlx.onUnplace = [&](int cell, int /*digit*/) {
        shadow.cells[cell].value = 0;
        };

    SolveResult dlxSr;
    int count = dlx.countSolutions(limit, dlxSr);

    sr.dlxRecursiveCalls = dlxSr.recursiveCalls;
    sr.dlxGuesses = dlxSr.guesses;
    sr.dlxBacktracks = dlxSr.backtracks;

    return count;
}