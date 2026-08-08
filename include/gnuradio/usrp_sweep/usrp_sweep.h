/* -*- c++ -*- */
/*
 * Copyright 2026 Mohammad Haghpanah.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file usrp_sweep.h
 * @brief Public API for the USRP panorama / frequency-sweep spectrum source.
 */

#ifndef INCLUDED_USRP_SWEEP_USRP_SWEEP_H
#define INCLUDED_USRP_SWEEP_USRP_SWEEP_H

#include <gnuradio/usrp_sweep/api.h>
#include <gnuradio/sync_block.h>

#include <cstdint>
#include <string>

namespace gr {
namespace usrp_sweep {

/*!
 * \brief Compute stitched panorama length (bins) for given span settings.
 * \ingroup usrp_sweep
 *
 * Matches the Qt Oscilloscope formula:
 *   total = ceil((fft_size / sample_rate) * (stop_fc - start_fc))
 */
USRP_SWEEP_API uint32_t compute_sweep_size(double start_fc,
                                           double stop_fc,
                                           double sample_rate,
                                           size_t fft_size);

/*!
 * \brief Compute number of LO slots needed to cover [start_fc, stop_fc].
 * \ingroup usrp_sweep
 *
 * Matches:
 *   ceil((stop - start - rate) / (rate * (1 - overlap))) + 1
 */
USRP_SWEEP_API size_t compute_num_slots(double start_fc,
                                        double stop_fc,
                                        double sample_rate,
                                        double overlap);

/*!
 * \brief Continuous USRP panorama spectrum source (float stream).
 * \ingroup usrp_sweep
 *
 * Ports the Qt UHD_Timed_TxRx::panorama + Panorama_FFT + stitch logic into a
 * GNU Radio source. Each complete stitched sweep is tagged with \c sweep_start
 * at the first sample (value = start_fc).
 *
 * For a stationary QT GUI Time Sink display:
 *   Trigger Mode = Tag, Trigger Tag Key = sweep_start.
 */
class USRP_SWEEP_API usrp_sweep : virtual public gr::sync_block
{
public:
    typedef std::shared_ptr<usrp_sweep> sptr;

    /*!
     * \brief Create a USRP panorama sweep source.
     *
     * Parameters map to the Qt \c Panorama_Params struct.
     *
     * \param args          UHD device args (e.g. "type=b200")
     * \param rx_subdev     RX subdevice spec ("" = default)
     * \param antenna       RX antenna ("" = default)
     * \param wire          Over-the-wire format ("" = UHD default for sc16)
     * \param sample_rate   RX sample rate (Hz)
     * \param start_fc      Sweep start frequency (Hz)
     * \param stop_fc       Sweep stop frequency (Hz)
     * \param norm_gain     Normalized RX gain in (0, 1]
     * \param bandwidth     Analog BW (Hz); <=0 leaves UHD auto
     * \param fft_size      FFT length / samples collected per slot
     * \param overlap       Fractional overlap in [0, 1) (PANORAMA_OVERLAP)
     * \param lock_time     Seconds of samples discarded after retune (settle)
     * \param lo_setup_time Max seconds to wait for lo_locked
     * \param ref           Clock source ("" = leave default), e.g. "internal"
     * \param master_clock  Master clock rate (Hz); <0 leaves default
     * \param output_db     If true, output 10*log10(|FFT|); else linear |FFT|
     * \param buffer_capacity Circular buffer depth (complete sweeps)
     * \param average_alpha EMA alpha in (0, 1]; 1.0 = no averaging (Qt pano_FFT_average_alpha)
     */
    static sptr make(const std::string& args = "",
                     const std::string& rx_subdev = "",
                     const std::string& antenna = "",
                     const std::string& wire = "",
                     double sample_rate = 10e6,
                     double start_fc = 100e6,
                     double stop_fc = 200e6,
                     float norm_gain = 0.5f,
                     float bandwidth = 0.f,
                     size_t fft_size = 2048,
                     double overlap = 0.25,
                     double lock_time = 0.01,
                     double lo_setup_time = 0.5,
                     const std::string& ref = "",
                     double master_clock = -1.0,
                     bool output_db = true,
                     int buffer_capacity = 32,
                     float average_alpha = 1.0f);

    virtual uint32_t sweep_size() const = 0;
    virtual size_t num_slots() const = 0;

    virtual void set_args(const std::string& args) = 0;
    virtual void set_rx_subdev(const std::string& rx_subdev) = 0;
    virtual void set_antenna(const std::string& antenna) = 0;
    virtual void set_sample_rate(double sample_rate) = 0;
    virtual void set_start_fc(double start_fc) = 0;
    virtual void set_stop_fc(double stop_fc) = 0;
    virtual void set_norm_gain(float norm_gain) = 0;
    virtual void set_bandwidth(float bandwidth) = 0;
    virtual void set_fft_size(size_t fft_size) = 0;
    virtual void set_overlap(double overlap) = 0;
    virtual void set_lock_time(double lock_time) = 0;
    virtual void set_output_db(bool output_db) = 0;
    virtual void set_average_alpha(float average_alpha) = 0;
};
} // namespace usrp_sweep
} // namespace gr

#endif /* INCLUDED_USRP_SWEEP_USRP_SWEEP_H */
