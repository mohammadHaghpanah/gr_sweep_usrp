/* -*- c++ -*- */
/*
 * Copyright 2026 Mohammad Haghpanah.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file usrp_sweep_impl.h
 * @brief Private implementation of the USRP panorama sweep source block.
 */

#ifndef INCLUDED_USRP_SWEEP_USRP_SWEEP_IMPL_H
#define INCLUDED_USRP_SWEEP_USRP_SWEEP_IMPL_H

#include "sweep_circular_buffer.h"
#include <gnuradio/usrp_sweep/usrp_sweep.h>

#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/stream.hpp>
#include <fftw3.h>

#include <atomic>
#include <chrono>
#include <complex>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gr {
namespace usrp_sweep {

/**
 * @brief Implementation of @ref usrp_sweep.
 *
 * Acquisition thread owns the UHD device: retune → settle → recv → FFT →
 * stitch with overlap → push into @ref sweep_circular_buffer. work() streams
 * bins with sweep_start tags, matching gr-bb60c.
 */
class usrp_sweep_impl : public usrp_sweep
{
private:
    mutable std::mutex d_param_mutex;

    std::string d_args;
    std::string d_rx_subdev;
    std::string d_antenna;
    std::string d_wire;
    double d_sample_rate;
    double d_start_fc;
    double d_stop_fc;
    float d_norm_gain;
    float d_bandwidth;
    size_t d_fft_size;
    double d_overlap;
    double d_lock_time;
    double d_lo_setup_time;
    std::string d_ref;
    double d_master_clock;
    bool d_output_db;
    float d_average_alpha; ///< EMA alpha; 1.0 disables averaging.

    std::atomic<uint32_t> d_sweep_size;
    std::atomic<size_t> d_num_slots;
    double d_bin_size;

    uhd::usrp::multi_usrp::sptr d_usrp;
    uhd::rx_streamer::sptr d_rx_stream; ///< Reused across slots (avoid USB overflow).
    bool d_device_open;

    fftw_plan d_fft_plan;
    fftw_complex* d_fft_in;
    fftw_complex* d_fft_out;
    std::vector<float> d_hamming;

    /// Per-slot EMA state (linear |FFT|), same layout as Qt pano_spectrum*_avg.
    std::vector<std::vector<float>> d_slot_avg;
    std::atomic<bool> d_avg_reset{ true };

    sweep_circular_buffer d_circ_buffer;
    std::atomic<bool> d_running;
    std::atomic<bool> d_reconfig_requested;
    std::thread d_acq_thread;

    std::vector<float> d_pending;
    std::size_t d_pending_offset;
    std::vector<float> d_last_good; ///< Last successfully stitched sweep (GUI hold).
    std::size_t d_prefill_target;   ///< Sweeps to buffer before work() streams.
    std::atomic<bool> d_display_ready{ false };

    /**
     * @brief Device acquisition / panorama loop.
     */
    void acquisition_loop();

    /**
     * @brief Opens and configures the USRP from current parameters.
     * @return true on success.
     */
    bool configure_device();

    /**
     * @brief Applies sample-rate / gain / antenna / span geometry (locked).
     * @return true on success.
     */
    bool apply_device_config_locked();

    /**
     * @brief Releases the USRP shared pointer.
     */
    void close_device();

    /**
     * @brief Rebuilds FFT plan and Hamming window for current fft_size.
     */
    void rebuild_fft_locked();

    /**
     * @brief Frees FFTW resources.
     */
    void destroy_fft();

    /**
     * @brief Recomputes num_slots / sweep_size / bin_size from current params.
     */
    void update_geometry_locked();

    /**
     * @brief Signals acquisition thread to rebuild geometry / gain.
     */
    void request_reconfigure();

    /**
     * @brief Publishes sweep_size / bin_size / start_freq on message ports.
     */
    void publish_sweep_meta();

    /**
     * @brief Publishes prefill/ready status for the GUI wait dialog.
     *
     * @param phase  "prefill" or "ready"
     * @param filled Current number of buffered sweeps
     * @param target Prefill target (usually buffer capacity)
     */
    void publish_status(const char* phase, std::size_t filled, std::size_t target);

    /**
     * @brief Waits for LO lock on channel 0 up to d_lo_setup_time.
     * @return true if locked (or sensor unavailable).
     */
    bool wait_lo_locked();

    /**
     * @brief Stops RX stream and drains residual samples (USB recovery).
     */
    void flush_rx_stream();

    /**
     * @brief Receives one slot after LO retune.
     *
     * @param[out] iq            Destination buffer of length d_fft_size.
     * @param      settle_s      Sleep before capture (seconds); 0 = none.
     * @param      discard_burst If true, discard a short burst before measure.
     * @return true on success.
     */
    bool receive_slot(std::vector<std::complex<short>>& iq,
                      double settle_s,
                      bool discard_burst);

    /**
     * @brief Ensures d_slot_avg matches num_slots × fft_size; clears if reset.
     */
    void ensure_slot_avg_locked();

    /**
     * @brief Applies Qt-style EMA: avg = avg*(1-a) + mag*a; writes result into mag.
     */
    void apply_slot_average_locked(size_t slot, std::vector<float>& mag);

    /**
     * @brief Hamming-windowed FFT magnitude (fftshifted) into @p mag_out.
     */
    void compute_slot_spectrum(const std::vector<std::complex<short>>& iq,
                               std::vector<float>& mag_out);

    /**
     * @brief Stitches per-slot spectra with overlap into one panorama vector.
     */
    std::vector<float>
    stitch_slots(const std::vector<std::vector<float>>& slot_spectra,
                 uint32_t total_points) const;

public:
    usrp_sweep_impl(const std::string& args,
                    const std::string& rx_subdev,
                    const std::string& antenna,
                    const std::string& wire,
                    double sample_rate,
                    double start_fc,
                    double stop_fc,
                    float norm_gain,
                    float bandwidth,
                    size_t fft_size,
                    double overlap,
                    double lock_time,
                    double lo_setup_time,
                    const std::string& ref,
                    double master_clock,
                    bool output_db,
                    int buffer_capacity,
                    float average_alpha);
    ~usrp_sweep_impl() override;

    uint32_t sweep_size() const override
    {
        return d_sweep_size.load(std::memory_order_relaxed);
    }

    size_t num_slots() const override
    {
        return d_num_slots.load(std::memory_order_relaxed);
    }

    void set_args(const std::string& args) override;
    void set_rx_subdev(const std::string& rx_subdev) override;
    void set_antenna(const std::string& antenna) override;
    void set_sample_rate(double sample_rate) override;
    void set_start_fc(double start_fc) override;
    void set_stop_fc(double stop_fc) override;
    void set_norm_gain(float norm_gain) override;
    void set_bandwidth(float bandwidth) override;
    void set_fft_size(size_t fft_size) override;
    void set_overlap(double overlap) override;
    void set_lock_time(double lock_time) override;
    void set_output_db(bool output_db) override;
    void set_average_alpha(float average_alpha) override;

    bool start() override;
    bool stop() override;

    int work(int noutput_items,
             gr_vector_const_void_star& input_items,
             gr_vector_void_star& output_items) override;
};

} // namespace usrp_sweep
} // namespace gr

#endif /* INCLUDED_USRP_SWEEP_USRP_SWEEP_IMPL_H */
