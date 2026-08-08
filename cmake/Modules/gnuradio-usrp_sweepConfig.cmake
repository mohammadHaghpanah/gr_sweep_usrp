find_package(PkgConfig)

PKG_CHECK_MODULES(PC_GR_USRP_SWEEP gnuradio-usrp_sweep)

FIND_PATH(
    GR_USRP_SWEEP_INCLUDE_DIRS
    NAMES gnuradio/usrp_sweep/api.h
    HINTS $ENV{USRP_SWEEP_DIR}/include
        ${PC_USRP_SWEEP_INCLUDEDIR}
    PATHS ${CMAKE_INSTALL_PREFIX}/include
          /usr/local/include
          /usr/include
)

FIND_LIBRARY(
    GR_USRP_SWEEP_LIBRARIES
    NAMES gnuradio-usrp_sweep
    HINTS $ENV{USRP_SWEEP_DIR}/lib
        ${PC_USRP_SWEEP_LIBDIR}
    PATHS ${CMAKE_INSTALL_PREFIX}/lib
          ${CMAKE_INSTALL_PREFIX}/lib64
          /usr/local/lib
          /usr/local/lib64
          /usr/lib
          /usr/lib64
          )

include("${CMAKE_CURRENT_LIST_DIR}/gnuradio-usrp_sweepTarget.cmake")

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(GR_USRP_SWEEP DEFAULT_MSG GR_USRP_SWEEP_LIBRARIES GR_USRP_SWEEP_INCLUDE_DIRS)
MARK_AS_ADVANCED(GR_USRP_SWEEP_LIBRARIES GR_USRP_SWEEP_INCLUDE_DIRS)
