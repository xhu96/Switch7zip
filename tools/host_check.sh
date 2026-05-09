#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT}/build-host"
mkdir -p "${BUILD_DIR}"

cc -std=c11 -Wall -Wextra -I"${ROOT}/include" \
  "${ROOT}/tools/host_cli.c" \
  "${ROOT}/source/archive_extract.c" \
  "${ROOT}/source/fs_utils.c" \
  $(pkg-config --cflags --libs libarchive) \
  -o "${BUILD_DIR}/switch7zip_host"

TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT
mkdir -p "${TMP}/sample/folder"
printf 'hello from Switch 7zip host test\n' > "${TMP}/sample/folder/hello.txt"
( cd "${TMP}/sample" && tar -cf "${TMP}/sample.tar" folder )
"${BUILD_DIR}/switch7zip_host" "${TMP}/sample.tar" "${TMP}/out"
test -f "${TMP}/out/folder/hello.txt"

echo "Host extraction core compiled and passed a tar extraction smoke test."
