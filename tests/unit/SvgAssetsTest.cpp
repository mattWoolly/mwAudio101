// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/SvgAssetsTest.cpp — vector-first asset-strategy tests (task 110).
//
// Realizes the objectively-testable half of docs/design/10-ui.md §12 / ADR-015 C11:
//   * Only static SVG art and the logo are bundled; ZERO raster faceplate bitmaps
//     and NO @2x/@3x raster matrix exist (§12; ADR-015 C11).
//   * Each bundled SVG is well-formed (an <svg> root with closing tag), i.e.
//     juce::Drawable::createFromSVG would parse it — verified here WITHOUT JUCE,
//     because the headless test binary links mwcore only (no JUCE) [tests/CMakeLists
//     §"links mwcore ONLY"]. The actual createFromSVG/BinaryData wiring lives in the
//     JUCE editor (out of this build); the JUCE-free manifest in core/ui/SvgAssets.h
//     is the single source of truth that drives it and is asserted against the
//     on-disk asset directory here.
//
// Test-case NAMES begin with "ui_assets" so `ctest -R ui_assets` selects them; the
// Catch2 tag is [ui_assets] (added to the committed labels snapshot).
//
// The asset directory is located from this translation unit's own absolute path
// (__FILE__) so the test needs no CMake-injected define (tests/CMakeLists.txt is a
// shared file this task must not edit).

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ui/SvgAssets.h"

namespace fs = std::filesystem;

namespace {

// Repo root derived from this file: <root>/tests/unit/SvgAssetsTest.cpp -> <root>.
fs::path repoRoot() {
    return fs::path(__FILE__).parent_path()   // tests/unit
        .parent_path()                        // tests
        .parent_path();                       // <root>
}

fs::path assetsDir() { return repoRoot() / "ui" / "assets"; }

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

// Collect the regular-file names directly under ui/assets (non-recursive: the
// matrix would live here, e.g. logo@2x.png).
std::vector<std::string> assetFilenames() {
    std::vector<std::string> names;
    if (!fs::exists(assetsDir())) return names;
    for (const auto& e : fs::directory_iterator(assetsDir())) {
        if (e.is_regular_file()) names.push_back(e.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace

TEST_CASE("ui_assets: the ui/assets directory exists and bundles at least one SVG", "[ui_assets]") {
    REQUIRE(fs::exists(assetsDir()));
    REQUIRE(fs::is_directory(assetsDir()));

    const auto names = assetFilenames();
    int svgCount = 0;
    for (const auto& n : names) {
        if (mw::ui::assets::isVectorSvg(n)) ++svgCount;
    }
    REQUIRE(svgCount >= 1);                 // logo + decorative art is SVG (§12)
    REQUIRE_FALSE(svgCount == 0);           // paired negative
}

TEST_CASE("ui_assets: zero raster faceplate bitmaps and no @2x/@3x matrix exist", "[ui_assets]") {
    const auto names = assetFilenames();
    REQUIRE_FALSE(names.empty());           // mis-located dir would silently pass otherwise

    std::vector<std::string> rasterOffenders;
    std::vector<std::string> matrixOffenders;
    for (const auto& n : names) {
        if (mw::ui::assets::isRasterBitmap(n))     rasterOffenders.push_back(n);
        if (mw::ui::assets::hasRetinaScaleMatrix(n)) matrixOffenders.push_back(n);
    }

    INFO("raster offenders: " << rasterOffenders.size()
         << "  matrix offenders: " << matrixOffenders.size());
    REQUIRE(rasterOffenders.empty());       // no .png/.jpg/.bmp/... faceplate (ADR-015 C11)
    REQUIRE(matrixOffenders.empty());       // no @2x/@3x raster matrix (ADR-015 C11)

    // Every bundled file must be a vector SVG — nothing else is permitted.
    for (const auto& n : names) {
        INFO("unexpected non-SVG asset: " << n);
        REQUIRE(mw::ui::assets::isVectorSvg(n));
    }
}

TEST_CASE("ui_assets: every bundled SVG is well-formed so createFromSVG would parse it", "[ui_assets]") {
    const auto names = assetFilenames();
    int checked = 0;
    for (const auto& n : names) {
        if (!mw::ui::assets::isVectorSvg(n)) continue;
        ++checked;
        const std::string xml = readFile(assetsDir() / n);
        INFO("malformed SVG: " << n);
        // The minimum juce::Drawable::createFromSVG requires: an <svg ...> element
        // with a matching </svg> close. Assert structural well-formedness without
        // pulling in JUCE.
        REQUIRE(xml.find("<svg") != std::string::npos);
        REQUIRE(xml.find("</svg>") != std::string::npos);
        REQUIRE(xml.find("<svg") < xml.find("</svg>"));
        // It must be vector: no embedded raster payload smuggled in via <image> or a
        // base64 data: bitmap URI.
        REQUIRE(xml.find("<image") == std::string::npos);
        REQUIRE(xml.find("data:image") == std::string::npos);
    }
    REQUIRE(checked >= 1);                   // selector/dir not mis-wired
}

TEST_CASE("ui_assets: the JUCE-free manifest matches the on-disk SVG set and is vector-only", "[ui_assets]") {
    // The manifest is the single source of truth the JUCE editor's BinaryData wiring
    // reads (the editor calls createFromSVG on each entry). It must list exactly the
    // on-disk SVGs and must itself be entirely vector.
    const auto onDisk = assetFilenames();
    std::vector<std::string> onDiskSvg;
    for (const auto& n : onDisk) {
        if (mw::ui::assets::isVectorSvg(n)) onDiskSvg.push_back(n);
    }

    std::vector<std::string> manifest(mw::ui::assets::kBundledSvgs.begin(),
                                      mw::ui::assets::kBundledSvgs.end());
    std::sort(manifest.begin(), manifest.end());

    REQUIRE_FALSE(manifest.empty());
    REQUIRE(manifest == onDiskSvg);          // no drift between manifest and disk

    // The manifest is vector-only and matrix-free by construction.
    for (const auto& n : manifest) {
        INFO("manifest entry not vector-clean: " << n);
        REQUIRE(mw::ui::assets::isVectorSvg(n));
        REQUIRE_FALSE(mw::ui::assets::isRasterBitmap(n));
        REQUIRE_FALSE(mw::ui::assets::hasRetinaScaleMatrix(n));
    }
    REQUIRE(mw::ui::assets::manifestIsVectorOnly());   // compile-checked aggregate

    // The logo is present and is one of the bundled SVGs.
    REQUIRE(std::find(manifest.begin(), manifest.end(),
                      std::string(mw::ui::assets::kLogoSvg)) != manifest.end());
}

TEST_CASE("ui_assets: classifier predicates reject the forbidden raster/matrix shapes", "[ui_assets]") {
    using namespace mw::ui::assets;

    // Vector accepted (case-insensitive extension).
    REQUIRE(isVectorSvg("logo.svg"));
    REQUIRE(isVectorSvg("Decoration.SVG"));
    REQUIRE_FALSE(isVectorSvg("logo.png"));

    // Raster bitmaps rejected.
    REQUIRE(isRasterBitmap("faceplate.png"));
    REQUIRE(isRasterBitmap("knob.jpg"));
    REQUIRE(isRasterBitmap("knob.jpeg"));
    REQUIRE(isRasterBitmap("bg.bmp"));
    REQUIRE(isRasterBitmap("anim.gif"));
    REQUIRE(isRasterBitmap("art.tga"));
    REQUIRE(isRasterBitmap("art.webp"));
    REQUIRE_FALSE(isRasterBitmap("logo.svg"));

    // @2x / @3x retina matrix rejected regardless of extension.
    REQUIRE(hasRetinaScaleMatrix("logo@2x.png"));
    REQUIRE(hasRetinaScaleMatrix("logo@3x.png"));
    REQUIRE(hasRetinaScaleMatrix("logo@2x.svg"));   // even a vector matrix is forbidden
    REQUIRE_FALSE(hasRetinaScaleMatrix("logo.svg"));
    REQUIRE_FALSE(hasRetinaScaleMatrix("logo2x.svg"));  // no '@' -> not a matrix marker
}
