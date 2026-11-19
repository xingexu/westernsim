#!/usr/bin/env bash
set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
sdk_version=4.0.20
tool_dir="${WESTERNSIM_WEB_TOOLS:-$repo/.vercel/cache/westernsim}"

if ! command -v emcmake >/dev/null; then
    sdk="$tool_dir/emsdk-$sdk_version"
    if [[ ! -d "$sdk/.git" ]]; then
        mkdir -p "$tool_dir"
        git clone --depth 1 --branch "$sdk_version" https://github.com/emscripten-core/emsdk.git "$sdk"
    fi
    "$sdk/emsdk" install "$sdk_version"
    "$sdk/emsdk" activate "$sdk_version"
    set +u
    source "$sdk/emsdk_env.sh"
    set -u
fi

if ! command -v cmake >/dev/null; then
    cmake_dir="$tool_dir/cmake"
    if [[ ! -x "$cmake_dir/cmake/data/bin/cmake" ]]; then
        python3 -m pip install --only-binary :all: --target "$cmake_dir" cmake==3.31.6
    fi
    export PATH="$cmake_dir/cmake/data/bin:$PATH"
fi

emcmake cmake -S "$repo" -B "$repo/build-web" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build "$repo/build-web" --parallel 2
cmake -E make_directory "$repo/dist"
cmake -E copy_directory "$repo/web" "$repo/dist"
cmake -E copy_if_different "$repo/build-web/westernsim.js" "$repo/build-web/westernsim.wasm" "$repo/dist"
cmake -E copy_if_different "$repo/docs/western-campus.webp" "$repo/dist/campus.webp"
