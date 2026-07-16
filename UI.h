#pragma once
#include "Grid.h"
#include "Generator.h"
#include "Solver.h"
#include "BenchmarkRunner.h"
#include "KillerData.h"
#include "KillerConstraint.h"
#include "ArrowData.h"
#include "ArrowConstraint.h"
#include "ThermoData.h"
#include "ThermoConstraint.h"
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>

inline bool loadPuzzleFromFile(const std::string& path, Grid& grid) {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::string line;
    bool gridLoaded = false;

    while (std::getline(f, line)) {
        if (line.rfind("PUZZLE:", 0) == 0) {
            if (!grid.loadFromString(line.substr(7))) return false;
            gridLoaded = true;
        }
        else if (line.rfind("CAGES:", 0) == 0) {
            int n = std::stoi(line.substr(6));
            for (int i = 0; i < n; i++) {
                if (!std::getline(f, line)) break;
                std::istringstream ss(line);
                KillerData cage;
                int cellCount;
                ss >> cage.targetSum >> cellCount;
                cage.cells.resize(cellCount);
                for (int j = 0; j < cellCount; j++) ss >> cage.cells[j];
                grid.addConstraint(std::make_shared<KillerConstraint>(cage));
            }
        }
        else if (line.rfind("ARROWS:", 0) == 0) {
            int n = std::stoi(line.substr(7));
            for (int i = 0; i < n; i++) {
                if (!std::getline(f, line)) break;
                std::istringstream ss(line);
                ArrowData arrow;
                int pathCount;
                ss >> arrow.circleCell >> pathCount;
                arrow.pathCells.resize(pathCount);
                for (int j = 0; j < pathCount; j++) ss >> arrow.pathCells[j];
                grid.addConstraint(std::make_shared<ArrowConstraint>(arrow));
            }
        }
        else if (line.rfind("THERMOS:", 0) == 0) {
            int n = std::stoi(line.substr(8));
            for (int i = 0; i < n; i++) {
                if (!std::getline(f, line)) break;
                std::istringstream ss(line);
                ThermoData thermo;
                int pathCount;
                ss >> thermo.bulbCell >> pathCount;
                thermo.pathCells.resize(pathCount);
                for (int j = 0; j < pathCount; j++) ss >> thermo.pathCells[j];
                grid.addConstraint(std::make_shared<ThermoConstraint>(thermo));
            }
        }
    }

    return gridLoaded;
}

inline std::vector<std::string> getFilesInFolder(const std::string& folder) {
    std::vector<std::string> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec))
        if (entry.path().extension() == ".txt")
            files.push_back(entry.path().filename().string());
    std::sort(files.begin(), files.end());
    return files;
}

struct UIState {
    Variant benchVariant = Variant::Classic;

    std::vector<std::string> puzzleFiles;
    std::string selectedFile;
    std::string statusMsg;

    Grid displayGrid;
    int  selectedCell = -1;

    BenchmarkState benchState;
};

void RenderSudokuGrid(UIState& ui);
void RenderControls(UIState& ui);