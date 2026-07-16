#include "Generator.h"
#include <algorithm>
#include <ctime>

Generator::Generator() : rng(std::time(nullptr)) {}
void Generator::setSeed(unsigned int seed) { rng.seed(seed); }

// ===================================================================
// Solution Generation
// ===================================================================

bool Generator::createSolution(int solution[81]) {
    for (int k = 0; k < 81; k++) solution[k] = 0;

    for (int box = 0; box < 3; box++) {
        std::vector<int> digits = { 1,2,3,4,5,6,7,8,9 };
        shuffleArray(digits);
        int sr = box * 3, sc = box * 3, i = 0;
        for (int r = sr; r < sr + 3; r++)
            for (int c = sc; c < sc + 3; c++)
                solution[r * 9 + c] = digits[i++];
    }

    return fillRecursive(solution, 0);
}

bool Generator::fillRecursive(int solution[81], int k) {
    if (k == 81) return true;
    if (solution[k] != 0) return fillRecursive(solution, k + 1);

    std::vector<int> digits = { 1,2,3,4,5,6,7,8,9 };
    shuffleArray(digits);

    for (int d : digits) {
        if (isValidPlacement(solution, k, d)) {
            solution[k] = d;
            if (fillRecursive(solution, k + 1)) return true;
            solution[k] = 0;
        }
    }
    return false;
}

bool Generator::isValidPlacement(const int solution[81], int k, int d) {
    int r = k / 9, c = k % 9;
    for (int j = 0; j < 9; j++)
        if (solution[r * 9 + j] == d || solution[j * 9 + c] == d) return false;
    int br = (r / 3) * 3, bc = (c / 3) * 3;
    for (int dr = 0; dr < 3; dr++)
        for (int dc = 0; dc < 3; dc++)
            if (solution[(br + dr) * 9 + (bc + dc)] == d) return false;
    return true;
}

//  Killer
std::vector<KillerData> Generator::buildKillerCages(const int solution[81],
    int numCages) {
    std::vector<KillerData> cages;
    bool used[81] = {};
    int  attempts = 0;

    while ((int)cages.size() < numCages && attempts < numCages * 20) {
        attempts++;

        int start = std::uniform_int_distribution<int>(0, 80)(rng);
        if (used[start]) continue;

        int targetSize = std::uniform_int_distribution<int>(2, 4)(rng);
        std::vector<int> cageCells = { start };
        int digitMask = 1 << (solution[start] - 1);   // digits already in cage

        for (int i = 1; i < targetSize; i++) {
            std::vector<int> neighbours;
            for (int c : cageCells) {
                int r = c / 9, col = c % 9;
                int dr[] = { -1, 1,  0, 0 };
                int dc[] = { 0, 0, -1, 1 };
                for (int d = 0; d < 4; d++) {
                    int nr = r + dr[d], nc = col + dc[d];
                    if (nr < 0 || nr > 8 || nc < 0 || nc > 8) continue;
                    int nk = nr * 9 + nc;
                    if (used[nk]) continue;
                    if (std::find(cageCells.begin(), cageCells.end(), nk)
                        != cageCells.end()) continue;
                    // Killer rule: digits in a cage must be distinct
                    if (digitMask & (1 << (solution[nk] - 1))) continue;
                    neighbours.push_back(nk);
                }
            }
            if (neighbours.empty()) break;

            int chosen = neighbours[std::uniform_int_distribution<int>
                (0, (int)neighbours.size() - 1)(rng)];
            cageCells.push_back(chosen);
            digitMask |= 1 << (solution[chosen] - 1);
        }

        if ((int)cageCells.size() < 2) continue;

        // Mark cells as used only for accepted cages
        int sum = 0;
        KillerData cage;
        for (int c : cageCells) {
            used[c] = true;
            sum += solution[c];
            cage.cells.push_back(c);
        }
        cage.targetSum = sum;
        cages.push_back(cage);
    }

    return cages;
}

// Arrow
std::vector<ArrowData> Generator::buildArrows(const int solution[81],
    int numArrows) {
    std::vector<ArrowData> arrows;
    bool used[81] = {};
    int  attempts = 0;

    while ((int)arrows.size() < numArrows && attempts < numArrows * 30) {
        attempts++;

        int start = std::uniform_int_distribution<int>(0, 80)(rng);
        if (used[start]) continue;

        int circleVal = solution[start];
        ArrowData arrow;
        arrow.circleCell = start;

        int r = start / 9, c = start % 9;
        int pathSum = 0;
        int pathLen = 2 + std::uniform_int_distribution<int>(0, 2)(rng);

        std::vector<int> visited = { start };

        for (int step = 0; step < pathLen; step++) {
            std::vector<int> dirs = { 0,1,2,3 };
            std::shuffle(dirs.begin(), dirs.end(), rng);

            int dr[] = { -1, 1,  0, 0 };
            int dc[] = { 0, 0, -1, 1 };
            bool moved = false;

            for (int d : dirs) {
                int nr = r + dr[d], nc = c + dc[d];
                if (nr < 0 || nr > 8 || nc < 0 || nc > 8) continue;
                int nk = nr * 9 + nc;
                if (used[nk]) continue;
                if (std::find(visited.begin(), visited.end(), nk) != visited.end()) continue;
                if (pathSum + solution[nk] > circleVal) continue;

                r = nr; c = nc;
                pathSum += solution[nk];
                arrow.pathCells.push_back(nk);
                visited.push_back(nk);
                moved = true;
                break;
            }
            if (!moved) break;
        }

        if ((int)arrow.pathCells.size() < 2 || pathSum != circleVal) continue;

        used[start] = true;
        for (int k : arrow.pathCells) used[k] = true;
        arrows.push_back(arrow);
    }

    return arrows;
}

// Thermo
std::vector<ThermoData> Generator::buildThermos(const int solution[81],
    int numThermos) {
    std::vector<ThermoData> thermos;
    bool used[81] = {};
    int  attempts = 0;

    while ((int)thermos.size() < numThermos && attempts < numThermos * 30) {
        attempts++;

        int start = std::uniform_int_distribution<int>(0, 80)(rng);
        if (used[start]) continue;

        ThermoData thermo;
        thermo.bulbCell = start;

        int r = start / 9, c = start % 9;
        int lastVal = solution[start];
        int pathLen = 2 + std::uniform_int_distribution<int>(0, 3)(rng);

        std::vector<int> visited = { start };

        for (int step = 0; step < pathLen; step++) {
            std::vector<int> dirs = { 0,1,2,3 };
            std::shuffle(dirs.begin(), dirs.end(), rng);

            int dr[] = { -1, 1,  0, 0 };
            int dc[] = { 0, 0, -1, 1 };
            bool moved = false;

            for (int d : dirs) {
                int nr = r + dr[d], nc = c + dc[d];
                if (nr < 0 || nr > 8 || nc < 0 || nc > 8) continue;
                int nk = nr * 9 + nc;
                if (used[nk]) continue;
                if (std::find(visited.begin(), visited.end(), nk) != visited.end()) continue;
                if (solution[nk] <= lastVal) continue;

                r = nr; c = nc;
                lastVal = solution[nk];
                thermo.pathCells.push_back(nk);
                visited.push_back(nk);
                moved = true;
                break;
            }
            if (!moved) break;
        }

        if ((int)thermo.pathCells.size() < 2) continue;

        used[start] = true;
        for (int k : thermo.pathCells) used[k] = true;
        thermos.push_back(thermo);
    }

    return thermos;
}