#pragma once
#include "Constraint.h"
#include "KillerData.h"
#include "Grid.h"
#include <vector>

struct KillerConstraint : public Constraint {
    KillerData cage;

    explicit KillerConstraint(const KillerData& c) : cage(c) {}

    bool onAssign(Grid& g, int /*k*/, int /*val*/) override {
        int sum = 0;
        int emptyCount = 0;
        int usedMask = 0;

        for (int cellIdx : cage.cells) {
            int v = g.cells[cellIdx].value;
            if (v == 0) { emptyCount++; continue; }

            // Brez ponavljanja
            if (usedMask & (1 << (v - 1))) return false;
            usedMask |= (1 << (v - 1));
            sum += v;
        }

        if (sum > cage.targetSum)                     return false;
        if (emptyCount == 0 && sum != cage.targetSum) return false;

        int remaining = cage.targetSum - sum;
        if (remaining < emptyCount)     return false;
        if (remaining > emptyCount * 9) return false;

        return true;
    }

    bool isSatisfied(const Grid& g) const override {
        int sum = 0;
        int usedMask = 0;
        for (int cellIdx : cage.cells) {
            if (g.cells[cellIdx].value == 0) return false;
            int v = g.cells[cellIdx].value;
            if (usedMask & (1 << (v - 1))) return false;
            usedMask |= (1 << (v - 1));
            sum += v;
        }
        return sum == cage.targetSum;
    }

    // Candidate-level pruning:
    //   1) strike digits already used elsewhere in the cage from
    //      every remaining empty cell (no-repeat rule)
    //   2) for each remaining candidate d of an empty cell, keep it
    //      only if the OTHER empty cells can still cover the
    //      resulting remainder (same bound math as onAssign's
    //      feasibility check, just applied per-digit instead of only
    //      to the aggregate)
    // When exactly one empty cell remains, (2) alone collapses it to its single forced value.
    bool prune(Grid& g) override {
        int filledSum = 0;
        int usedMask = 0;
        std::vector<int> emptyCells;

        for (int cellIdx : cage.cells) {
            int v = g.cells[cellIdx].value;
            if (v == 0) {
                emptyCells.push_back(cellIdx);
            }
            else {
                filledSum += v;
                usedMask |= (1 << (v - 1));
            }
        }

        int n = (int)emptyCells.size();
        int remaining = cage.targetSum - filledSum;

        if (n == 0) return remaining == 0;
        if (remaining < n || remaining > n * 9) return false;

        // (1) No-repeat pruning
        for (int cellIdx : emptyCells) {
            for (int d = 1; d <= 9; d++) {
                if ((usedMask & (1 << (d - 1))) && g.cells[cellIdx].hasCandidate(d)) {
                    g.cells[cellIdx].removeCandidate(d);
                }
            }
            if (g.cells[cellIdx].candidateCount() == 0) return false;
        }

        // (2) Sum-bound pruning per candidate digit
        int otherCount = n - 1;
        for (int cellIdx : emptyCells) {
            for (int d = 1; d <= 9; d++) {
                if (!g.cells[cellIdx].hasCandidate(d)) continue;
                int rest = remaining - d;
                if (rest < otherCount || rest > otherCount * 9) {
                    g.cells[cellIdx].removeCandidate(d);
                }
            }
            if (g.cells[cellIdx].candidateCount() == 0) return false;
        }

        return true;
    }

    bool involves(int cell) const override {
        for (int c : cage.cells) if (c == cell) return true;
        return false;
    }
};