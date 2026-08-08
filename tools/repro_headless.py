#!/usr/bin/env python3
"""
@file repro_headless.py
@brief Headless smoke test for the usrp_sweep GNU Radio source.

Starts a short USRP panorama sweep with a null sink (no GUI) and exits
cleanly if the block constructs and runs. Requires a reachable USRP and
a working local install of gnuradio.usrp_sweep.
"""

import time

from gnuradio import blocks
from gnuradio import gr
from gnuradio import usrp_sweep


class TB(gr.top_block):
    """Minimal flowgraph: usrp_sweep -> null_sink."""

    def __init__(self):
        gr.top_block.__init__(self, "repro")
        # Narrow span so the first sweep slot arrives quickly.
        self.src = usrp_sweep.usrp_sweep(
            "",
            "",
            "",
            "",
            10e6,
            100e6,
            120e6,
            0.5,
            0,
            1024,
            0.25,
            0.01,
            0.5,
            "",
            -1,
            True,
            4,
        )


def main():
    tb = TB()
    sink = blocks.null_sink(gr.sizeof_float)
    tb.connect(tb.src, sink)
    print(
        "sweep_size=%s slots=%s"
        % (tb.src.sweep_size(), tb.src.num_slots())
    )
    tb.start()
    time.sleep(8)
    tb.stop()
    tb.wait()
    print("OK")


if __name__ == "__main__":
    main()
