#pragma once
#include "Grid.h"
#include "Solver.h"
#include <vector>
#include <array>
#include <functional>

struct DLXNode {
    DLXNode* left;
    DLXNode* right;
    DLXNode* up;
    DLXNode* down;
    DLXNode* col;       
    int      rowID;     // encodes (cell, digit): cell*9 + (digit-1)
    int      size;      // only valid for column headers: count of nodes
};

class DLX {
public:
    DLX();
    ~DLX();

    void build(const Grid& grid);

    // Run Algorithm X, fill solution into grid
    // Returns false if no solution exists
    bool solve(Grid& grid, SolveResult& sr);

    // Optional hooks used by Solver::solveHybrid to keep a persistent
    // shadow grid in sync with DLX's search path, 
    // for variants grid updates instead of reconstructing the partial grid from scratch at every node 


    // onPlaceCheck: called immediately after a row (cell,digit) is committed, before recursing further. Return false
    // to reject the row and force DLX to backtrack, exactly as if the chosen column had size 0.
    std::function<bool(int cell, int digit)> onPlaceCheck;
    // onUnplace: called when a previously-committed row is backtracked out of
    // symmetric with onPlaceCheck, so a caller-maintained shadow grid can undo it
    std::function<void(int cell, int digit)> onUnplace;

    int countSolutions(int limit, SolveResult& sr);

    std::vector<std::pair<int, int>> currentAssignments() const {
        std::vector<std::pair<int, int>> out;
        out.reserve(solution.size());
        for (DLXNode* row : solution) {
            int cell, digit;
            decode(row->rowID, cell, digit);
            out.push_back({ cell, digit });
        }
        return out;
    }

private:
    static const int NUM_COLS = 324;

    DLXNode* header;
    std::vector<DLXNode*> colHeaders;   // 324 column headers
    std::vector<DLXNode*> allNodes;     // all allocated nodes

    // Chosen rows during search
    std::vector<DLXNode*> solution;

    DLXNode* newNode();

    void cover(DLXNode* col);
    void uncover(DLXNode* col);
    DLXNode* chooseCol();               // minimum size heuristic (S)

    bool search(int depth, SolveResult& sr);
    void searchCount(int depth, int limit, int& count, SolveResult& sr);

    // Decode rowID back to cell and digit
    static void decode(int rowID, int& cell, int& digit);

    // Column index helpers
    static int cellCol(int cell) { return cell; }
    static int rowCol(int r, int d) { return 81 + r * 9 + (d - 1); }
    static int colCol(int c, int d) { return 162 + c * 9 + (d - 1); }
    static int boxCol(int b, int d) { return 243 + b * 9 + (d - 1); }

    void cleanup();
};