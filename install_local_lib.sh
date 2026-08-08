#!/usr/bin/env bash
# Install built lib into ~/.local (needs write access; use sudo if root-owned).
set -euo pipefail
SRC=/home/haghpanah/Desktop/my_github/gr_sweep_usrp/build/lib
DST=$HOME/.local/lib/x86_64-linux-gnu
PYSRC=/home/haghpanah/Desktop/my_github/gr_sweep_usrp/build/python/usrp_sweep/bindings
PYDST=$HOME/.local/local/lib/python3.10/dist-packages/gnuradio/usrp_sweep

mkdir -p "$DST" "$PYDST"
cp -f "$SRC"/libgnuradio-usrp_sweep.so.1.0.0.0 "$DST"/
ln -sfn libgnuradio-usrp_sweep.so.1.0.0.0 "$DST"/libgnuradio-usrp_sweep.so.1.0.0
ln -sfn libgnuradio-usrp_sweep.so.1.0.0 "$DST"/libgnuradio-usrp_sweep.so
cp -f "$PYSRC"/usrp_sweep_python*.so "$PYDST"/
echo "Installed:"
ls -la "$DST"/libgnuradio-usrp_sweep.so*
strings "$DST"/libgnuradio-usrp_sweep.so.1.0.0.0 | grep -c pass_addr_of_ptr || true
