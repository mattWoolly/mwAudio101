// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/golden/GoldenStore.h — the golden corpus store: maps a GoldenKey to an
// on-disk golden blob (raw f32) + sidecar JSON, with has() / load() (throws if
// absent) / blobPath() / sidecarPath(), and a store() that writes the pair (task 077,
// golden-5).
//
// Realizes docs/design/11 §5.4 (the GoldenStore signatures — has/load/blobPath/
// sidecarPath; golden blobs are raw f32 + sidecar JSON keyed by the GoldenKey) and
// §7.1 (each golden is a binary blob plus a sidecar JSON recording its GoldenKey and a
// human-readable render-graph description). It loads a blessed RenderResult; it does
// NOT compare (golden-6/7), validate the MANIFEST (golden-8), or bless/author corpus
// artifacts as a side-effect — store() is harness/tool scaffolding the guarded bless
// tool (golden-10) uses, never a test side-effect by itself [task 077 Scope/Out-of-
// scope; §7.2].
//
// Header-only: the design tree lists tests/golden/GoldenStore.{h,cpp}, but a header-
// only realization keeps the primitive self-contained and avoids touching the shared
// tests/CMakeLists glob set (which compiles tests/unit/*.cpp; a tests/golden/*.cpp
// would NOT be picked up, and editing tests/CMakeLists.txt is forbidden by the
// parallel-fleet conflict-avoidance rule). Same pattern as the sibling tests/golden/
// Sha256.h (040), GoldenKey.h (041), Stimulus.h (042), RenderHarness.h (076). This is
// OFFLINE harness code — the no-alloc/no-lock RT invariants are NOT in play here
// [docs/design/11 §2.2].
//
// PATH DERIVATION (the keying contract, §7.1). All paths are pure, stable functions of
// the GoldenKey, derived as:
//
//     <root> / <class> / sr<sampleRate> / <hash16>.f32     (blob)
//     <root> / <class> / sr<sampleRate> / <hash16>.json    (sidecar)
//
//   * <class>      — "exact" or "fp", from key.cls. PARTITIONED so a CLASS-EXACT golden
//                    can never share a directory with a CLASS-FP golden of the same SR
//                    (a mis-classified path would otherwise let one overwrite the other)
//                    [docs/design/11 §5.1, §7.1].
//   * sr<rate>     — "sr44100" .. "sr96000", from key.sampleRate. PARTITIONED so the
//                    four-way rate axis (ADR-023 V12) keeps each rate's corpus separate
//                    [docs/design/11 §5.2, §7.1].
//   * <hash16>     — lowercase 16-hex of mw::golden::hash(key), the §5.3 stable SHA-256-
//                    derived 64-bit key hash (changes when ANY key field changes, stable
//                    run-to-run and across macOS arm64 / Linux x64).
//
// The directory partitioning makes the distinctness across determinism class and
// sample rate STRUCTURAL (different parent_path()), and the hash basename makes two
// goldens with different render-graph/engine/seed in one partition distinct too. The
// whole derivation is integer/string arithmetic — no FP rounding of the path.
//
// BLOB FORMAT (raw f32 + the keyed context, §5.4). A small fixed header followed by the
// raw little-endian f32 samples:
//
//     bytes  0.. 3   magic   "MWG1"          (ASCII)
//     bytes  4.. 7   uint32  format version  (= 1)
//     bytes  8..15   double  sampleRate
//     bytes 16..19   int32   ladder engine   (LadderEngine underlying)
//     bytes 20..23   int32   oversampleFactor
//     bytes 24..27   int32   renderVersion
//     bytes 28..35   uint64  sample count N
//     bytes 36..      N * float32 (little-endian) samples
//
// All multi-byte integers are written little-endian explicitly (no struct-layout /
// endianness dependence), and the floats are written via their raw IEEE-754 bits, so a
// load() reproduces the EXACT float bits that were stored — the byte-identical round-
// trip the CLASS-EXACT contract and the FP arm's bit-exact-on-arm64 gate depend on
// [docs/design/11 §5.1, §6.2]. On the supported little-endian targets the byte order is
// identical, matching the sibling GoldenKey.h serialization.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "GoldenKey.h"
#include "RenderHarness.h"   // RenderResult, EngineTag

namespace mw::golden {

namespace detail {

// --- little-endian primitive (de)serialization (no struct-layout dependence) -------
template <typename T>
inline void putLe(std::vector<std::byte>& out, T value) {
    std::array<std::byte, sizeof(T)> tmp{};
    std::memcpy(tmp.data(), &value, sizeof(T));
    out.insert(out.end(), tmp.begin(), tmp.end());
}

template <typename T>
inline bool getLe(const std::vector<std::byte>& in, std::size_t& pos, T& out) noexcept {
    if (pos + sizeof(T) > in.size()) return false;
    std::memcpy(&out, in.data() + pos, sizeof(T));
    pos += sizeof(T);
    return true;
}

// Human-readable class token used both in the path partition and the sidecar JSON.
inline const char* classToken(DeterminismClass cls) noexcept {
    return cls == DeterminismClass::Exact ? "exact" : "fp";
}

// Sidecar-facing class label matching the MANIFEST corpusClass vocabulary (§7.3).
inline const char* classLabel(DeterminismClass cls) noexcept {
    return cls == DeterminismClass::Exact ? "Exact" : "Fp";
}

inline const char* ladderToken(LadderEngine e) noexcept {
    return e == LadderEngine::ZDF ? "ZDF" : "Huovilainen";
}

// "sr48000" etc. The sample rate is a blessed-set exact integer-valued double; render
// it as an integer so the partition name has no decimal noise. A non-integer rate
// falls back to a fixed-point rendering (defensive; blessed rates never hit it).
inline std::string sampleRatePartition(double sampleRate) {
    const long long whole = static_cast<long long>(sampleRate);
    if (static_cast<double>(whole) == sampleRate)
        return "sr" + std::to_string(whole);
    std::ostringstream os;
    os << "sr" << static_cast<long long>(sampleRate * 1000.0 + 0.5) << "m";  // milli-Hz
    return os.str();
}

// 16-hex lowercase of the §5.3 stable 64-bit key hash — the per-key basename.
inline std::string keyHashHex(const GoldenKey& key) {
    static constexpr char kHex[] = "0123456789abcdef";
    const std::uint64_t h = mw::golden::hash(key);
    std::string s(16, '0');
    for (int i = 0; i < 16; ++i)
        s[static_cast<std::size_t>(15 - i)] = kHex[(h >> (i * 4)) & 0xFu];
    return s;
}

} // namespace detail

// ---------------------------------------------------------------------------
// GoldenStore — keys, locates, loads (and writes) golden blobs + sidecars under a
// corpus root [docs/design/11 §5.4]. Constructed with the corpus root directory; all
// paths are derived UNDER it from the GoldenKey. Stateless beyond the root.
// ---------------------------------------------------------------------------
class GoldenStore {
public:
    explicit GoldenStore(std::filesystem::path corpusRoot)
        : root_(std::move(corpusRoot)) {}

    // The corpus root this store reads/writes under.
    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

    // Stable, pure function of the key: the partition directory
    // <root>/<class>/sr<rate>. Partitioned by determinism class AND sample rate so the
    // two axes never collide [docs/design/11 §5.1, §5.2, §7.1].
    [[nodiscard]] std::filesystem::path partitionDir(const GoldenKey& key) const {
        return root_ / detail::classToken(key.cls)
                     / detail::sampleRatePartition(key.sampleRate);
    }

    // Stable, pure function of the key: the blob path
    // <root>/<class>/sr<rate>/<hash16>.f32 [docs/design/11 §5.4, §7.1].
    [[nodiscard]] std::filesystem::path blobPath(const GoldenKey& key) const {
        return partitionDir(key) / (detail::keyHashHex(key) + ".f32");
    }

    // Stable, pure function of the key: the sidecar path
    // <root>/<class>/sr<rate>/<hash16>.json [docs/design/11 §5.4, §7.1].
    [[nodiscard]] std::filesystem::path sidecarPath(const GoldenKey& key) const {
        return partitionDir(key) / (detail::keyHashHex(key) + ".json");
    }

    // True iff BOTH the blob and the sidecar exist for the key. A golden is the pair
    // (blob + sidecar) [§7.1], so a half-written artifact (one without the other) is
    // NOT "present" — has() is false and load() will throw, never returning a partial.
    [[nodiscard]] bool has(const GoldenKey& key) const {
        std::error_code ec;
        const bool blob    = std::filesystem::exists(blobPath(key), ec) && !ec;
        std::error_code ec2;
        const bool sidecar = std::filesystem::exists(sidecarPath(key), ec2) && !ec2;
        return blob && sidecar;
    }

    // Load the blessed RenderResult for the key. THROWS std::runtime_error if absent or
    // malformed [docs/design/11 §5.4 — "throws if absent"]. The samples + the pinned
    // context (sampleRate + EngineTag) round-trip byte-identically from the blob.
    [[nodiscard]] RenderResult load(const GoldenKey& key) const {
        if (!has(key))
            throw std::runtime_error(
                "GoldenStore::load: no golden for key at " + blobPath(key).string());

        const std::filesystem::path bp = blobPath(key);
        std::ifstream in(bp, std::ios::binary | std::ios::ate);
        if (!in)
            throw std::runtime_error("GoldenStore::load: cannot open blob " + bp.string());

        const std::streamoff size = in.tellg();
        in.seekg(0, std::ios::beg);
        std::vector<std::byte> bytes(size > 0 ? static_cast<std::size_t>(size) : 0);
        if (!bytes.empty())
            in.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));

        return decodeBlob(bytes, bp);
    }

    // Write the blob + sidecar pair for the key, creating the partition directory.
    // Round-trips back through load() byte-identically. This is harness/tool
    // scaffolding (the guarded bless tool, golden-10, drives it) — not a test side-
    // effect on its own [§7.2; task 077 Out-of-scope re-affirmed].
    void store(const GoldenKey& key, const RenderResult& result) const {
        std::filesystem::create_directories(partitionDir(key));

        // --- blob ---
        const std::vector<std::byte> blob = encodeBlob(result);
        const std::filesystem::path bp = blobPath(key);
        std::ofstream out(bp, std::ios::binary | std::ios::trunc);
        if (!out)
            throw std::runtime_error("GoldenStore::store: cannot write blob " + bp.string());
        out.write(reinterpret_cast<const char*>(blob.data()),
                  static_cast<std::streamsize>(blob.size()));
        out.close();

        // --- sidecar JSON ---
        const std::string json = sidecarJson(key, result);
        const std::filesystem::path sp = sidecarPath(key);
        std::ofstream sout(sp, std::ios::trunc);
        if (!sout)
            throw std::runtime_error("GoldenStore::store: cannot write sidecar " + sp.string());
        sout << json;
    }

private:
    static constexpr std::array<std::byte, 4> kMagic = {
        std::byte{'M'}, std::byte{'W'}, std::byte{'G'}, std::byte{'1'} };
    static constexpr std::uint32_t kBlobVersion = 1;

    // Serialize a RenderResult into the §5.4 blob byte layout (header + raw f32).
    [[nodiscard]] static std::vector<std::byte> encodeBlob(const RenderResult& r) {
        std::vector<std::byte> out;
        out.reserve(36 + r.samples.size() * sizeof(float));
        out.insert(out.end(), kMagic.begin(), kMagic.end());
        detail::putLe<std::uint32_t>(out, kBlobVersion);
        detail::putLe<double>(out, r.sampleRate);
        detail::putLe<std::int32_t>(out, static_cast<std::int32_t>(r.engine.ladder));
        detail::putLe<std::int32_t>(out, r.engine.oversampleFactor);
        detail::putLe<std::int32_t>(out, r.engine.renderVersion);
        detail::putLe<std::uint64_t>(out, static_cast<std::uint64_t>(r.samples.size()));
        for (float s : r.samples)
            detail::putLe<float>(out, s);   // raw IEEE-754 bits, little-endian
        return out;
    }

    // Inverse of encodeBlob: reconstruct the exact float bits + pinned context. Throws
    // on a malformed/truncated blob rather than returning a half-decoded result.
    [[nodiscard]] static RenderResult decodeBlob(const std::vector<std::byte>& bytes,
                                                 const std::filesystem::path& bp) {
        const auto fail = [&](const char* why) -> RenderResult {
            throw std::runtime_error(std::string("GoldenStore::load: ") + why + " in "
                                     + bp.string());
        };

        std::size_t pos = 0;
        if (bytes.size() < 36) return fail("blob too small for header");
        if (std::memcmp(bytes.data(), kMagic.data(), kMagic.size()) != 0)
            return fail("bad magic");
        pos = kMagic.size();

        std::uint32_t version = 0;
        if (!detail::getLe(bytes, pos, version)) return fail("truncated version");
        if (version != kBlobVersion) return fail("unsupported blob version");

        RenderResult r{};
        std::int32_t ladder = 0, os = 0, rv = 0;
        std::uint64_t n = 0;
        if (!detail::getLe(bytes, pos, r.sampleRate)) return fail("truncated sampleRate");
        if (!detail::getLe(bytes, pos, ladder))       return fail("truncated ladder");
        if (!detail::getLe(bytes, pos, os))           return fail("truncated oversampleFactor");
        if (!detail::getLe(bytes, pos, rv))           return fail("truncated renderVersion");
        if (!detail::getLe(bytes, pos, n))            return fail("truncated sample count");

        r.engine.ladder           = static_cast<LadderEngine>(ladder);
        r.engine.oversampleFactor = os;
        r.engine.renderVersion    = rv;
        r.constantSetSelected     = true;   // a stored golden was rendered (set bound)

        if (pos + n * sizeof(float) != bytes.size())
            return fail("sample payload length mismatch");

        r.samples.resize(static_cast<std::size_t>(n));
        for (std::uint64_t i = 0; i < n; ++i) {
            float s = 0.0f;
            if (!detail::getLe(bytes, pos, s)) return fail("truncated samples");
            r.samples[static_cast<std::size_t>(i)] = s;
        }
        return r;
    }

    // The sidecar JSON: the GoldenKey fields + a human-readable render-graph
    // description [docs/design/11 §7.1; task 077 Scope]. Hand-emitted (no JSON library
    // in the JUCE-free mwcore-only test world) but well-formed, minimal-escaping JSON.
    [[nodiscard]] static std::string sidecarJson(const GoldenKey& key,
                                                 const RenderResult& r) {
        std::ostringstream js;
        js << "{\n";
        js << "  \"renderGraphHash\": " << key.renderGraphHash << ",\n";
        js << "  \"engine\": {\n";
        js << "    \"ladder\": \"" << detail::ladderToken(key.engine.ladder) << "\",\n";
        js << "    \"oversampleFactor\": " << key.engine.oversampleFactor << ",\n";
        js << "    \"renderVersion\": " << key.engine.renderVersion << "\n";
        js << "  },\n";
        js << "  \"sampleRate\": " << renderDouble(key.sampleRate) << ",\n";
        js << "  \"blockSize\": " << key.blockSize << ",\n";
        js << "  \"seed\": " << key.seed << ",\n";
        js << "  \"determinismClass\": \"" << detail::classLabel(key.cls) << "\",\n";
        js << "  \"sampleCount\": " << r.samples.size() << ",\n";
        js << "  \"keyHash\": \"" << detail::keyHashHex(key) << "\",\n";
        // Human-readable render-graph description — what was rendered, in prose.
        js << "  \"renderGraph\": \""
           << "mwAudio101 serial voice (VCO -> mixer -> IR3109 ladder -> VCA) rendered "
           << "offline at " << renderDouble(key.sampleRate) << " Hz, blockSize "
           << key.blockSize << ", " << detail::ladderToken(key.engine.ladder)
           << " ladder, " << key.engine.oversampleFactor << "x oversampling, "
           << "renderVersion " << key.engine.renderVersion << ", "
           << detail::classLabel(key.cls) << " determinism class, seed "
           << key.seed << "\"\n";
        js << "}\n";
        return js.str();
    }

    // Render a blessed-set double as a plain integer string when it is integer-valued
    // (the blessed rates are), else as its default stream form. Keeps the sidecar tidy.
    [[nodiscard]] static std::string renderDouble(double v) {
        const long long whole = static_cast<long long>(v);
        if (static_cast<double>(whole) == v) return std::to_string(whole);
        std::ostringstream os;
        os << v;
        return os.str();
    }

    std::filesystem::path root_;
};

} // namespace mw::golden
