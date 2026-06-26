#!/bin/bash

# Test the *deployed* duck_dggs WASM build.
#
# The native `make test` runs the DuckDB unittest binary, but the WASM build
# only produces an artifact — nothing verifies the published WASM extension
# actually loads and runs. This script does, by driving
# Query-farm-haybarn/haybarn-extension-wasm-tester against the published
# @haybarn/haybarn-wasm engine and running this repo's own sqllogictest suite.
#
# Usage:
#   ./scripts/test-wasm.sh [--local-tests] [-- <extra args passed to the tester>]
#
#   (default)       Run the tests AT THE PUBLISHED ref (what users actually get).
#   --local-tests   Run THIS working tree's test/sql/*.test against the live
#                   artifact instead — use to check a local test change before
#                   releasing. NOTE: the deployed artifact lags your source, so
#                   tests referencing not-yet-deployed functions will fail.
#
# Common extra args: --platform mvp | --verbose | --json report.json
#
# Env overrides:
#   ENGINE_VERSION   Force the R2 catalog version (e.g. v1.5.3). Default: auto-detect.
#   PLATFORM         wasm platform: eh (default) | mvp
#   EXT_NAME         Extension to test (default: duck_dggs)
#   WORKDIR          Scratch dir (default: build/wasm-test, gitignored under build/)
#
# Why auto-detect the version: the bundled engine reports a *build* version that
# can be ahead of the *catalog* version actually deployed on R2 (e.g. build
# v1.5.4-dev makes the tester probe an empty v1.5.4 catalog and falsely report
# "not deployed"). We instead probe R2 for the newest version where this
# extension's artifact really exists, and pass it as --engine-version.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

EXT_NAME="${EXT_NAME:-duck_dggs}"
PLATFORM="${PLATFORM:-eh}"
WORKDIR="${WORKDIR:-${REPO_ROOT}/build/wasm-test}"
R2_BASE="https://haybarn-extensions.query.farm/community"
TESTER_REPO="https://github.com/Query-farm-haybarn/haybarn-extension-wasm-tester.git"
COMMUNITY_REPO="https://github.com/Query-farm-haybarn/haybarn-community-extensions.git"

# Parse our own flags first; extra args after a literal `--` go to the tester.
LOCAL_TESTS=0
EXTRA_ARGS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --local-tests) LOCAL_TESTS=1; shift ;;
    --) shift; EXTRA_ARGS=("$@"); break ;;
    *) echo "unknown option: $1 (forward tester args after a literal --)"; exit 2 ;;
  esac
done

command -v node >/dev/null || { echo "error: node (>=18) is required"; exit 2; }
command -v git  >/dev/null || { echo "error: git is required"; exit 2; }

mkdir -p "$WORKDIR"
TESTER_DIR="${WORKDIR}/tester"
COMMUNITY_DIR="${WORKDIR}/community"

clone_or_update() {
  local url="$1" dir="$2"
  if [[ -d "${dir}/.git" ]]; then
    echo "updating $(basename "$dir") ..."
    git -C "$dir" pull --ff-only --quiet || echo "  (pull skipped; using existing checkout)"
  else
    echo "cloning $(basename "$dir") ..."
    git clone --depth 1 --quiet "$url" "$dir"
  fi
}

clone_or_update "$TESTER_REPO"    "$TESTER_DIR"
clone_or_update "$COMMUNITY_REPO" "$COMMUNITY_DIR"

# Install tester deps once (node_modules persists in the gitignored workdir).
if [[ ! -d "${TESTER_DIR}/node_modules" ]]; then
  echo "installing tester dependencies ..."
  ( cd "$TESTER_DIR" && npm install --silent )
fi

plat_dir="wasm_${PLATFORM}"

# Resolve the catalog version: explicit override, else probe R2 for the newest
# version where this extension's artifact exists.
ENGINE_VERSION="${ENGINE_VERSION:-}"
if [[ -z "$ENGINE_VERSION" ]]; then
  echo "detecting deployed catalog version for ${EXT_NAME} (${plat_dir}) ..."
  # Engine major.minor from the bundled package, to bound the scan.
  pkg="${TESTER_DIR}/node_modules/@haybarn/haybarn-wasm/package.json"
  mm="$(node -e "try{console.log(require('${pkg}').version.match(/^(\d+)\.(\d+)/).slice(1,3).join('.'))}catch(e){console.log('')}" 2>/dev/null || true)"
  candidates=()
  if [[ -n "$mm" ]]; then
    major="${mm%%.*}"; minor="${mm##*.}"
    for p in $(seq 20 -1 0); do candidates+=("v${major}.${minor}.${p}"); done
    if (( minor > 0 )); then
      for p in $(seq 20 -1 0); do candidates+=("v${major}.$((minor-1)).${p}"); done
    fi
  fi
  for ver in "${candidates[@]}"; do
    url="${R2_BASE}/${ver}/${plat_dir}/${EXT_NAME}.duckdb_extension.wasm"
    if [[ "$(curl -s -o /dev/null -w '%{http_code}' -I "$url")" == "200" ]]; then
      ENGINE_VERSION="$ver"; break
    fi
  done
  if [[ -z "$ENGINE_VERSION" ]]; then
    echo "error: could not find a deployed ${EXT_NAME} artifact on R2 for any ${mm:-?}.x version."
    echo "       The extension may not be deployed yet, or set ENGINE_VERSION=vX.Y.Z explicitly."
    exit 1
  fi
fi

# The tester clones each extension's source into <src-workdir>/<name> and skips
# re-cloning when a `.cloned-ok` marker is present. For --local-tests we pre-seed
# that dir with THIS repo's test files so the live artifact runs against them.
SRC_WORKDIR="${WORKDIR}/src"
if [[ "$LOCAL_TESTS" == "1" ]]; then
  if [[ "$EXT_NAME" != "duck_dggs" ]]; then
    echo "error: --local-tests only applies to this repo's extension (duck_dggs)"; exit 2
  fi
  seeded="${SRC_WORKDIR}/${EXT_NAME}"
  echo "seeding local tests from ${REPO_ROOT}/test ..."
  rm -rf "$seeded"
  mkdir -p "${seeded}/test/sql"
  cp "${REPO_ROOT}"/test/sql/*.test "${seeded}/test/sql/"
  touch "${seeded}/.cloned-ok"
fi

echo "==> testing ${EXT_NAME} against catalog ${ENGINE_VERSION} (${plat_dir})$([[ $LOCAL_TESTS == 1 ]] && echo ' [local tests]')"
echo ""

exec node "${TESTER_DIR}/bin/test.mjs" \
  --community-dir "$COMMUNITY_DIR" \
  --only "$EXT_NAME" \
  --engine-version "$ENGINE_VERSION" \
  --platform "$PLATFORM" \
  --workdir "$SRC_WORKDIR" \
  --probe-r2 \
  ${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}
