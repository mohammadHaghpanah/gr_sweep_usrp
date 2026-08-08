#!/usr/bin/env bash
# Launch GNU Radio Companion with usrp_sweep env (prefers rebuilt build/lib).
set -euo pipefail
source "$HOME/.bashrc" || true
ROOT=/home/haghpanah/Desktop/my_github/gr_sweep_usrp
# GRC: later paths overwrite earlier ones for the same block id.
# Put $ROOT/grc LAST so the workspace YAML (with status port) wins over
# stale root-owned copies in ~/.local/share/gnuradio/grc/blocks.
export GRC_BLOCKS_PATH="/usr/share/gnuradio/grc/blocks:/usr/local/share/gnuradio/grc/blocks:$HOME/.local/share/gnuradio/grc/blocks:$HOME/Desktop/my_github/gr-bb60c/grc:$ROOT/grc"
export PYTHONPATH="$ROOT/build/test_modules:$HOME/.local/local/lib/python3.10/dist-packages:/usr/local/lib/python3.10/dist-packages:${PYTHONPATH:-}"
export LD_LIBRARY_PATH="$ROOT/build/lib:$HOME/.local/lib/x86_64-linux-gnu:/usr/local/lib:${LD_LIBRARY_PATH:-}"

echo "GRC_BLOCKS_PATH ends with: ${GRC_BLOCKS_PATH##*:}"
echo "LD_LIBRARY_PATH starts with: ${LD_LIBRARY_PATH%%:*}"
echo "PYTHONPATH starts with: ${PYTHONPATH%%:*}"
# Sanity: workspace YAML must parse and include status
python3 - <<'PY'
import yaml, os
p = os.environ.get("ROOT", "/home/haghpanah/Desktop/my_github/gr_sweep_usrp") + "/grc/usrp_sweep_usrp_sweep.block.yml"
# ROOT not in env; hardcode
p = "/home/haghpanah/Desktop/my_github/gr_sweep_usrp/grc/usrp_sweep_usrp_sweep.block.yml"
d = yaml.safe_load(open(p))
outs = [o["label"] for o in d["outputs"]]
assert "status" in outs, outs
print("workspace block outputs:", outs)
PY
echo "Starting gnuradio-companion..."
exec gnuradio-companion "$@"
