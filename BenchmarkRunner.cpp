#include "BenchmarkRunner.h"
#include "DLX.h"
#include "Solver.h"
#include "KillerConstraint.h"
#include "ArrowConstraint.h"
#include "ThermoConstraint.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <direct.h>
#include <algorithm>
#include <memory>

namespace {

    enum class GenAlgo { DFS, CSP, DLX, Hybrid };

    std::string genAlgoName(GenAlgo a) {
        switch (a) {
        case GenAlgo::DFS:    return "DFS";
        case GenAlgo::CSP:    return "CSP";
        case GenAlgo::DLX:    return "DLX";
        case GenAlgo::Hybrid: return "Hybrid";
        }
        return "?";
    }

    // Runs one uniqueness check on `g` (givens/clues already set)
    // uses algorithm specific counting 
    int runUniquenessCheck(GenAlgo algo, Grid& g, double& checkMs, long& checkNodes) {
        SolveResult sr;
        auto t0 = std::chrono::high_resolution_clock::now();

        int count = 0;
        switch (algo) {
        case GenAlgo::DFS:
            count = Solver::countSolutionsDFS(g, 2, sr);
            checkNodes = sr.recursiveCalls;
            break;
        case GenAlgo::CSP:
            count = Solver::countSolutionsCSP(g, 2, sr);
            checkNodes = sr.guesses;
            break;
        case GenAlgo::DLX:
            count = Solver::countSolutionsDLX(g, 2, sr);
            checkNodes = sr.recursiveCalls;
            break;
        case GenAlgo::Hybrid:
            count = Solver::countSolutionsHybrid(g, 2, sr);
            checkNodes = sr.cspNodes + sr.dlxRecursiveCalls;
            break;
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        checkMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        return count;
    }

    // Classic: one construction attempt = fresh solution + blind clue-removal walk down into [band.minCount, band.maxCount]
    // every removal gets its own uniqueness check
    bool genClassicAttempt(Generator& gen, GenAlgo algo, const Band& band,
        std::mt19937& rng, int& outGivens,
        int& outChecks, double& outChecksMs, long& outNodes) {

        outChecks = 0; outChecksMs = 0.0; outNodes = 0;

        int solution[81] = {};
        if (!gen.createSolution(solution)) return false;

        bool given[81];
        for (int k = 0; k < 81; k++) given[k] = true;

        std::vector<int> positions;
        for (int k = 0; k < 81; k++) positions.push_back(k);
        std::shuffle(positions.begin(), positions.end(), rng);

        int givenCount = 81;

        for (int k : positions) {
            if (givenCount <= band.minCount) break;

            given[k] = false;

            Grid attempt;
            bool ok = true;
            for (int j = 0; j < 81; j++) {
                if (!given[j]) continue;
                if (!attempt.assignFull(j, solution[j])) { ok = false; break; }
                attempt.cells[j].isGiven = true;
            }

            if (!ok) { given[k] = true; continue; }

            double checkMs = 0.0; long checkNodes = 0;
            int count = runUniquenessCheck(algo, attempt, checkMs, checkNodes);
            outChecks++;
            outChecksMs += checkMs;
            outNodes += checkNodes;

            bool accept = (count == 1);
            if (accept) givenCount--;
            else        given[k] = true;
        }

        outGivens = givenCount;
        return (givenCount >= band.minCount && givenCount <= band.maxCount);
    }

    // Variants: one construction attempt = fresh solution + one sampled clue set (cages/arrows/thermos) + 15 sampled givens 
    // uniqueness check using the specific algorithm
    bool genVariantAttempt(Generator& gen, GenAlgo algo, Variant variant,
        const Band& band, std::mt19937& rng, int& outClues,
        int& outChecks, double& outChecksMs, long& outNodes) {

        outChecks = 0; outChecksMs = 0.0; outNodes = 0;

        int solution[81] = {};
        if (!gen.createSolution(solution)) return false;

        int clueCount = std::uniform_int_distribution<int>(
            band.minCount, band.maxCount)(rng);

        std::vector<KillerData>  cages;
        std::vector<ArrowData>   arrows;
        std::vector<ThermoData>  thermos;

        if (variant == Variant::Killer) {
            cages = gen.buildKillerCages(solution, clueCount);
            if ((int)cages.size() != clueCount) return false;
        }
        else if (variant == Variant::Arrow) {
            arrows = gen.buildArrows(solution, clueCount);
            if ((int)arrows.size() != clueCount) return false;
        }
        else if (variant == Variant::Thermo) {
            thermos = gen.buildThermos(solution, clueCount);
            if ((int)thermos.size() != clueCount) return false;
        }
        else {
            return false; // Classic goes through genClassicAttempt instead
        }

        bool given[81] = {};
        std::vector<int> positions;
        for (int k = 0; k < 81; k++) positions.push_back(k);
        std::shuffle(positions.begin(), positions.end(), rng);
        for (int i = 0; i < VARIANT_ORDINARY_GIVENS; i++)
            given[positions[i]] = true;

        Grid attempt;
        bool ok = true;
        for (int k = 0; k < 81; k++) {
            if (!given[k]) continue;
            if (!attempt.assignFull(k, solution[k])) { ok = false; break; }
            attempt.cells[k].isGiven = true;
        }
        if (!ok) return false;

        for (const auto& c : cages)   attempt.addConstraint(std::make_shared<KillerConstraint>(c));
        for (const auto& a : arrows)  attempt.addConstraint(std::make_shared<ArrowConstraint>(a));
        for (const auto& t : thermos) attempt.addConstraint(std::make_shared<ThermoConstraint>(t));

        double checkMs = 0.0; long checkNodes = 0;
        int count = runUniquenessCheck(algo, attempt, checkMs, checkNodes);
        outChecks = 1;
        outChecksMs = checkMs;
        outNodes = checkNodes;

        outClues = clueCount;
        return count == 1;
    }

    std::string readSolutionLine(const std::string& path) {
        std::ifstream f(path);
        std::string line;
        while (std::getline(f, line))
            if (line.rfind("SOLUTION:", 0) == 0) return line.substr(9);
        return "";
    }

    int cellVal(const Cell& c) {
        return c.value != 0 ? c.value : c.singleCandidate();
    }

    bool gridValid(const Grid& g, const std::string& sol81) {
        int v[81];
        for (int k = 0; k < 81; k++) {
            v[k] = cellVal(g.cells[k]);
            if (v[k] < 1 || v[k] > 9) return false;
            if (v[k] != sol81[k] - '0') return false;     // equals stored solution
        }
        for (int grp = 0; grp < 27; grp++) {
            int mask = 0;
            for (int j = 0; j < 9; j++) mask |= 1 << v[Grid::groups[grp][j]];
            if (mask != 0x3FE) return false;              // rule check
        }
        Grid tmp = g;
        for (int k = 0; k < 81; k++) tmp.cells[k].value = v[k];
        for (auto& c : tmp.constraints)
            if (!c->isSatisfied(tmp)) return false;
        return true;
    }

} // namespace

BenchmarkRunner::BenchmarkRunner(unsigned int seed) {
    reseed(seed);         
}

void BenchmarkRunner::reseed(unsigned int s) {
    masterSeed = s;
    std::mt19937 master(s);
    gen.setSeed(master());
    clueRng.seed(master());
    posRng.seed(master());
    genRng.seed(master());
}

void BenchmarkRunner::ensureFolder(const std::string& folder) {
    _mkdir(folder.c_str());
}

int BenchmarkRunner::existingCount(Variant variant, const Band& band) {
    std::string folder = BENCHMARK_FOLDER + "/" + variantFolderName(variant)
        + "/" + band.label;
    int count = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec))
        if (entry.path().extension() == ".txt") count++;
    return count;
}

void BenchmarkRunner::verifyPuzzleSet(BenchmarkState& state, Variant variant) {
    const Band* bands = bandsForVariant(variant);
    std::vector<std::string> files;
    std::error_code ec;
    for (int b = 0; b < NUM_BANDS; b++)
        for (const auto& e : std::filesystem::directory_iterator(
            BENCHMARK_FOLDER + "/" + variantFolderName(variant) + "/" + bands[b].label, ec))
            if (e.path().extension() == ".txt") files.push_back(e.path().string());
    std::sort(files.begin(), files.end());

    state.running.store(true);
    state.total.store((int)files.size());
    state.generated.store(0);

    std::thread([this, files, variant, &state]() {
        int checked = 0, solverFail = 0, countFail = 0, loadFail = 0;

        for (const auto& path : files) {
            Grid grid; std::string band; int gv, cl;
            std::vector<KillerData> cages; std::vector<ArrowData> arrows;
            std::vector<ThermoData> thermos;
            std::string sol = readSolutionLine(path);
            if (sol.size() != 81 ||
                !loadPuzzle(path, grid, band, gv, cl, cages, arrows, thermos)) {
                loadFail++; continue;
            }
            for (auto& c : cages)   grid.addConstraint(std::make_shared<KillerConstraint>(c));
            for (auto& a : arrows)  grid.addConstraint(std::make_shared<ArrowConstraint>(a));
            for (auto& t : thermos) grid.addConstraint(std::make_shared<ThermoConstraint>(t));

            bool ok = true;
            SolveResult d = Solver::solveBacktracking(grid);
            SolveResult c = Solver::solveCSP(grid);
            ok &= d.solved && gridValid(d.solvedGrid, sol);
            ok &= c.solved && gridValid(c.solvedGrid, sol);
            if (variant == Variant::Classic) {
                SolveResult x = Solver::solveDLX(grid);
                ok &= x.solved && gridValid(x.solvedGrid, sol);
            }
            else {
                SolveResult h = Solver::solveHybrid(grid);
                ok &= h.solved && gridValid(h.solvedGrid, sol);
            }
            if (!ok) { solverFail++; state.addLog("  SOLVER MISMATCH: " + path); }

            SolveResult s1, s2, s3;
            bool uniq = Solver::countSolutionsDFS(grid, 2, s1) == 1
                && Solver::countSolutionsCSP(grid, 2, s2) == 1
                && (variant == Variant::Classic
                    ? Solver::countSolutionsDLX(grid, 2, s3) == 1
                    : Solver::countSolutionsHybrid(grid, 2, s3) == 1);
            if (!uniq) { countFail++; state.addLog("  UNIQUENESS MISMATCH: " + path); }

            state.generated.store(++checked);
        }

        state.addLog("=== Verification " + variantFolderName(variant) + ": checked "
            + std::to_string(checked) + ", solver mismatches " + std::to_string(solverFail)
            + ", uniqueness mismatches " + std::to_string(countFail)
            + ", load failures " + std::to_string(loadFail) + " ===");
        state.running.store(false);
        }).detach();
}

// Generate puzzle set
void BenchmarkRunner::generatePuzzleSet(BenchmarkState& state, Variant variant,
    const BandRequest requests[NUM_BANDS]) {
    const Band* bands = bandsForVariant(variant);

    reseed(masterSeed);

    int total = 0;
    for (int b = 0; b < NUM_BANDS; b++)
        if (requests[b].enabled) total += requests[b].count;

    state.running.store(true);
    state.generated.store(0);
    state.total.store(total);

    std::vector<BandRequest> reqs(requests, requests + NUM_BANDS);

    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.log.clear();
        state.log.push_back("Using seed: " + std::to_string(masterSeed));
        state.log.push_back("Starting " + variantFolderName(variant) + " generation...");
        state.log.push_back("Total target: " + std::to_string(total) + " puzzles");
    }

    std::thread([this, reqs, variant, bands, &state]() {
        ensureFolder(BENCHMARK_FOLDER);
        ensureFolder(BENCHMARK_FOLDER + "/" + variantFolderName(variant));

        int totalGenerated = 0;

        for (int b = 0; b < NUM_BANDS; b++) {
            if (!reqs[b].enabled) continue;

            const Band& band = bands[b];
            int startIdx = existingCount(variant, band);

            state.addLog("--- Band: " + band.label
                + " (" + std::to_string(band.minCount)
                + "-" + std::to_string(band.maxCount)
                + (variant == Variant::Classic ? " givens" : " clues")
                + ") | existing: " + std::to_string(startIdx)
                + " | generating: " + std::to_string(reqs[b].count) + " ---");

            int n = generateBand(band, variant, reqs[b].count, startIdx, state);
            totalGenerated += n;
            state.generated.store(state.generated.load() + n);

            state.addLog("Band " + band.label + " done: +"
                + std::to_string(n) + " new  (total in folder: "
                + std::to_string(startIdx + n) + ")");
        }

        state.addLog("=== Generation complete: +"
            + std::to_string(totalGenerated) + " new puzzles ===");
        state.addLog("Used seed: " + std::to_string(masterSeed));
        state.running.store(false);
        }).detach();
}

int BenchmarkRunner::generateBand(const Band& band, Variant variant, int count,
    int startIndex, BenchmarkState& state) {
    ensureFolder(BENCHMARK_FOLDER + "/" + variantFolderName(variant) + "/" + band.label);

    int generated = 0;
    int attempts = 0;
    const int maxAttempts = count * 100 * band.attemptsMultiplier;

    while (generated < count && attempts < maxAttempts) {
        attempts++;

        int solution[81] = {};
        if (!gen.createSolution(solution)) continue;

        bool given[81] = {};
        std::vector<KillerData>  cages;
        std::vector<ArrowData>   arrows;
        std::vector<ThermoData>  thermos;

        if (variant == Variant::Classic) {
            for (int k = 0; k < 81; k++) given[k] = true;
            if (!buildClassicPuzzle(solution, given, band.minCount, band.maxCount))
                continue;
        }
        else {
            int clueCount = std::uniform_int_distribution<int>(
                band.minCount, band.maxCount)(clueRng);
            if (!buildVariantPuzzle(solution, variant, clueCount, given,
                cages, arrows, thermos))
                continue;
        }

        // Build the grid from givens only (no constraints attached yet)
        Grid puzzle;
        bool ok = true;
        for (int k = 0; k < 81; k++) {
            if (!given[k]) continue;
            if (!puzzle.assignFull(k, solution[k])) { ok = false; break; }
            puzzle.cells[k].isGiven = true;
        }
        for (int k = 0; k < 81; k++) {
            if (!given[k] && puzzle.cells[k].value != 0) {
                puzzle.cells[k].value = 0;
                puzzle.cells[k].candidates = 0x1FF;
                puzzle.cells[k].isGiven = false;
            }
        }
        if (!ok) continue;

        // Attach constraints
        for (const auto& c : cages)   puzzle.addConstraint(std::make_shared<KillerConstraint>(c));
        for (const auto& a : arrows)  puzzle.addConstraint(std::make_shared<ArrowConstraint>(a));
        for (const auto& t : thermos) puzzle.addConstraint(std::make_shared<ThermoConstraint>(t));

        // Classic puzzles are already fully verified unique
        // Variants are here    
        if (variant != Variant::Classic && Solver::countSolutions(puzzle, 2) != 1)
            continue;

        if (buildAndSave(solution, given, variant, band, startIndex + generated,
            cages, arrows, thermos)) {
            generated++;
            if (generated % 10 == 0 || generated == count) {
                state.addLog("  [" + variantFolderName(variant) + "/" + band.label + "] +"
                    + std::to_string(generated) + "/" + std::to_string(count)
                    + "  attempts: " + std::to_string(attempts));
            }
        }
    }

    if (generated < count) {
        state.addLog("  WARNING: [" + variantFolderName(variant) + "/" + band.label
            + "] only generated " + std::to_string(generated) + "/" + std::to_string(count)
            + " after " + std::to_string(attempts) + " attempts");
    }

    return generated;
}

// Classic: blind clue removal down into [minGivens, maxGivens],
bool BenchmarkRunner::buildClassicPuzzle(const int solution[81], bool given[81],
    int minGivens, int maxGivens) {

    std::vector<int> positions;
    for (int k = 0; k < 81; k++) positions.push_back(k);
    std::shuffle(positions.begin(), positions.end(), posRng);

    int givenCount = 81;

    for (int k : positions) {
        if (givenCount <= minGivens) break;

        given[k] = false;

        Grid attempt;
        bool ok = true;
        for (int j = 0; j < 81; j++) {
            if (!given[j]) continue;
            if (!attempt.assignFull(j, solution[j])) { ok = false; break; }
            attempt.cells[j].isGiven = true;
        }

        if (!ok) { given[k] = true; continue; }
        bool accept = (Solver::countSolutions(attempt, 2) == 1);

        if (accept) givenCount--;
        else given[k] = true;
    }

    return (givenCount >= minGivens && givenCount <= maxGivens);
}

// Variants: fixed 15 ordinary givens and a variant clue from band
bool BenchmarkRunner::buildVariantPuzzle(const int solution[81], Variant variant,
    int clueCount, bool given[81],
    std::vector<KillerData>& cages, std::vector<ArrowData>& arrows,
    std::vector<ThermoData>& thermos) {

    cages.clear(); arrows.clear(); thermos.clear();
    for (int k = 0; k < 81; k++) given[k] = false;

    if (variant == Variant::Killer) {
        cages = gen.buildKillerCages(solution, clueCount);
        if ((int)cages.size() != clueCount) return false;
    }
    else if (variant == Variant::Arrow) {
        arrows = gen.buildArrows(solution, clueCount);
        if ((int)arrows.size() != clueCount) return false;
    }
    else if (variant == Variant::Thermo) {
        thermos = gen.buildThermos(solution, clueCount);
        if ((int)thermos.size() != clueCount) return false;
    }
    else {
        return false; // Classic goes through buildClassicPuzzle instead
    }

    std::vector<int> positions;
    for (int k = 0; k < 81; k++) positions.push_back(k);
    std::shuffle(positions.begin(), positions.end(), posRng);
    for (int i = 0; i < VARIANT_ORDINARY_GIVENS; i++)
        given[positions[i]] = true;

    return true;
}

// Solving benchmark
void BenchmarkRunner::runSolvingBenchmark(BenchmarkState& state, Variant variant) {
    reseed(masterSeed);
    
    state.running.store(true);
    state.generated.store(0);

    const Band* bands = bandsForVariant(variant);
    std::string variantFolder = BENCHMARK_FOLDER + "/" + variantFolderName(variant);

    // Collect all puzzle files from all band subfolders for this variant
    std::vector<std::string> allFiles;
    std::error_code ec;
    for (int b = 0; b < NUM_BANDS; b++) {
        std::string folder = variantFolder + "/" + bands[b].label;
        for (const auto& entry :
            std::filesystem::directory_iterator(folder, ec)) {
            if (entry.path().extension() == ".txt")
                allFiles.push_back(entry.path().string());
        }
    }
    std::sort(allFiles.begin(), allFiles.end());
    state.total.store((int)allFiles.size());

    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.log.clear();
        state.log.push_back("Using seed: " + std::to_string(masterSeed));
        state.log.push_back("Starting " + variantFolderName(variant) + " benchmark on "
            + std::to_string(allFiles.size()) + " puzzles...");
    }

    std::thread([this, allFiles, variant, &state]() {
        ensureFolder(RESULTS_FOLDER);

        std::vector<BenchmarkResult> results;
        results.reserve(allFiles.size());

        int done = 0;
        for (const auto& path : allFiles) {
            Grid grid;
            std::string band;
            int givens = 0, clues = 0;
            std::vector<KillerData>  cages;
            std::vector<ArrowData>   arrows;
            std::vector<ThermoData>  thermos;

            if (!loadPuzzle(path, grid, band, givens, clues, cages, arrows, thermos)) {
                state.addLog("  SKIP (load failed): " + path);
                continue;
            }

            for (const auto& c : cages)   grid.addConstraint(std::make_shared<KillerConstraint>(c));
            for (const auto& a : arrows)  grid.addConstraint(std::make_shared<ArrowConstraint>(a));
            for (const auto& t : thermos) grid.addConstraint(std::make_shared<ThermoConstraint>(t));

            BenchmarkResult r;
            r.variant = variantResultsSuffix(variant);
            r.band = band;
            r.givens = givens;
            r.clues = clues;
            r.puzzleFile = std::filesystem::path(path).filename().string();
            
            // Algorithm 1: DFS
            {
                SolveResult sr = Solver::solveBacktracking(grid);
                r.dfsSolved = sr.solved;
                r.dfsBacktracks = sr.backtracks;
                r.dfsMs = sr.solveTimeMs;
            }
            
            // Algorithm 2: CSP
            {
                SolveResult sr = Solver::solveCSP(grid);
                r.cspSolved = sr.solved;
                r.cspGuesses = sr.guesses;
                r.cspMs = sr.solveTimeMs;
            }
            
            if (variant == Variant::Classic) {
                // Algorithm 3: DLX
                SolveResult sr = Solver::solveDLX(grid);
                r.dlxSolved = sr.solved;
                r.dlxCalls = sr.recursiveCalls;
                r.dlxMs = sr.solveTimeMs;
            }
            else {
                // Algorithm 4: CSP-DLX
                SolveResult sr = Solver::solveHybrid(grid);
                r.hybridSolved = sr.solved;
                r.hybridCspNodes = sr.cspNodes;
                r.hybridCspMs = sr.cspPhaseMs;
                r.hybridDlxCalls = sr.dlxRecursiveCalls;
                r.hybridDlxBacktracks = sr.dlxBacktracks;
                r.hybridDlxMs = sr.dlxPhaseMs;
            }
            
            results.push_back(r);
            done++;
            state.generated.store(done);

            if (done % 25 == 0 || done == (int)allFiles.size()) {
                std::string thirdAlgo = (variant == Variant::Classic)
                    ? (" DLX:" + std::to_string(r.dlxCalls))
                    : (" HybridDLX:" + std::to_string(r.hybridDlxCalls));
                state.addLog("  Benchmarked " + std::to_string(done)
                    + " / " + std::to_string(allFiles.size())
                    + "  [" + r.band + " | G:" + std::to_string(givens)
                    + " C:" + std::to_string(clues)
                    + " | DFS:" + std::to_string(r.dfsBacktracks)
                    + " CSP:" + std::to_string(r.cspGuesses)
                    + thirdAlgo + "]");
            }
        }

        saveResultsCSV(variant, results);
        state.addLog("=== Benchmark complete. Results saved to "
            + RESULTS_FOLDER + "/benchmark_results_" + variantResultsSuffix(variant)
            + ".csv ===");
        state.addLog("Used seed: " + std::to_string(masterSeed));
        state.running.store(false);
        }).detach();
}

// Generation benchmark
void BenchmarkRunner::runGenerationBenchmark(BenchmarkState& state, Variant variant) {
    reseed(masterSeed);
    
    const Band* bands = bandsForVariant(variant);

    std::vector<GenAlgo> algos = (variant == Variant::Classic)
        ? std::vector<GenAlgo>{ GenAlgo::DFS, GenAlgo::CSP, GenAlgo::DLX }
    : std::vector<GenAlgo>{ GenAlgo::DFS, GenAlgo::CSP, GenAlgo::Hybrid };

    int total = NUM_BANDS * (int)algos.size() * GENERATION_SLOTS_PER_ALGO;

    state.running.store(true);
    state.generated.store(0);
    state.total.store(total);

    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.log.clear();
        state.log.push_back("Using seed: " + std::to_string(masterSeed));
        state.log.push_back("Starting " + variantFolderName(variant) + " generation benchmark...");
        state.log.push_back("Total slots: " + std::to_string(total)
            + "  (" + std::to_string(NUM_BANDS) + " bands x "
            + std::to_string(algos.size()) + " algorithms x "
            + std::to_string(GENERATION_SLOTS_PER_ALGO) + " slots, "
            + std::to_string((int)(GENERATION_SLOT_TIMEOUT_MS / 1000)) + "s/slot)");
    }

    std::thread([this, variant, bands, algos, &state]() {
        ensureFolder(RESULTS_FOLDER);

        std::vector<GenerationResult> results;
        results.reserve((size_t)NUM_BANDS * algos.size() * GENERATION_SLOTS_PER_ALGO);

        std::mt19937 rng = genRng;
        int done = 0;

        for (int b = 0; b < NUM_BANDS; b++) {
            const Band& band = bands[b];

            for (GenAlgo algo : algos) {
                state.addLog("--- Band: " + band.label + " | Algorithm: "
                    + genAlgoName(algo) + " ---");

                for (int slot = 0; slot < GENERATION_SLOTS_PER_ALGO; slot++) {
                    GenerationResult r;
                    r.variant = variantResultsSuffix(variant);
                    r.band = band.label;
                    r.algorithm = genAlgoName(algo);
                    r.slot = slot;

                    auto slotStart = std::chrono::high_resolution_clock::now();

                    // No attempts cap,30s wall-clock limit for a slot
                    // Elapsed time is checked between attempts
                    while (true) {
                        auto now = std::chrono::high_resolution_clock::now();
                        double elapsed = std::chrono::duration<double, std::milli>(
                            now - slotStart).count();
                        if (elapsed >= GENERATION_SLOT_TIMEOUT_MS) break;

                        r.attempts++;

                        int checks = 0; double checksMs = 0.0; long nodes = 0;
                        bool ok;

                        if (variant == Variant::Classic) {
                            int givens = 0;
                            ok = genClassicAttempt(gen, algo, band, rng,
                                givens, checks, checksMs, nodes);
                            if (ok) r.givens = givens;
                        }
                        else {
                            int clues = 0;
                            ok = genVariantAttempt(gen, algo, variant, band, rng,
                                clues, checks, checksMs, nodes);
                            if (ok) r.clues = clues;
                        }

                        r.uniquenessChecks += checks;
                        r.uniquenessTotalMs += checksMs;
                        r.uniquenessNodes += nodes;

                        if (ok) { r.success = true; break; }
                    }

                    auto slotEnd = std::chrono::high_resolution_clock::now();
                    r.totalTimeMs = std::chrono::duration<double, std::milli>(
                        slotEnd - slotStart).count();

                    results.push_back(r);
                    done++;
                    state.generated.store(done);

                    if ((slot + 1) % 5 == 0 || slot == GENERATION_SLOTS_PER_ALGO - 1) {
                        state.addLog("  [" + band.label + " | " + genAlgoName(algo)
                            + "] slot " + std::to_string(slot + 1) + "/"
                            + std::to_string(GENERATION_SLOTS_PER_ALGO)
                            + (r.success ? "  OK" : "  FAIL (timeout)")
                            + "  attempts:" + std::to_string(r.attempts)
                            + "  checks:" + std::to_string(r.uniquenessChecks)
                            + "  " + std::to_string((int)r.totalTimeMs) + "ms");
                    }
                }
            }
        }

        saveGenerationResultsCSV(variant, results);
        state.addLog("=== Generation benchmark complete. Results saved to "
            + RESULTS_FOLDER + "/generation_results_" + variantResultsSuffix(variant)
            + ".csv ===");
        state.addLog("Used seed: " + std::to_string(masterSeed));
        state.running.store(false);
        }).detach();
}

void BenchmarkRunner::saveGenerationResultsCSV(Variant variant,
    const std::vector<GenerationResult>& results) {

    std::string path = RESULTS_FOLDER + "/generation_results_"
        + variantResultsSuffix(variant) + ".csv";
    std::ofstream f(path);
    if (!f.is_open()) return;

    f << "variant,band,algorithm,slot,success,totalTimeMs,attempts,"
        << "uniquenessChecks,uniquenessTotalMs,uniquenessNodes,givens,clues\n";

    for (const auto& r : results) {
        f << r.variant << ","
            << r.band << ","
            << r.algorithm << ","
            << r.slot << ","
            << (r.success ? 1 : 0) << ","
            << std::fixed << std::setprecision(3) << r.totalTimeMs << ","
            << r.attempts << ","
            << r.uniquenessChecks << ","
            << r.uniquenessTotalMs << ","
            << r.uniquenessNodes << ","
            << r.givens << ","
            << r.clues << "\n";
    }
}

bool BenchmarkRunner::loadPuzzle(const std::string& path, Grid& grid,
    std::string& band, int& givens, int& clues,
    std::vector<KillerData>& cages,
    std::vector<ArrowData>& arrows,
    std::vector<ThermoData>& thermos) {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::string line, puzzle81;
    band = ""; givens = 0; clues = 0;
    cages.clear(); arrows.clear(); thermos.clear();

    while (std::getline(f, line)) {
        if (line.rfind("BAND:", 0) == 0)
            band = line.substr(5);
        else if (line.rfind("GIVENS:", 0) == 0)
            givens = std::stoi(line.substr(7));
        else if (line.rfind("CLUES:", 0) == 0)
            clues = std::stoi(line.substr(6));
        else if (line.rfind("PUZZLE:", 0) == 0)
            puzzle81 = line.substr(7);
        else if (line.rfind("CAGES:", 0) == 0) {
            int n = std::stoi(line.substr(6));
            for (int i = 0; i < n; i++) {
                if (!std::getline(f, line)) break;
                std::istringstream ss(line);
                KillerData cage;
                int cellCount;
                ss >> cage.targetSum >> cellCount;
                cage.cells.resize(cellCount);
                for (int j = 0; j < cellCount; j++) ss >> cage.cells[j];
                cages.push_back(cage);
            }
        }
        else if (line.rfind("ARROWS:", 0) == 0) {
            int n = std::stoi(line.substr(7));
            for (int i = 0; i < n; i++) {
                if (!std::getline(f, line)) break;
                std::istringstream ss(line);
                ArrowData arrow;
                int pathCount;
                ss >> arrow.circleCell >> pathCount;
                arrow.pathCells.resize(pathCount);
                for (int j = 0; j < pathCount; j++) ss >> arrow.pathCells[j];
                arrows.push_back(arrow);
            }
        }
        else if (line.rfind("THERMOS:", 0) == 0) {
            int n = std::stoi(line.substr(8));
            for (int i = 0; i < n; i++) {
                if (!std::getline(f, line)) break;
                std::istringstream ss(line);
                ThermoData thermo;
                int pathCount;
                ss >> thermo.bulbCell >> pathCount;
                thermo.pathCells.resize(pathCount);
                for (int j = 0; j < pathCount; j++) ss >> thermo.pathCells[j];
                thermos.push_back(thermo);
            }
        }
    }

    if (puzzle81.empty()) return false;
    return grid.loadFromString(puzzle81);
}

void BenchmarkRunner::saveResultsCSV(Variant variant,
    const std::vector<BenchmarkResult>& results) {

    std::string path = RESULTS_FOLDER + "/benchmark_results_"
        + variantResultsSuffix(variant) + ".csv";
    std::ofstream f(path);
    if (!f.is_open()) return;

    f << "file,variant,band,givens,clues,"
        << "dfs_backtracks,dfs_ms,dfs_solved,"
        << "csp_guesses,csp_ms,csp_solved,"
        << "dlx_calls,dlx_ms,dlx_solved,"
        << "hybrid_csp_nodes,hybrid_csp_ms,"
        << "hybrid_dlx_calls,hybrid_dlx_backtracks,hybrid_dlx_ms,hybrid_solved\n";

    for (const auto& r : results) {
        f << r.puzzleFile << ","
            << r.variant << ","
            << r.band << ","
            << r.givens << ","
            << r.clues << ","
            << r.dfsBacktracks << ","
            << std::fixed << std::setprecision(3) << r.dfsMs << ","
            << (r.dfsSolved ? 1 : 0) << ","
            << r.cspGuesses << ","
            << r.cspMs << ","
            << (r.cspSolved ? 1 : 0) << ","
            << r.dlxCalls << ","
            << r.dlxMs << ","
            << (r.dlxSolved ? 1 : 0) << ","
            << r.hybridCspNodes << ","
            << r.hybridCspMs << ","
            << r.hybridDlxCalls << ","
            << r.hybridDlxBacktracks << ","
            << r.hybridDlxMs << ","
            << (r.hybridSolved ? 1 : 0) << "\n";
    }
}

bool BenchmarkRunner::buildAndSave(const int solution[81], const bool given[81],
    Variant variant, const Band& band, int index,
    const std::vector<KillerData>& cages,
    const std::vector<ArrowData>& arrows,
    const std::vector<ThermoData>& thermos) {

    std::string puzzle81(81, '0');
    std::string solution81(81, '0');
    int givenCount = 0;

    for (int k = 0; k < 81; k++) {
        solution81[k] = '0' + solution[k];
        if (given[k]) { puzzle81[k] = '0' + solution[k]; givenCount++; }
    }

    int clueCount = (int)(cages.size() + arrows.size() + thermos.size());

    // Save into: Benchmark/<Variant>/<band>/<band>_042.txt
    std::ostringstream fname;
    fname << BENCHMARK_FOLDER << "/" << variantFolderName(variant) << "/"
        << band.label << "/" << band.label << "_"
        << variantFolderName(variant) << "_"
        << std::setfill('0') << std::setw(3) << index << ".txt";

    std::ofstream f(fname.str());
    if (!f.is_open()) return false;

    f << "VARIANT:" << variantResultsSuffix(variant) << "\n";
    f << "BAND:" << band.label << "\n";
    f << "GIVENS:" << givenCount << "\n";
    f << "CLUES:" << clueCount << "\n";
    f << "PUZZLE:" << puzzle81 << "\n";
    f << "SOLUTION:" << solution81 << "\n";

    if (!cages.empty()) {
        f << "CAGES:" << cages.size() << "\n";
        for (const auto& cage : cages) {
            f << cage.targetSum << " " << cage.cells.size();
            for (int c : cage.cells) f << " " << c;
            f << "\n";
        }
    }

    if (!arrows.empty()) {
        f << "ARROWS:" << arrows.size() << "\n";
        for (const auto& arrow : arrows) {
            f << arrow.circleCell << " " << arrow.pathCells.size();
            for (int c : arrow.pathCells) f << " " << c;
            f << "\n";
        }
    }

    if (!thermos.empty()) {
        f << "THERMOS:" << thermos.size() << "\n";
        for (const auto& thermo : thermos) {
            f << thermo.bulbCell << " " << thermo.pathCells.size();
            for (int c : thermo.pathCells) f << " " << c;
            f << "\n";
        }
    }

    return true;
}
