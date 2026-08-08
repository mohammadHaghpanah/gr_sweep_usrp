# Copyright 2020 Free Software Foundation, Inc.
#
# This file is part of GNU Radio
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

########################################################################
# Check if there is C++ code at all
########################################################################
if(NOT usrp_sweep_sources)
    MESSAGE(STATUS "No C++ sources... skipping python bindings")
    return()
endif(NOT usrp_sweep_sources)

########################################################################
# Check for pygccxml
########################################################################
GR_PYTHON_CHECK_MODULE_RAW(
    "pygccxml"
    "import pygccxml"
    PYGCCXML_FOUND
    )

include(GrPybind)

########################################################################
# Python Bindings
########################################################################

list(APPEND usrp_sweep_python_files
    usrp_sweep_python.cc python_bindings.cc)

GR_PYBIND_MAKE_OOT(usrp_sweep
   ../../..
   gr::usrp_sweep
   "${usrp_sweep_python_files}")

# copy in bindings .so file for use in QA test module / run_*.sh
add_custom_command(
  TARGET usrp_sweep_python
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          ${CMAKE_BINARY_DIR}/test_modules/gnuradio/usrp_sweep
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:usrp_sweep_python>
          ${CMAKE_BINARY_DIR}/test_modules/gnuradio/usrp_sweep/
  COMMENT "Copy usrp_sweep_python into test_modules")

install(TARGETS usrp_sweep_python DESTINATION ${GR_PYTHON_DIR}/gnuradio/usrp_sweep COMPONENT pythonapi)
