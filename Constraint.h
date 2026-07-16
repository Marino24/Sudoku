#pragma once
class Grid;

struct Constraint {
    virtual ~Constraint() = default;

    virtual bool onAssign(Grid& g, int k, int val) = 0;

    virtual bool isSatisfied(const Grid& g) const = 0;

    virtual bool prune(Grid& g) { (void)g; return true; }

    virtual bool involves(int cell) const = 0;
};