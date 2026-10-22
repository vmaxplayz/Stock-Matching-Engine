#!/bin/sh
set -eu
cd -- "$(dirname -- "$0")"
make -s
exec ./matching-engine
