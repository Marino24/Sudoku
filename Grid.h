#pragma once
#include "Cell.h"
#include "Constraint.h"
#include <string>
#include <vector>
#include <memory>

// idx(r,c) = r*9 + c
inline int idx(int r, int c) { return r * 9 + c; }
inline int row(int k) { return k / 9; }
inline int col(int k) { return k % 9; }

class Grid {
public:
    Cell cells[81];

    // Precomputed topology
    static int peers[81][20];
    static int groups[27][9];
    static int cellGroups[81][3];
    static void init();

    Grid();

    void clear();

    bool assign(int k, int val);
    bool eliminate(int k, int val);      // bare: removes candidate only
    bool eliminateFull(int k, int val);  // full: cascades naked + hidden singles
    bool assignFull(int k, int val);     // assign using eliminateFull

    bool isSolved() const;
    int  leastCount() const;

    bool        loadFromString(const std::string& s);
    std::string toString() const;
    void        print() const;

    std::vector<std::shared_ptr<Constraint>> constraints;
    void addConstraint(std::shared_ptr<Constraint> c);
    bool isVariant() const { return !constraints.empty(); }
};