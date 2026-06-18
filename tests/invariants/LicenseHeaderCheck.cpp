// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/invariants/LicenseHeaderCheck.cpp — SPDX license-header gate (task 011).
//
// Realizes docs/design/11 sec 13.2 and ADR-013 C18 / ADR-014 sec 1. Scans tracked
// source/CMake/docs files under the repo root for the required marker
//   SPDX-License-Identifier: GPL-3.0-or-later
// and FAILS (non-zero exit) if any is missing. Vendored third-party files and the
// build tree are excluded (they carry their own upstream licenses).
//
// Invoked as: LicenseHeaderCheck <repo-root>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr const char* kMarker = "SPDX-License-Identifier: GPL-3.0-or-later";

bool hasExtension(const fs::path& p, std::initializer_list<const char*> exts) {
    const std::string e = p.extension().string();
    for (const char* x : exts) {
        if (e == x) return true;
    }
    return false;
}

bool isTrackedSource(const fs::path& p) {
    if (hasExtension(p, {".h", ".hpp", ".cpp", ".cc", ".cmake", ".md", ".sh"})) return true;
    if (p.filename() == "CMakeLists.txt") return true;
    return false;
}

// Path segments that mark a file as vendored/build output / not our source.
bool isExcluded(const fs::path& rel) {
    for (const auto& seg : rel) {
        const std::string s = seg.string();
        if (s == "build" || s == "_deps" || s == ".git" ||
            s == "research-cache" || s == "test-output" || s == "test-scratch" ||
            s == ".cache") {
            return true;
        }
    }
    // The vendored CPM bootstrap carries its own MIT license, not our SPDX header.
    if (rel.filename() == "CPM.cmake") return true;
    return false;
}

bool fileHasMarker(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return contents.find(kMarker) != std::string::npos;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "license_headers: usage: LicenseHeaderCheck <repo-root>\n";
        return 2;
    }
    const fs::path root = argv[1];
    if (!fs::exists(root)) {
        std::cerr << "license_headers: repo root does not exist: " << root << "\n";
        return 2;
    }

    std::vector<fs::path> missing;
    int scanned = 0;

    for (auto it = fs::recursive_directory_iterator(
             root, fs::directory_options::skip_permission_denied);
         it != fs::recursive_directory_iterator(); ++it) {
        const fs::path& p = it->path();
        const fs::path rel = fs::relative(p, root);

        if (it->is_directory()) {
            // Prune excluded directories so we never descend into build/_deps/.git.
            const std::string name = p.filename().string();
            if (name == "build" || name == "_deps" || name == ".git" ||
                name == "research-cache" || name == ".cache") {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!it->is_regular_file()) continue;
        if (isExcluded(rel)) continue;
        if (!isTrackedSource(p)) continue;

        ++scanned;
        if (!fileHasMarker(p)) {
            missing.push_back(rel);
        }
    }

    // Silent-pass guard: if we scanned nothing, the check is mis-wired — fail.
    if (scanned == 0) {
        std::cerr << "license_headers: FAIL — scanned 0 files (mis-wired root?)\n";
        return 3;
    }

    if (!missing.empty()) {
        std::cerr << "license_headers: FAIL — " << missing.size()
                  << " file(s) missing '" << kMarker << "':\n";
        for (const auto& m : missing) std::cerr << "  " << m.string() << "\n";
        return 1;
    }

    std::cout << "license_headers: PASS — " << scanned
              << " tracked source/doc files all carry the SPDX header.\n";
    return 0;
}
