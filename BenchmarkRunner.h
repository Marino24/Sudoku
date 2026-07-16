#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <random>
#include <filesystem>
#include "Grid.h"
#include "Generator.h"
#include "Solver.h"
#include "KillerData.h"
#include "ArrowData.h"
#include "ThermoData.h"

struct Band {
    int         minCount;
    int         maxCount;
    std::string label;
    int         attemptsMultiplier;
};

static const Band CLASSIC_BANDS[] = {
    { 21, 21, "4_extreme", 16 },
    { 22, 22, "3_hard", 8 },
    { 23, 23, "2_medium", 4 },
    { 24, 24, "1_easy", 2 }
};

static const Band KILLER_BANDS[] = {
    { 9,  9,  "4_extreme", 16 },
    { 10,  10,  "3_hard", 8 },
    { 11,  11,  "2_medium", 4 },
    { 12, 12, "1_easy", 2 }
};

static const Band ARROW_BANDS[] = {
    { 7,  7,  "4_extreme", 16 },
    { 8,  8,  "3_hard", 8 },
    { 9,  9,  "2_medium", 4 },
    { 10, 10, "1_easy", 2 }
};

static const Band THERMO_BANDS[] = {
    { 10,  10,  "4_extreme", 16 },
    { 11,  11,  "3_hard", 8 },
    { 12,  12,  "2_medium", 4 },
    { 13, 13, "1_easy", 2 }
};

static const int NUM_BANDS = 4;
static const int VARIANT_ORDINARY_GIVENS = 15;
static const std::string BENCHMARK_FOLDER = "Benchmark";
static const std::string RESULTS_FOLDER = "Results";

// Generation benchmark
// 
// For each band of the selected variant, and for each of that
// variant's applicable algorithms try to build a puzzle
// one per "slot". Each slot repeatedly samples a fresh construction 
// (solution -> givens/clues) and uniqueness-checks it with that slot's algorithm, 
// until either a unique puzzle is found or GENERATION_SLOT_TIMEOUT_MS
// puzzles are discarded

static const int    GENERATION_SLOTS_PER_ALGO = 25;
static const double GENERATION_SLOT_TIMEOUT_MS = 30000.0;


struct GenerationResult {
    std::string variant;
    std::string band;
    std::string algorithm;
    int         slot = 0;

    bool        success = false;   
    double      totalTimeMs = 0.0; 
    int         attempts = 0;      // full solution+clue+given samples tried

    // uniquenessChecks == attempts for variants (one check per sample)
    int         uniquenessChecks = 0;
    double      uniquenessTotalMs = 0.0; // summed time inside uniqueness-check calls only
    long        uniquenessNodes = 0;     // summed engine-appropriate effort

    int         givens = 0;
    int         clues = 0;
};

inline const Band* bandsForVariant(Variant v) {
    switch (v) {
    case Variant::Killer: return KILLER_BANDS;
    case Variant::Arrow:  return ARROW_BANDS;
    case Variant::Thermo: return THERMO_BANDS;
    default:              return CLASSIC_BANDS;
    }
}

inline std::string variantFolderName(Variant v) {
    switch (v) {
    case Variant::Killer: return "Killer";
    case Variant::Arrow:  return "Arrow";
    case Variant::Thermo: return "Thermo";
    default:              return "Classic";
    }
}

inline std::string variantResultsSuffix(Variant v) {
    switch (v) {
    case Variant::Killer: return "killer";
    case Variant::Arrow:  return "arrow";
    case Variant::Thermo: return "thermo";
    default:              return "classic";
    }
}

struct BenchmarkState {
    std::atomic<bool> running{ false };
    std::atomic<int>  generated{ 0 };
    std::atomic<int>  total{ 0 };

    std::mutex               mutex;
    std::vector<std::string> log;

    void addLog(const std::string& msg) {
        std::lock_guard<std::mutex> lock(mutex);
        log.push_back(msg);
    }
};

struct BandRequest {
    bool enabled = true;
    int  count = 100;
};

struct BenchmarkResult {
    std::string variant;      
    std::string band;
    int         givens = 0;   // ordinary givens
    int         clues = 0;    // variant clue count (0 for classic)
    std::string puzzleFile;

    // Algorithm 1: DFS (all variants)
    int         dfsBacktracks = 0;
    double      dfsMs = 0.0;
    bool        dfsSolved = false;

    // Algorithm 2: CSP (all variants)
    int         cspGuesses = 0;
    double      cspMs = 0.0;
    bool        cspSolved = false;

    // Algorithm 3: DLX (classic only)
    int         dlxCalls = 0;
    double      dlxMs = 0.0;
    bool        dlxSolved = false;

    // Algorithm 4: Hybrid CSP-DLX (variants only) 
    // phases kept separate so each phase's contribution is visible.
    int         hybridCspNodes = 0;
    double      hybridCspMs = 0.0;
    int         hybridDlxCalls = 0;
    int         hybridDlxBacktracks = 0;
    double      hybridDlxMs = 0.0;
    bool        hybridSolved = false;
};

class BenchmarkRunner {
public:

    explicit BenchmarkRunner(unsigned int masterSeed = std::random_device{}()); 
    unsigned int seed() const { return masterSeed; }
    void reseed(unsigned int s);
    void verifyPuzzleSet(BenchmarkState& state, Variant variant); 


    // Solving benchmark, generate a puzzle set for one variant (async)
    void generatePuzzleSet(BenchmarkState& state, Variant variant,
        const BandRequest requests[NUM_BANDS]);

    // Solving benchmark, solve all puzzles for one variant (async)
    void runSolvingBenchmark(BenchmarkState& state, Variant variant);

    // Generation benchmark (async)
    void runGenerationBenchmark(BenchmarkState& state, Variant variant);

private:
    unsigned int masterSeed;
    Generator    gen;
    std::mt19937 clueRng, posRng, genRng;

    // Generation helpers
    int  generateBand(const Band& band, Variant variant, int count,
        int startIndex, BenchmarkState& state);
    bool buildClassicPuzzle(const int solution[81], bool given[81],
        int minGivens, int maxGivens);
    bool buildVariantPuzzle(const int solution[81], Variant variant,
        int clueCount, bool given[81],
        std::vector<KillerData>& cages, std::vector<ArrowData>& arrows,
        std::vector<ThermoData>& thermos);
    bool buildAndSave(const int solution[81], const bool given[81],
        Variant variant, const Band& band, int index,
        const std::vector<KillerData>& cages,
        const std::vector<ArrowData>& arrows,
        const std::vector<ThermoData>& thermos);
    int  existingCount(Variant variant, const Band& band);

    // Benchmarking helpers
    bool loadPuzzle(const std::string& path, Grid& grid,
        std::string& band, int& givens, int& clues,
        std::vector<KillerData>& cages,
        std::vector<ArrowData>& arrows,
        std::vector<ThermoData>& thermos);
    void saveResultsCSV(Variant variant,
        const std::vector<BenchmarkResult>& results);

    // Generation-benchmark helper
    void saveGenerationResultsCSV(Variant variant,
        const std::vector<GenerationResult>& results);

    static void ensureFolder(const std::string& folder);
};