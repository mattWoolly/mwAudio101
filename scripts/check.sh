#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Matt Woolly
#
# scripts/check.sh — the one-command host configure -> build -> test gate.
#
# Realizes docs/design/11 §9.4 and ADR-014 C1 / Decision. This is the SINGLE
# command a developer runs and the SAME command CI invokes — no build/test logic
# lives only in CI YAML; the workflow is a dumb dispatcher of this script / preset
# names. The preset defaults to `default` (RelWithDebInfo, FP-disciplined, JUCE
# gated off so the build is disk- and network-light).
set -euo pipefail

PRESET="${1:-default}"

# Cache CPM sources so they are fetched once and a no-network rebuild succeeds.
export CPM_SOURCE_CACHE="${CPM_SOURCE_CACHE:-$HOME/.cache/CPM}"

echo "==> configure (preset: ${PRESET})"
cmake --preset "${PRESET}"

echo "==> build (preset: ${PRESET})"
cmake --build --preset "${PRESET}"

echo "==> test (preset: ${PRESET})"
ctest --preset "${PRESET}" --no-tests=error --output-on-failure

echo "==> check.sh: GREEN (preset: ${PRESET})"
