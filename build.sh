#!/bin/sh
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ ! -f "$HERE/portlibs32/lib/libEGL.a" ]; then
  echo "Missing Mesa32: install its include/ and lib/ directories in portlibs32/." >&2
  exit 1
fi
PORT_DIR="$HERE" exec sh "$HERE/runtime/tools/docker_build.sh" "$@"
