#pragma once
#include <cstdint>

struct Cell {
    int      value = 0;        // 0 = empty, 1-9 = placed
    uint16_t candidates = 0x1FF;    // bits 0-8 represent digits 1-9 (all on by default)
    bool     isGiven = false;

    // Candidate helpers
    bool hasCandidate(int d) const { return (candidates >> (d - 1)) & 1; }
    void removeCandidate(int d) { candidates &= ~(1 << (d - 1)); }
    void setOnlyCandidate(int d) { candidates = (1 << (d - 1)); }
    void clearCandidates() { candidates = 0; }
    int  candidateCount()  const { return __popcnt(candidates); }

    // Return the single candidate value, or -1 if not exactly one
    int  singleCandidate() const {
        if (candidateCount() != 1) return -1;
        for (int d = 1; d <= 9; d++)
            if (hasCandidate(d)) return d;
        return -1;
    }
};