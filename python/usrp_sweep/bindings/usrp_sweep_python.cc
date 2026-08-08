/*
 * Copyright 2026 Mohammad Haghpanah.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/***********************************************************************************/
/* BINDTOOL_GEN_AUTOMATIC(0)                                                       */
/* BINDTOOL_USE_PYGCCXML(0)                                                        */
/* BINDTOOL_HEADER_FILE(usrp_sweep.h)                                              */
/* BINDTOOL_HEADER_FILE_HASH(07a3ed6bc772ee02b330da1cf225826b)                     */
/***********************************************************************************/

#include <pybind11/complex.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

#include <gnuradio/usrp_sweep/usrp_sweep.h>
#include <usrp_sweep_pydoc.h>

void bind_usrp_sweep(py::module& m)
{
    m.def("compute_sweep_size",
          &gr::usrp_sweep::compute_sweep_size,
          py::arg("start_fc"),
          py::arg("stop_fc"),
          py::arg("sample_rate"),
          py::arg("fft_size"));

    m.def("compute_num_slots",
          &gr::usrp_sweep::compute_num_slots,
          py::arg("start_fc"),
          py::arg("stop_fc"),
          py::arg("sample_rate"),
          py::arg("overlap"));

    using usrp_sweep = ::gr::usrp_sweep::usrp_sweep;

    py::class_<usrp_sweep,
               gr::sync_block,
               gr::block,
               gr::basic_block,
               std::shared_ptr<usrp_sweep>>(m, "usrp_sweep", D(usrp_sweep))
        .def(py::init(&usrp_sweep::make),
             py::arg("args") = "",
             py::arg("rx_subdev") = "",
             py::arg("antenna") = "",
             py::arg("wire") = "",
             py::arg("sample_rate") = 10e6,
             py::arg("start_fc") = 100e6,
             py::arg("stop_fc") = 200e6,
             py::arg("norm_gain") = 0.5f,
             py::arg("bandwidth") = 0.f,
             py::arg("fft_size") = 2048,
             py::arg("overlap") = 0.25,
             py::arg("lock_time") = 0.01,
             py::arg("lo_setup_time") = 0.5,
             py::arg("ref") = "",
             py::arg("master_clock") = -1.0,
             py::arg("output_db") = true,
             py::arg("buffer_capacity") = 32,
             py::arg("average_alpha") = 1.0f,
             D(usrp_sweep, make))
        .def("sweep_size", &usrp_sweep::sweep_size)
        .def("num_slots", &usrp_sweep::num_slots)
        .def("set_args", &usrp_sweep::set_args, py::arg("args"))
        .def("set_rx_subdev", &usrp_sweep::set_rx_subdev, py::arg("rx_subdev"))
        .def("set_antenna", &usrp_sweep::set_antenna, py::arg("antenna"))
        .def("set_sample_rate", &usrp_sweep::set_sample_rate, py::arg("sample_rate"))
        .def("set_start_fc", &usrp_sweep::set_start_fc, py::arg("start_fc"))
        .def("set_stop_fc", &usrp_sweep::set_stop_fc, py::arg("stop_fc"))
        .def("set_bandwidth", &usrp_sweep::set_bandwidth, py::arg("bandwidth"))
        .def("set_norm_gain", &usrp_sweep::set_norm_gain, py::arg("norm_gain"))
        .def("set_fft_size", &usrp_sweep::set_fft_size, py::arg("fft_size"))
        .def("set_overlap", &usrp_sweep::set_overlap, py::arg("overlap"))
        .def("set_lock_time", &usrp_sweep::set_lock_time, py::arg("lock_time"))
        .def("set_output_db", &usrp_sweep::set_output_db, py::arg("output_db"))
        .def("set_average_alpha",
             &usrp_sweep::set_average_alpha,
             py::arg("average_alpha"));
}
