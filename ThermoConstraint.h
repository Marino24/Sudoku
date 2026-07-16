#pragma once
#include "Constraint.h"
#include "ThermoData.h"
#include "Grid.h"
#include <vector>

struct ThermoConstraint : public Constraint {
    ThermoData thermo;

    explicit ThermoConstraint(const ThermoData& t) : thermo(t) {}

    bool onAssign(Grid& g, int /*k*/, int /*val*/) override {
        std::vector<int> seq;
        seq.push_back(thermo.bulbCell);
        for (int idx : thermo.pathCells) seq.push_back(idx);

        // Check each consecutive filled pair
        for (int i = 0; i + 1 < (int)seq.size(); i++) {
            int v1 = g.cells[seq[i]].value;
            int v2 = g.cells[seq[i + 1]].value;
            if (v1 == 0 || v2 == 0) continue; // skip unfilled pairs
            if (v2 <= v1) return false;        // must strictly increase
        }

        // Positional bounds: cell at position i (0-based) must be >= i+1
        // and <= 9 - (seqLen - 1 - i)
        int seqLen = (int)seq.size();
        for (int i = 0; i < seqLen; i++) {
            int v = g.cells[seq[i]].value;
            if (v == 0) continue;
            int minVal = i + 1;
            int maxVal = 9 - (seqLen - 1 - i);
            if (v < minVal || v > maxVal) return false;
        }

        return true;
    }

    bool isSatisfied(const Grid& g) const override {
        std::vector<int> seq;
        seq.push_back(thermo.bulbCell);
        for (int idx : thermo.pathCells) seq.push_back(idx);

        for (int i = 0; i + 1 < (int)seq.size(); i++) {
            int v1 = g.cells[seq[i]].value;
            int v2 = g.cells[seq[i + 1]].value;
            if (v1 == 0 || v2 == 0) return false;
            if (v2 <= v1) return false;
        }
        return true;
    }

    // Candidate-level pruning:
    //   1) static positional bounds (same formula as onAssign), applied
    //      per-cell instead of only checked once a cell is filled
    //   2) forward pass: each cell's minimum viable value must exceed
    //      the previous cell's minimum viable value
    //   3) backward pass: each cell's maximum viable value must be
    //      below the next cell's maximum viable value
    // (2)/(3) tighten beyond the static bounds using whatever's
    // already been deduced about neighbours, and can cascade a single
    // forced value down/up the thermometer.
    bool prune(Grid& g) override {
        std::vector<int> seq;
        seq.push_back(thermo.bulbCell);
        for (int idx : thermo.pathCells) seq.push_back(idx);
        int n = (int)seq.size();

        // (1) Static positional bounds
        for (int i = 0; i < n; i++) {
            int cellIdx = seq[i];
            if (g.cells[cellIdx].value != 0) continue;
            int minVal = i + 1;
            int maxVal = 9 - (n - 1 - i);
            for (int d = 1; d <= 9; d++) {
                if ((d < minVal || d > maxVal) && g.cells[cellIdx].hasCandidate(d))
                    g.cells[cellIdx].removeCandidate(d);
            }
            if (g.cells[cellIdx].candidateCount() == 0) return false;
        }
        
        // Pregled naprej, naraščajoča spodnja meja
        int prevMin = 0;
        for (int i = 0; i < n; i++) {
            int cellIdx = seq[i];
            int cMin = cellMinCandidate(g, cellIdx);
            if (cMin <= prevMin) {
                for (int d = 1; d <= prevMin; d++)
                    if (g.cells[cellIdx].hasCandidate(d)) g.cells[cellIdx].removeCandidate(d);
                if (g.cells[cellIdx].candidateCount() == 0) return false;
                cMin = cellMinCandidate(g, cellIdx);
            }
            prevMin = cMin;
        }

        // Pregled nazaj, padajoča zgornja meja
        int nextMax = 10;
        for (int i = n - 1; i >= 0; i--) {
            int cellIdx = seq[i];
            int cMax = cellMaxCandidate(g, cellIdx);
            if (cMax >= nextMax) {
                for (int d = nextMax; d <= 9; d++)
                    if (g.cells[cellIdx].hasCandidate(d)) g.cells[cellIdx].removeCandidate(d);
                if (g.cells[cellIdx].candidateCount() == 0) return false;
                cMax = cellMaxCandidate(g, cellIdx);
            }
            nextMax = cMax;
        }

        return true;
    }

    bool involves(int cell) const override {
        if (cell == thermo.bulbCell) return true;
        for (int c : thermo.pathCells) if (c == cell) return true;
        return false;
    }

private:
    static int cellMinCandidate(const Grid& g, int cellIdx) {
        if (g.cells[cellIdx].value != 0) return g.cells[cellIdx].value;
        for (int d = 1; d <= 9; d++)
            if (g.cells[cellIdx].hasCandidate(d)) return d;
        return 10;
    }
    static int cellMaxCandidate(const Grid& g, int cellIdx) {
        if (g.cells[cellIdx].value != 0) return g.cells[cellIdx].value;
        for (int d = 9; d >= 1; d--)
            if (g.cells[cellIdx].hasCandidate(d)) return d;
        return 0;
    }
};