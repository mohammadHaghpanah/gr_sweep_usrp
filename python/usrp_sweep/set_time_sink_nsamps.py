#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Copyright 2026 Mohammad Haghpanah.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

"""
@file set_time_sink_nsamps.py
@brief Message helper: USRP Sweep num_points → QT GUI Time Sink set_nsamps().
"""

import pmt
from gnuradio import gr


class set_time_sink_nsamps(gr.basic_block):
    """
    @brief Applies (num_points . N) messages to a QT GUI Time Sink.

    Resolves the sink by attribute name on the first message (or later), so GRC
    block construction order does not matter — unlike blocks.msg_pair_to_var
    which needs getattr(self, sink).set_nsamps at make() time.
    """

    def __init__(self, parent, time_sink_id="qtgui_time_sink_x_0"):
        """
        @param parent Top block / QWidget that owns the Time Sink attribute.
        @param time_sink_id Attribute name of the qtgui.time_sink_* instance.
        """
        gr.basic_block.__init__(
            self, name="set_time_sink_nsamps", in_sig=None, out_sig=None
        )
        self._parent = parent
        self._time_sink_id = str(time_sink_id)
        self._last_n = None
        self.message_port_register_in(pmt.intern("inpair"))
        self.set_msg_handler(pmt.intern("inpair"), self._handle_inpair)

    def _handle_inpair(self, msg):
        """
        @brief Handles PMT pair (num_points . N) and calls set_nsamps(N).
        """
        if not pmt.is_pair(msg):
            return
        try:
            n = int(pmt.to_long(pmt.cdr(msg)))
        except Exception:
            return
        if n <= 0:
            return
        if self._last_n == n:
            return

        sink = getattr(self._parent, self._time_sink_id, None)
        if sink is None or not hasattr(sink, "set_nsamps"):
            return
        try:
            sink.set_nsamps(n)
            self._last_n = n
        except Exception:
            pass
