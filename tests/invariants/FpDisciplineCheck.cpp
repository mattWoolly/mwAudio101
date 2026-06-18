// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/invariants/FpDisciplineCheck.cpp — fp_discipline_guard (task 012).
//
// Realizes docs/design/11 sec 13.4 and ADR-014 C5 / ADR-013 C20. Greps
// compile_commands.json for forbidden FP flags (-ffast-math, -Ofast, /fp:fast,
// -ffp-contract=fast) on every golden/DSP translation unit and FAILS if any
// appears — fast-math poisoning is caught mechanically, not by review. As a paired
// positive control it also asserts the DSP TUs actually carry the discipline flags
// (so a clean DSP target passes AND a stripped one would fail).
//
// Invoked as: FpDisciplineCheck <path-to-compile_commands.json>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

const std::vector<std::string> kForbidden = {
    "-ffast-math", "-Ofast", "/fp:fast", "-ffp-contract=fast"
};

// A TU is "DSP/golden" if its compiled file lives under core/ or tests/ (the
// targets that link mw_fp_discipline). We scan those for both forbidden flags and
// proof-of-discipline.
bool isDspOrGoldenTu(const std::string& file) {
    return file.find("/core/") != std::string::npos
        || file.find("/tests/") != std::string::npos;
}

// Crude-but-sufficient extraction of the "file" and "command"/"arguments" strings
// from each compile_commands.json entry without a JSON dependency.
std::string jsonStringField(const std::string& entry, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    auto k = entry.find(needle);
    if (k == std::string::npos) return {};
    auto colon = entry.find(':', k + needle.size());
    if (colon == std::string::npos) return {};
    auto q1 = entry.find('"', colon);
    if (q1 == std::string::npos) return {};
    // Read until the closing unescaped quote.
    std::string out;
    for (std::size_t i = q1 + 1; i < entry.size(); ++i) {
        if (entry[i] == '\\' && i + 1 < entry.size()) { out += entry[i + 1]; ++i; continue; }
        if (entry[i] == '"') break;
        out += entry[i];
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "fp_discipline_guard: usage: FpDisciplineCheck <compile_commands.json>\n";
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::cerr << "fp_discipline_guard: cannot open " << argv[1] << "\n";
        return 2;
    }
    std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    // Split into top-level objects on "}," / "}]" boundaries (good enough for the
    // flat array CMake emits).
    std::vector<std::string> entries;
    std::size_t depth = 0, start = std::string::npos;
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (all[i] == '{') { if (depth == 0) start = i; ++depth; }
        else if (all[i] == '}') { if (depth > 0) { --depth; if (depth == 0 && start != std::string::npos) entries.push_back(all.substr(start, i - start + 1)); } }
    }

    int dspTus = 0;
    int withDiscipline = 0;
    std::vector<std::string> poisoned;

    for (const auto& e : entries) {
        const std::string file = jsonStringField(e, "file");
        if (file.empty() || !isDspOrGoldenTu(file)) continue;
        ++dspTus;

        // The command may be in "command" (string) or "arguments" (array). Scan the
        // whole entry text — flags appear verbatim either way.
        for (const auto& bad : kForbidden) {
            if (e.find(bad) != std::string::npos) {
                poisoned.push_back(file + "  [" + bad + "]");
            }
        }
        // Proof of discipline: the FROZEN set includes -ffp-contract=off.
        if (e.find("-ffp-contract=off") != std::string::npos || e.find("/fp:contract-") != std::string::npos) {
            ++withDiscipline;
        }
    }

    // Silent-pass guard: a guard that scanned no DSP TUs is mis-wired — fail.
    if (dspTus == 0) {
        std::cerr << "fp_discipline_guard: FAIL — scanned 0 DSP/golden TUs (mis-wired?)\n";
        return 3;
    }

    if (!poisoned.empty()) {
        std::cerr << "fp_discipline_guard: FAIL — forbidden FP flag on " << poisoned.size()
                  << " DSP/golden TU(s):\n";
        for (const auto& p : poisoned) std::cerr << "  " << p << "\n";
        return 1;
    }

    // Positive control: at least one DSP TU must actually carry the discipline flag,
    // proving mw_fp_discipline is reaching the targets (a stripped target fails here).
    if (withDiscipline == 0) {
        std::cerr << "fp_discipline_guard: FAIL — no DSP/golden TU carries -ffp-contract=off; "
                     "mw_fp_discipline is not reaching the DSP targets.\n";
        return 4;
    }

    std::cout << "fp_discipline_guard: PASS — " << dspTus << " DSP/golden TU(s) scanned, "
              << "0 forbidden flags, " << withDiscipline << " carry -ffp-contract=off.\n";
    return 0;
}
