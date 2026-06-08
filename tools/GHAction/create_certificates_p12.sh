#!/usr/bin/env zsh
set -e

SOLAR2D_PROJECT_DIR="${${0:A}%%/tools/GHAction/*}"
cd "${SOLAR2D_PROJECT_DIR}"

exec ruby "${SOLAR2D_PROJECT_DIR}/tools/GHAction/refreshSigningAssets.rb" "$@"
