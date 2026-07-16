#pragma once
#include "Grid.h"
#include "KillerData.h"
#include "ArrowData.h"
#include "ThermoData.h"
#include <vector>
#include <string>
#include <random>
#include <algorithm>

enum class Variant { Classic, Killer, Arrow, Thermo };

class Generator {
public:
    Generator();
    void setSeed(unsigned int seed);

    bool createSolution(int solution[81]);

    std::vector<KillerData>  buildKillerCages(const int solution[81], int numCages);
    std::vector<ArrowData>   buildArrows(const int solution[81], int numArrows);
    std::vector<ThermoData>  buildThermos(const int solution[81], int numThermos);

private:
    std::mt19937 rng;

    bool fillRecursive(int solution[81], int k);
    bool isValidPlacement(const int solution[81], int k, int d);

    template<typename T>
    void shuffleArray(std::vector<T>& v) {
        std::shuffle(v.begin(), v.end(), rng);
    }
};