#define IMGUI_DEFINE_MATH_OPERATORS
#include "UI.h"
#include "BenchmarkRunner.h"
#include "KillerConstraint.h"
#include "ArrowConstraint.h"
#include "ThermoConstraint.h"
#include <imgui/imgui.h>
#include <vector>
#include <algorithm>
#include <cmath>

static void DrawDottedLine(ImDrawList* dl, ImVec2 p1, ImVec2 p2,
    ImU32 color, float thickness = 1.5f,
    float dashLen = 4.0f, float gapLen = 3.0f) {
    float dx = p2.x - p1.x, dy = p2.y - p1.y;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) return;
    dx /= len; dy /= len;

    float t = 0.0f;
    bool  drawing = true;
    while (t < len) {
        float seg = drawing ? dashLen : gapLen;
        float end = std::min(t + seg, len);
        if (drawing)
            dl->AddLine({ p1.x + dx * t, p1.y + dy * t },
                { p1.x + dx * end, p1.y + dy * end },
                color, thickness);
        t = end;
        drawing = !drawing;
    }
}

static void RenderArrowConstraints(ImDrawList* dl, ImVec2 pos,
    float cellSize, const Grid& g) {
    auto cellCenter = [&](int k) -> ImVec2 {
        return { pos.x + (k % 9) * cellSize + cellSize * 0.5f,
                 pos.y + (k / 9) * cellSize + cellSize * 0.5f };
        };

    for (const auto& cp : g.constraints) {
        auto* ac = dynamic_cast<ArrowConstraint*>(cp.get());
        if (!ac) continue;

        const auto& arrow = ac->arrow;
        ImU32 col = IM_COL32(180, 60, 60, 220);

        // Draw circle at start
        ImVec2 cc = cellCenter(arrow.circleCell);
        float  r = cellSize * 0.32f;
        dl->AddCircle(cc, r, col, 32, 2.0f);

        if (arrow.pathCells.empty()) continue;

        ImVec2 next = cellCenter(arrow.pathCells[0]);

        ImVec2 dir = ImVec2(next.x - cc.x, next.y - cc.y);
        float leng = sqrtf(dir.x * dir.x + dir.y * dir.y);

        if (leng > 0.0f) {
            dir.x /= leng;
            dir.y /= leng;
        }

        ImVec2 prev = ImVec2(
            cc.x + dir.x * r,
            cc.y + dir.y * r
        );

        for (int i = 0; i < (int)arrow.pathCells.size(); i++) {
            ImVec2 next = cellCenter(arrow.pathCells[i]);
            dl->AddLine(prev, next, col, 2.0f);
            prev = next;
        }

        // Arrowhead at tip
        ImVec2 tip = cellCenter(arrow.pathCells.back());
        ImVec2 from = arrow.pathCells.size() >= 2
            ? cellCenter(arrow.pathCells[arrow.pathCells.size() - 2])
            : cc;

        float dx = tip.x - from.x, dy = tip.y - from.y;
        float len = sqrtf(dx * dx + dy * dy);
        if (len > 0.001f) {
            dx /= len; dy /= len;
            float px = -dy, py = dx;
            float sz = 8.0f;
            ImVec2 p1{ tip.x - dx * sz + px * sz * 0.5f, tip.y - dy * sz + py * sz * 0.5f };
            ImVec2 p2{ tip.x - dx * sz - px * sz * 0.5f, tip.y - dy * sz - py * sz * 0.5f };
            dl->AddTriangleFilled(tip, p1, p2, col);
        }
    }
}

static void RenderThermoConstraints(ImDrawList* dl, ImVec2 pos,
    float cellSize, const Grid& g) {
    auto cellCenter = [&](int k) -> ImVec2 {
        return { pos.x + (k % 9) * cellSize + cellSize * 0.5f,
                 pos.y + (k / 9) * cellSize + cellSize * 0.5f };
        };

    for (const auto& cp : g.constraints) {
        auto* tc = dynamic_cast<ThermoConstraint*>(cp.get());
        if (!tc) continue;

        const auto& thermo = tc->thermo;
        ImU32  col = IM_COL32(234, 214, 55, 220);
        float  tubeR = cellSize * 0.13f;

        // Build full sequence: bulb + path
        std::vector<int> seq = { thermo.bulbCell };
        for (int k : thermo.pathCells) seq.push_back(k);

        // Draw tube as thick rounded segments
        ImVec2 prev = cellCenter(seq[0]);
        for (int i = 1; i < (int)seq.size(); i++) {
            ImVec2 next = cellCenter(seq[i]);

            float dx = next.x - prev.x, dy = next.y - prev.y;
            float len = sqrtf(dx * dx + dy * dy);
            if (len > 0.001f) {
                float nx = dx / len, ny = dy / len;
                float px = -ny * tubeR, py = nx * tubeR;
                ImVec2 p1{ prev.x + px, prev.y + py };
                ImVec2 p2{ prev.x - px, prev.y - py };
                ImVec2 p3{ next.x - px, next.y - py };
                ImVec2 p4{ next.x + px, next.y + py };
                dl->AddQuadFilled(p1, p2, p3, p4, col);
            }
            // Rounded join
            dl->AddCircleFilled(prev, tubeR, col, 16);
            dl->AddCircleFilled(next, tubeR, col, 16);

            prev = next;
        }

        // Filled bulb (larger circle) at bulb cell
        ImVec2 bulbCenter = cellCenter(thermo.bulbCell);
        dl->AddCircleFilled(bulbCenter, cellSize * 0.28f, col, 32);
    }
}

static void RenderKillerConstraints(ImDrawList* dl, ImVec2 pos,
    float cellSize, const Grid& g) {
    // Collect all KillerConstraint instances from the grid
    for (const auto& cp : g.constraints) {
        auto* kc = dynamic_cast<KillerConstraint*>(cp.get());
        if (!kc) continue;

        const auto& cage = kc->cage;

        // Build a set for fast neighbour lookup
        std::vector<bool> inCage(81, false);
        for (int k : cage.cells) inCage[k] = true;

        // Draw dotted borders on cage edges
        for (int k : cage.cells) {
            int r = k / 9, c = k % 9;
            ImVec2 p1(pos.x + c * cellSize, pos.y + r * cellSize);
            ImVec2 p2(p1.x + cellSize, p1.y + cellSize);

            // Inset slightly
            const float in = 2.0f;
            ImU32 col = IM_COL32(60, 60, 200, 220);

            // Top edge
            if (r == 0 || !inCage[(r - 1) * 9 + c])
                DrawDottedLine(dl, { p1.x + in, p1.y + in }, { p2.x - in, p1.y + in }, col);
            // Bottom edge
            if (r == 8 || !inCage[(r + 1) * 9 + c])
                DrawDottedLine(dl, { p1.x + in, p2.y - in }, { p2.x - in, p2.y - in }, col);
            // Left edge
            if (c == 0 || !inCage[r * 9 + (c - 1)])
                DrawDottedLine(dl, { p1.x + in, p1.y + in }, { p1.x + in, p2.y - in }, col);
            // Right edge
            if (c == 8 || !inCage[r * 9 + (c + 1)])
                DrawDottedLine(dl, { p2.x - in, p1.y + in }, { p2.x - in, p2.y - in }, col);
        }

        // Draw sum label in top-left cell
        int topLeft = cage.cells[0];
        for (int k : cage.cells) if (k < topLeft) topLeft = k;

        int tr = topLeft / 9, tc = topLeft % 9;
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", cage.targetSum);

        // Small offset from top-left corner
        ImVec2 labelPos(pos.x + tc * cellSize + 3.0f,
            pos.y + tr * cellSize + 2.0f);

        // Tiny shadow then label
        dl->AddText(ImGui::GetFont(), 11.0f,
            { labelPos.x + 1, labelPos.y + 1 },
            IM_COL32(255, 255, 255, 180), buf);
        dl->AddText(ImGui::GetFont(), 11.0f,
            labelPos,
            IM_COL32(60, 60, 200, 255), buf);
    }
}

void RenderSudokuGrid(UIState& ui) {
    const float cellSize = 56.0f;
    const float gridSize = cellSize * 9;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2      pos = ImGui::GetCursorScreenPos();

    // White background
    dl->AddRectFilled(pos,
        ImVec2(pos.x + gridSize, pos.y + gridSize),
        IM_COL32(255, 255, 255, 255));

    const Grid& g = ui.displayGrid;

    // Cell backgrounds
    for (int k = 0; k < 81; k++) {
        int    r = k / 9, c = k % 9;
        ImVec2 p1(pos.x + c * cellSize, pos.y + r * cellSize);
        ImVec2 p2(p1.x + cellSize, p1.y + cellSize);

        // Background
        ImU32 bg = IM_COL32(255, 255, 255, 255);
        if (k == ui.selectedCell)
            bg = IM_COL32(180, 210, 255, 255);
        else if (g.cells[k].isGiven)
            bg = IM_COL32(220, 220, 220, 255);

        dl->AddRectFilled(p1, p2, bg);
    }

    // Render variant constraints (under borders and numbers)
    if (g.isVariant()) {
        RenderKillerConstraints(dl, pos, cellSize, g);
        RenderArrowConstraints(dl, pos, cellSize, g);
        RenderThermoConstraints(dl, pos, cellSize, g);
    }

    // Cell numbers
    for (int k = 0; k < 81; k++) {
        int    r = k / 9, c = k % 9;
        ImVec2 p1(pos.x + c * cellSize, pos.y + r * cellSize);
        ImVec2 p2(p1.x + cellSize, p1.y + cellSize);

        // Number
        if (g.cells[k].value != 0) {
            char   buf[2] = { char('0' + g.cells[k].value), '\0' };
            ImVec2 ts = ImGui::CalcTextSize(buf);
            ImVec2 tp(p1.x + (cellSize - ts.x) * 0.5f,
                p1.y + (cellSize - ts.y) * 0.5f);
            ImU32 col = g.cells[k].isGiven
                ? IM_COL32(0, 0, 0, 255)
                : IM_COL32(30, 100, 200, 255);
            dl->AddText(tp, col, buf);
        }
    }


    // Thin cell borders
    for (int i = 0; i <= 9; i++) {
        float x = pos.x + i * cellSize;
        float y = pos.y + i * cellSize;
        dl->AddLine({ x, pos.y }, { x, pos.y + gridSize }, IM_COL32(180, 180, 180, 255), 1.0f);
        dl->AddLine({ pos.x, y }, { pos.x + gridSize, y }, IM_COL32(180, 180, 180, 255), 1.0f);
    }

    // Thick 3x3 borders
    for (int i = 0; i <= 9; i += 3) {
        float x = pos.x + i * cellSize;
        float y = pos.y + i * cellSize;
        dl->AddLine({ x, pos.y }, { x, pos.y + gridSize }, IM_COL32(0, 0, 0, 255), 3.0f);
        dl->AddLine({ pos.x, y }, { pos.x + gridSize, y }, IM_COL32(0, 0, 0, 255), 3.0f);
    }

    // Mouse click
    ImGui::InvisibleButton("grid", ImVec2(gridSize, gridSize));
    if (ImGui::IsItemClicked()) {
        ImVec2 mp = ImGui::GetMousePos();
        int c = (int)((mp.x - pos.x) / cellSize);
        int r = (int)((mp.y - pos.y) / cellSize);
        if (r >= 0 && r < 9 && c >= 0 && c < 9)
            ui.selectedCell = r * 9 + c;
    }
}

void RenderControls(UIState& ui) {
    ImGui::SeparatorText("Solve");

    auto doSolve = [&](SolveResult sr) {
        if (sr.solved) {
            for (int k = 0; k < 81; k++) {
                ui.displayGrid.cells[k].value = sr.solvedGrid.cells[k].value;
                ui.displayGrid.cells[k].candidates = sr.solvedGrid.cells[k].candidates;
            }
            char msg[256];
            snprintf(msg, sizeof(msg),
                "Solved!  Guesses:%d  Backtracks:%d  Depth:%d  %.1fms",
                sr.guesses, sr.backtracks, sr.maxDepth, sr.solveTimeMs);
            ui.statusMsg = msg;
        }
        else {
            ui.statusMsg = "No solution found.";
        }
        };
    if (ImGui::Button("Solve DFS", ImVec2(90, 0)))
        doSolve(Solver::solveBacktracking(ui.displayGrid));
    ImGui::SameLine();
    if (ImGui::Button("Solve CSP", ImVec2(90, 0)))
        doSolve(Solver::solveCSP(ui.displayGrid));
    ImGui::SameLine();
    if (ImGui::Button("Solve DLX", ImVec2(90, 0)))
        doSolve(Solver::solveDLX(ui.displayGrid));
    if (ImGui::Button("Solve Hybrid DLX", ImVec2(90, 0)))
        doSolve(Solver::solveHybrid(ui.displayGrid));

    // Status line
    if (!ui.statusMsg.empty()) {
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f),
            "%s", ui.statusMsg.c_str());
    }

    // Benchmark section
    ImGui::SeparatorText("Benchmark");
    static BenchmarkRunner benchRunner;

    static unsigned int seedInput= 0;
    ImGui::SetNextItemWidth(140);
    ImGui::InputScalar("Seed", ImGuiDataType_U32, &seedInput);
    ImGui::SameLine();
    if (ImGui::Button("Set seed", ImVec2(200, 0))
        && !ui.benchState.running.load()) {
        benchRunner.reseed(seedInput);
        ui.statusMsg = "Seed set to " + std::to_string(benchRunner.seed());
    }
    ImGui::TextDisabled("Current seed: %u", benchRunner.seed());

    ImGui::SetNextItemWidth(140);
    const char* variantNames[] = { "Classic", "Killer", "Arrow", "Thermo" };
    int variantIdx = (int)ui.benchVariant;
    if (ImGui::Combo("Variant", &variantIdx, variantNames, 4))
        ui.benchVariant = (Variant)variantIdx;

    static BandRequest bandRequests[NUM_BANDS] = {
        { true, 100 },  // extreme
        { true, 100 },  // hard
        { true, 100 },  // medium
        { true, 100 }   // easy
    };

    const Band* activeBands = bandsForVariant(ui.benchVariant);
    for (int b = 0; b < NUM_BANDS; b++) {
        const auto& band = activeBands[b];
        ImGui::PushID(b);

        ImGui::Checkbox(band.label.c_str(), &bandRequests[b].enabled);
        if (bandRequests[b].enabled) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70);
            ImGui::InputInt("##count", &bandRequests[b].count);
            bandRequests[b].count = std::max(1, bandRequests[b].count);
            ImGui::SameLine();
            ImGui::TextDisabled("(%d-%d %s, x%d attempts)",
                band.minCount, band.maxCount,
                ui.benchVariant == Variant::Classic ? "givens" : "clues",
                band.attemptsMultiplier);
        }

        ImGui::PopID();
    }

    ImGui::Spacing();
    if (ImGui::Button("Generate Puzzle Set", ImVec2(200, 0))
        && !ui.benchState.running.load()) {
        benchRunner.generatePuzzleSet(ui.benchState, ui.benchVariant, bandRequests);
        ui.benchState.addLog("Using seed: " + std::to_string(benchRunner.seed()));
    }
    ImGui::SameLine();
    
    if (ImGui::Button("Verify solutions", ImVec2(200, 0))
        && !ui.benchState.running.load()) {
        benchRunner.verifyPuzzleSet(ui.benchState, ui.benchVariant);
    }
    if (ImGui::Button("Run Solving Benchmark", ImVec2(200, 0))
        && !ui.benchState.running.load()) {
        benchRunner.runSolvingBenchmark(ui.benchState, ui.benchVariant);
        ui.benchState.addLog("Using seed: " + std::to_string(benchRunner.seed()));
    }
    ImGui::SameLine();
    if (ImGui::Button("Run Generation Benchmark", ImVec2(200, 0))
        && !ui.benchState.running.load()) {
        benchRunner.runGenerationBenchmark(ui.benchState, ui.benchVariant);
        ui.benchState.addLog("Using seed: " + std::to_string(benchRunner.seed()));
    }

    if (ui.benchState.running.load()) {
        float progress = ui.benchState.total.load() > 0
            ? (float)ui.benchState.generated.load() / ui.benchState.total.load()
            : 0.0f;
        ImGui::ProgressBar(progress, ImVec2(-1, 0));
        ImGui::Text("%d / %d", ui.benchState.generated.load(),
            ui.benchState.total.load());
    }

    // Log output
    {
        std::lock_guard<std::mutex> lock(ui.benchState.mutex);
        if (!ui.benchState.log.empty()) {
            ImGui::BeginChild("##benchlog", ImVec2(-1, 120), true,
                ImGuiWindowFlags_HorizontalScrollbar);
            for (const auto& line : ui.benchState.log)
                ImGui::TextUnformatted(line.c_str());
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();
        }
    }

    // Puzzle browser
    ImGui::SeparatorText("Load Puzzle");

    if (ImGui::BeginCombo("##files",
        ui.selectedFile.empty() ? "Select puzzle..." : ui.selectedFile.c_str()))
    {
        if (ui.puzzleFiles.empty())
            ui.puzzleFiles = getFilesInFolder("Puzzles");

        for (const auto& f : ui.puzzleFiles) {
            bool sel = (f == ui.selectedFile);
            if (ImGui::Selectable(f.c_str(), sel))
                ui.selectedFile = f;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::Button("Refresh##files")) {
        ui.puzzleFiles = getFilesInFolder("Puzzles");
    }

    if (ImGui::Button("Load", ImVec2(80, 0))) {
        if (!ui.selectedFile.empty()) {
            std::string path = "Puzzles/" + ui.selectedFile;
            Grid loaded;
            if (loadPuzzleFromFile(path, loaded)) {
                ui.displayGrid = loaded;
                ui.selectedCell = -1;
                ui.statusMsg = "Loaded: " + ui.selectedFile;
            }
            else {
                ui.statusMsg = "Failed to load: " + ui.selectedFile;
            }
        }
    }
}