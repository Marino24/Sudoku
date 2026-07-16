#pragma once
#include "Constraint.h"
#include "ArrowData.h"
#include "Grid.h"
#include <algorithm>

struct ArrowConstraint : public Constraint {
    ArrowData arrow;

    explicit ArrowConstraint(const ArrowData& a) : arrow(a) {}

    bool onAssign(Grid& g, int /*k*/, int /*val*/) override {
        int circleVal = g.cells[arrow.circleCell].value;

        int pathSum = 0;
        int emptyCount = 0;
        for (int idx : arrow.pathCells) {
            int v = g.cells[idx].value;
            if (v == 0) emptyCount++;
            else        pathSum += v;
        }

        if (circleVal != 0) {
            if (pathSum > circleVal) return false;

            // Path complete
            if (emptyCount == 0 && pathSum != circleVal) return false;

            // Must be able to reach target
            int remaining = circleVal - pathSum;
            if (remaining < emptyCount)     return false; // min is all 1s
            if (remaining > emptyCount * 9) return false; // max is all 9s
        }

        return true;
    }

    bool isSatisfied(const Grid& g) const override {
        int circleVal = g.cells[arrow.circleCell].value;
        if (circleVal == 0) return false;

        int sum = 0;
        for (int idx : arrow.pathCells) {
            if (g.cells[idx].value == 0) return false;
            sum += g.cells[idx].value;
        }
        return sum == circleVal;
    }

    // Candidate-level pruning. Two cases:
    //   - circle already assigned: identical bound math to
    //     KillerConstraint::prune, treating the path cells as a cage
    //     whose target is the circle's value.
    //   - circle still unknown: bound the circle's own candidates by
    //     the achievable path-sum range, then bound each empty path
    //     cell's candidates against the circle's current candidate
    //     RANGE (rather than a fixed target), keeping a digit only if
    //     some circle value in range could still make the total work.
    bool prune(Grid& g) override {
        int circleVal = g.cells[arrow.circleCell].value;

        int filledSum = 0;
        std::vector<int> emptyPath;
        for (int idx : arrow.pathCells) {
            int v = g.cells[idx].value;
            if (v == 0) emptyPath.push_back(idx);
            else        filledSum += v;
        }
        int n = (int)emptyPath.size();

        if (circleVal != 0) {
            int remaining = circleVal - filledSum;
            if (n == 0) return remaining == 0;
            if (remaining < n || remaining > n * 9) return false;

            int otherCount = n - 1;
            for (int cellIdx : emptyPath) {
                for (int d = 1; d <= 9; d++) {
                    if (!g.cells[cellIdx].hasCandidate(d)) continue;
                    int rest = remaining - d;
                    if (rest < otherCount || rest > otherCount * 9)
                        g.cells[cellIdx].removeCandidate(d);
                }
                if (g.cells[cellIdx].candidateCount() == 0) return false;
            }
            return true;
        }

        // Nepoznana vrednost kroga: omejimo jo z dosegljivim razponom vsote poti
        int minPathSum = filledSum + n * 1;
        int maxPathSum = filledSum + n * 9;
        for (int d = 1; d <= 9; d++) {
            if (g.cells[arrow.circleCell].hasCandidate(d) &&
                (d < minPathSum || d > maxPathSum)) {
                g.cells[arrow.circleCell].removeCandidate(d);
            }
        }
        if (g.cells[arrow.circleCell].candidateCount() == 0) return false;

        int circleMin = 10, circleMax = 0;
        for (int d = 1; d <= 9; d++) {
            if (g.cells[arrow.circleCell].hasCandidate(d)) {
                circleMin = circleMin < d ? circleMin : d;
                circleMax = circleMax > d ? circleMax : d;
            }
        }

        int otherCount = n - 1;
        for (int cellIdx : emptyPath) {
            for (int d = 1; d <= 9; d++) {
                if (!g.cells[cellIdx].hasCandidate(d)) continue;
                int minNeeded = filledSum + d + otherCount * 1;
                int maxNeeded = filledSum + d + otherCount * 9;
                // Keep d only if [minNeeded,maxNeeded] overlaps the circle achievable range [circleMin,circleMax]
                if (minNeeded > circleMax || maxNeeded < circleMin)
                    g.cells[cellIdx].removeCandidate(d);
            }
            if (g.cells[cellIdx].candidateCount() == 0) return false;
        }

        return true;
    }

    bool involves(int cell) const override {
        if (cell == arrow.circleCell) return true;
        for (int c : arrow.pathCells) if (c == cell) return true;
        return false;
    }
};