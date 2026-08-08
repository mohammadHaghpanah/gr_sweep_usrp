#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Copyright 2026 Mohammad Haghpanah.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

"""
@file prefill_wait_dialog.py
@brief Qt progress dialog while USRP Sweep fills its circular buffer.
"""

import pmt
from gnuradio import gr

try:
    from PyQt5.QtCore import QObject, Qt, pyqtSignal
    from PyQt5.QtWidgets import QApplication, QProgressDialog, QWidget
except ImportError:  # pragma: no cover
    QObject = object  # type: ignore
    pyqtSignal = None
    QWidget = object  # type: ignore


class _PrefillUiBridge(QObject):
    """
    @brief Qt signal bridge so status messages update the GUI thread-safely.
    """

    progress = pyqtSignal(str, int, int)  # phase, filled, target


class prefill_wait_dialog(gr.basic_block):
    """
    @brief Shows a progress dialog during buffer prefill.

    Connect USRP Sweep (Panorama) port ``status`` → this block ``status``.
    """

    def __init__(self, parent=None, title="USRP Sweep"):
        """
        @param parent Top-block / QWidget used as dialog parent (may be None).
        @param title  Window title prefix.
        """
        gr.basic_block.__init__(
            self, name="prefill_wait_dialog", in_sig=None, out_sig=None
        )
        self._parent = parent
        self._title = title
        self._dlg = None
        self._bridge = None

        if pyqtSignal is not None:
            self._bridge = _PrefillUiBridge()
            self._bridge.progress.connect(self._on_progress)

        self.message_port_register_in(pmt.intern("status"))
        self.set_msg_handler(pmt.intern("status"), self._handle_status)

    def _handle_status(self, msg):
        """
        @brief Parses status PMT dict and forwards to the Qt GUI thread.
        """
        if self._bridge is None or not pmt.is_dict(msg):
            return

        phase_p = pmt.dict_ref(msg, pmt.intern("phase"), pmt.PMT_NIL)
        filled_p = pmt.dict_ref(msg, pmt.intern("filled"), pmt.PMT_NIL)
        target_p = pmt.dict_ref(msg, pmt.intern("target"), pmt.PMT_NIL)

        try:
            phase = pmt.symbol_to_string(phase_p) if pmt.is_symbol(phase_p) else str(phase_p)
            filled = int(pmt.to_long(filled_p))
            target = int(pmt.to_long(target_p))
        except Exception:
            return

        self._bridge.progress.emit(phase, filled, max(1, target))

    def _on_progress(self, phase, filled, target):
        """
        @brief Qt-slot: create/update/close the wait dialog.
        """
        app = QApplication.instance()
        if app is None:
            return

        if phase == "prefill":
            if self._dlg is None:
                parent = self._parent if isinstance(self._parent, QWidget) else None
                self._dlg = QProgressDialog(parent)
                self._dlg.setWindowTitle(self._title)
                self._dlg.setCancelButton(None)
                self._dlg.setMinimumDuration(0)
                self._dlg.setWindowModality(Qt.ApplicationModal)
                self._dlg.setMinimum(0)
                self._dlg.setMaximum(target)
                self._dlg.setValue(0)
                self._dlg.show()

            if self._dlg is None:
                return
            self._dlg.setMaximum(max(1, target))
            self._dlg.setValue(min(max(0, filled), target))
            self._dlg.setLabelText(
                "Please wait — filling spectrum buffers\n"
                f"{filled} / {target}"
            )
            app.processEvents()

        elif phase == "ready":
            if self._dlg is not None:
                try:
                    self._dlg.setMaximum(max(1, target))
                    self._dlg.setValue(target)
                    self._dlg.setLabelText("Ready — starting spectrum display")
                    app.processEvents()
                    self._dlg.close()
                except Exception:
                    pass
                self._dlg = None
