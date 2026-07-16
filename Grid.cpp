#include "Grid.h"
#include "Constraint.h"
#include <iostream>
#include <cstring>

int Grid::peers[81][20];
int Grid::groups[27][9];
int Grid::cellGroups[81][3];

void Grid::init() {
    for (int r = 0; r < 9; r++)
        for (int c = 0; c < 9; c++) {
            int k = idx(r, c);
            groups[r][c] = k;
            groups[9 + c][r] = k;
            groups[18 + (r / 3) * 3 + c / 3][(r % 3) * 3 + c % 3] = k;
        }

    for (int k = 0; k < 81; k++) {
        int r = row(k), c = col(k);
        cellGroups[k][0] = r;
        cellGroups[k][1] = 9 + c;
        cellGroups[k][2] = 18 + (r / 3) * 3 + (c / 3);

        int pi = 0;
        bool seen[81] = {};
        seen[k] = true;
        for (int g = 0; g < 3; g++) {
            int grp = cellGroups[k][g];
            for (int j = 0; j < 9; j++) {
                int p = groups[grp][j];
                if (!seen[p]) { seen[p] = true; peers[k][pi++] = p; }
            }
        }
    }
}

Grid::Grid() { clear(); }

void Grid::addConstraint(std::shared_ptr<Constraint> c) {
    constraints.push_back(std::move(c));
}

void Grid::clear() {
    for (int k = 0; k < 81; k++) {
        cells[k].value = 0;
        cells[k].candidates = 0x1FF;
        cells[k].isGiven = false;
    }
    constraints.clear();
}

// Does NOT cascade or check hidden singles
bool Grid::assign(int k, int val) {
    for (int d = 1; d <= 9; d++)
        if (d != val && !eliminate(k, d)) return false;

    for (auto& c : constraints)
        if (!c->onAssign(*this, k, val)) return false;

    return true;
}

bool Grid::eliminate(int k, int val) {
    if (!cells[k].hasCandidate(val)) return true;

    cells[k].removeCandidate(val);
    int n = cells[k].candidateCount();

    if (n == 0) return false;

    if (n == 1)
        cells[k].value = cells[k].singleCandidate();

    return true;
}

// Cascades naked singles to peers and checks hidden singles in groups
bool Grid::assignFull(int k, int val) {
    for (int d = 1; d <= 9; d++)
        if (d != val && !eliminateFull(k, d)) return false;

    for (auto& c : constraints)
        if (!c->onAssign(*this, k, val)) return false;

    return true;
}

bool Grid::eliminateFull(int k, int val) {
    if (!cells[k].hasCandidate(val)) return true;

    cells[k].removeCandidate(val);
    int n = cells[k].candidateCount();

    if (n == 0) return false;

    // Edini kandidat
    if (n == 1) {
        int v = cells[k].singleCandidate();
        cells[k].value = v;
        for (int i = 0; i < 20; i++)
            if (!eliminateFull(peers[k][i], v)) return false;
    }

    // Edino mesto
    for (int g = 0; g < 3; g++) {
        int grp = cellGroups[k][g];
        int count = 0, last = -1;
        for (int j = 0; j < 9; j++) {
            int p = groups[grp][j];
            if (cells[p].hasCandidate(val)) { count++; last = p; }
        }
        if (count == 0) return false;
        if (count == 1)
            if (!assignFull(last, val)) return false;
    }

    return true;
}
bool Grid::isSolved() const {
    for (int k = 0; k < 81; k++)
        if (cells[k].candidateCount() != 1) return false;
    return true;
}

int Grid::leastCount() const {
    int best = -1, bestN = 10;
    for (int k = 0; k < 81; k++) {
        if (cells[k].value != 0) continue;
        int n = cells[k].candidateCount();
        if (n > 1 && n < bestN) { bestN = n; best = k; }
    }
    return best;
}

bool Grid::loadFromString(const std::string& s) {
    clear();
    int k = 0;
    for (char ch : s) {
        if (k >= 81) break;
        if (ch >= '1' && ch <= '9') {
            int d = ch - '0';
            cells[k].value = d;
            cells[k].candidates = (1 << (d - 1));
            cells[k].isGiven = true;
            k++;
        }
        else if (ch == '0' || ch == '.') {
            k++;
        }
    }
    return true;
}

std::string Grid::toString() const {
    std::string s(81, '0');
    for (int k = 0; k < 81; k++)
        if (cells[k].isGiven && cells[k].value != 0)
            s[k] = '0' + cells[k].value;
    return s;
}

void Grid::print() const {
    for (int r = 0; r < 9; r++) {
        if (r == 3 || r == 6) std::cout << "------+-------+------\n";
        for (int c = 0; c < 9; c++) {
            if (c == 3 || c == 6) std::cout << "| ";
            int v = cells[idx(r, c)].value;
            std::cout << (v ? char('0' + v) : '.') << " ";
        }
        std::cout << "\n";
    }
}