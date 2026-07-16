#include "DLX.h"
#include <algorithm>
#include <cassert>

DLX::DLX() : header(nullptr) {}

DLX::~DLX() { cleanup(); }

void DLX::cleanup() {
    for (DLXNode* n : allNodes) delete n;
    allNodes.clear();
    colHeaders.clear();
    solution.clear();
    header = nullptr;
}

DLXNode* DLX::newNode() {
    DLXNode* n = new DLXNode();
    n->left = n->right = n->up = n->down = n->col = nullptr;
    n->rowID = -1;
    n->size = 0;
    allNodes.push_back(n);
    return n;
}

void DLX::build(const Grid& grid) {
    cleanup();

    // Header node
    header = newNode();
    header->left = header->right = header;

    // 324 column headers, link horizontally
    colHeaders.resize(NUM_COLS);
    DLXNode* prev = header;
    for (int i = 0; i < NUM_COLS; i++) {
        DLXNode* ch = newNode();
        ch->size = 0;
        ch->col = ch;
        ch->up = ch;
        ch->down = ch;

        ch->left = prev;
        ch->right = header;
        prev->right = ch;
        header->left = ch;

        colHeaders[i] = ch;
        prev = ch;
    }

    // Add one row per valid placement (cell, digit)
    for (int cell = 0; cell < 81; cell++) {
        int r = cell / 9;
        int c = cell % 9;
        int b = (r / 3) * 3 + (c / 3);

        int fixedVal = grid.cells[cell].value;

        for (int d = 1; d <= 9; d++) {
            // If cell is already given, only allow that digit
            if (fixedVal != 0 && d != fixedVal) continue;

            // If cell has candidates set
            // skip digits not in candidate set
            if (fixedVal == 0 && !grid.cells[cell].hasCandidate(d)) continue;

            int rowID = cell * 9 + (d - 1);

            // Four columns this placement satisfies
            int cols[4] = {
                cellCol(cell),
                rowCol(r, d),
                colCol(c, d),
                boxCol(b, d)
            };

            // Create 4 nodes for this row, link horizontally
            DLXNode* first = nullptr;
            DLXNode* rowPrev = nullptr;
            for (int i = 0; i < 4; i++) {
                DLXNode* n = newNode();
                n->rowID = rowID;
                n->col = colHeaders[cols[i]];

                // Vertical link: insert at bottom of column
                DLXNode* colH = colHeaders[cols[i]];
                n->down = colH;
                n->up = colH->up;
                colH->up->down = n;
                colH->up = n;
                colH->size++;

                // Horizontal link
                if (!first) {
                    first = n;
                    n->left = n;
                    n->right = n;
                }
                else {
                    n->left = rowPrev;
                    n->right = first;
                    rowPrev->right = n;
                    first->left = n;
                }
                rowPrev = n;
            }
        }
    }
}

void DLX::cover(DLXNode* col) {
    // Odstrani stolpec iz seznama glave
    col->right->left = col->left;
    col->left->right = col->right;

    // Odstrani vse vrstice, ki imajo vozlišče v tem stolpcu
    for (DLXNode* row = col->down; row != col; row = row->down) {
        for (DLXNode* node = row->right; node != row; node = node->right) {
            node->down->up = node->up;
            node->up->down = node->down;
            node->col->size--;
        }
    }
}

void DLX::uncover(DLXNode* col) {
    // Restore rows in reverse order
    for (DLXNode* row = col->up; row != col; row = row->up) {
        for (DLXNode* node = row->left; node != row; node = node->left) {
            node->col->size++;
            node->down->up = node;
            node->up->down = node;
        }
    }
    // Restore column to header list
    col->right->left = col;
    col->left->right = col;
}


// S heuristic
DLXNode* DLX::chooseCol() {
    DLXNode* best = nullptr;
    int minSize = INT_MAX;
    for (DLXNode* c = header->right; c != header; c = c->right) {
        if (c->size < minSize) {
            minSize = c->size;
            best = c;
            if (minSize == 0) break; // Can't do better
        }
    }
    return best;
}

// Algorithm X 
bool DLX::search(int depth, SolveResult& sr) {
    sr.recursiveCalls++;
    sr.maxDepth = std::max(sr.maxDepth, depth);

    // All columns covered = solution found
    if (header->right == header) return true;

    DLXNode* col = chooseCol();
    if (!col || col->size == 0) return false; // No valid placement

    cover(col);

    for (DLXNode* row = col->down; row != col; row = row->down) {
        sr.guesses++;

        // Select this row
        solution.push_back(row);

        // Cover all other columns in this row
        for (DLXNode* node = row->right; node != row; node = node->right)
            cover(node->col);

        // Variant-constraint gate (no-op for plain classic solving,
        // where onPlaceCheck is left unset).
        int cell = -1, digit = -1;
        bool constraintOk = true;
        if (onPlaceCheck || onUnplace) decode(row->rowID, cell, digit);
        if (onPlaceCheck) constraintOk = onPlaceCheck(cell, digit);

        if (constraintOk && search(depth + 1, sr)) return true;

        // Backtrack
        sr.backtracks++;
        solution.pop_back();

        if (onUnplace) onUnplace(cell, digit);

        for (DLXNode* node = row->left; node != row; node = node->left)
            uncover(node->col);
    }

    uncover(col);
    return false;
}

void DLX::searchCount(int depth, int limit, int& count, SolveResult& sr) {
    if (count >= limit) return;

    sr.recursiveCalls++;
    sr.maxDepth = std::max(sr.maxDepth, depth);

    // All columns covered = one full solution found
    if (header->right == header) { count++; return; }

    DLXNode* col = chooseCol();
    if (!col || col->size == 0) return; // No valid placement

    cover(col);

    for (DLXNode* row = col->down; row != col && count < limit; row = row->down) {
        sr.guesses++;

        solution.push_back(row);

        for (DLXNode* node = row->right; node != row; node = node->right)
            cover(node->col);

        int cell = -1, digit = -1;
        bool constraintOk = true;
        if (onPlaceCheck || onUnplace) decode(row->rowID, cell, digit);
        if (onPlaceCheck) constraintOk = onPlaceCheck(cell, digit);

        if (constraintOk) searchCount(depth + 1, limit, count, sr);

        sr.backtracks++;
        solution.pop_back();

        if (onUnplace) onUnplace(cell, digit);

        for (DLXNode* node = row->left; node != row; node = node->left)
            uncover(node->col);
    }

    uncover(col);
}

int DLX::countSolutions(int limit, SolveResult& sr) {
    solution.clear();
    int count = 0;
    searchCount(0, limit, count, sr);
    return count;
}

bool DLX::solve(Grid& grid, SolveResult& sr) {
    solution.clear();
    if (!search(0, sr)) return false;

    // Decode solution into grid
    for (DLXNode* row : solution) {
        int cell, digit;
        decode(row->rowID, cell, digit);
        grid.cells[cell].value = digit;
        grid.cells[cell].setOnlyCandidate(digit);
    }

    return true;
}

void DLX::decode(int rowID, int& cell, int& digit) {
    cell = rowID / 9;
    digit = rowID % 9 + 1;
}