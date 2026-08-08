#!/usr/bin/env bash
# Run a generated GRC Python flowgraph with the rebuilt usrp_sweep module.
set -euo pipefail
ROOT=/home/haghpanah/Desktop/my_github/gr_sweep_usrp
export PYTHONPATH="$ROOT/build/test_modules:$HOME/.local/local/lib/python3.10/dist-packages:${PYTHONPATH:-}"
export LD_LIBRARY_PATH="$ROOT/build/lib:$HOME/.local/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}"
PY=${1:-"$ROOT/TEST/untitled.py"}
shift || true
exec python3 -u "$PY" "$@"
