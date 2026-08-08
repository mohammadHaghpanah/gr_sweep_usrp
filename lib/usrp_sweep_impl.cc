/* -*- c++ -*- */
/*
 * Copyright 2026 Mohammad Haghpanah.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file usrp_sweep_impl.cc
 * @brief USRP panorama sweep source: retune, FFT, overlap-stitch, float stream.
 *
 * Ports Qt UHD_Timed_TxRx::panorama + panorama_receiver + Panorama_FFT +
 * Oscilloscope stitch logic into a GNU Radio OOT source (same display contract
 * as gr-bb60c: float bins + sweep_start tags).
 */

#include "usrp_sweep_impl.h"
#include <gnuradio/io_signature.h>
#include <pmt/pmt.h>

#include <uhd/stream.hpp>
#include <uhd/types/tune_request.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>


namespace gr {
namespace usrp_sweep {

namespace {

/**
 * @brief Hamming window coefficient for index @p n of length @p N.
 */
inline float hamming(size_t n, size_t N)
{
    if (N <= 1) {
        return 1.f;
    }
    return 0.54f - 0.46f * std::cos(2.f * static_cast<float>(M_PI) *
                                    static_cast<float>(n) /
                                    static_cast<float>(N - 1));
}

/**
 * @brief Clamp circular-buffer depth to a practical range.
 *
 * Huge values (e.g. 1e6 from GRC) would never finish prefill and waste RAM.
 */
inline int clamp_buffer_capacity(int buffer_capacity)
{
    constexpr int k_max = 32;
    if (buffer_capacity < 1) {
        return 1;
    }
    if (buffer_capacity > k_max) {
        std::cerr << "usrp_sweep: buffer_capacity=" << buffer_capacity
                  << " clamped to " << k_max
                  << " (needed for full-buffer prefill before display)"
                  << std::endl;
        return k_max;
    }
    return buffer_capacity;
}

/**
 * @brief Clamp EMA alpha to (0, 1]; invalid/zero → 1.0 (no averaging).
 */
inline float clamp_average_alpha(float average_alpha)
{
    if (!(average_alpha > 0.f) || average_alpha > 1.f) {
        return 1.f;
    }
    return average_alpha;
}

/**
 * @brief Clamp overlap to [0, 1). Value 1.0 (GRC combo) → 0.999.
 */
inline double clamp_overlap(double overlap)
{
    if (!(overlap >= 0.0)) {
        return 0.0;
    }
    if (overlap >= 1.0) {
        std::cerr << "usrp_sweep: overlap=" << overlap
                  << " clamped to 0.999 (must be < 1)" << std::endl;
        return 0.999;
    }
    return overlap;
}

} // namespace

uint32_t compute_sweep_size(double start_fc,
                            double stop_fc,
                            double sample_rate,
                            size_t fft_size)
{
    if (!(sample_rate > 0.0) || fft_size == 0 || !(stop_fc > start_fc)) {
        return 0;
    }
    return static_cast<uint32_t>(
        std::ceil((static_cast<double>(fft_size) / sample_rate) *
                  (stop_fc - start_fc)));
}

size_t compute_num_slots(double start_fc,
                         double stop_fc,
                         double sample_rate,
                         double overlap)
{
    if (!(sample_rate > 0.0) || !(stop_fc > start_fc)) {
        return 1;
    }
    const double denom = sample_rate * (1.0 - overlap);
    if (!(denom > 0.0)) {
        return 1;
    }
    const double n =
        std::ceil((stop_fc - start_fc - sample_rate) / denom) + 1.0;
    return static_cast<size_t>(std::max(1.0, n));
}

usrp_sweep::sptr usrp_sweep::make(const std::string& args,
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
                                  float average_alpha)
{
    return gnuradio::make_block_sptr<usrp_sweep_impl>(args,
                                                      rx_subdev,
                                                      antenna,
                                                      wire,
                                                      sample_rate,
                                                      start_fc,
                                                      stop_fc,
                                                      norm_gain,
                                                      bandwidth,
                                                      fft_size,
                                                      overlap,
                                                      lock_time,
                                                      lo_setup_time,
                                                      ref,
                                                      master_clock,
                                                      output_db,
                                                      buffer_capacity,
                                                      average_alpha);
}

usrp_sweep_impl::usrp_sweep_impl(const std::string& args,
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
                                 float average_alpha)
    : gr::sync_block(
          "usrp_sweep",
          gr::io_signature::make(0, 0, 0),
          gr::io_signature::makev(1,
                                  2,
                                  std::vector<int>{ sizeof(float),
                                                    sizeof(std::int32_t) })),
      d_args(args),
      d_rx_subdev(rx_subdev),
      d_antenna(antenna),
      d_wire(wire),
      d_sample_rate(sample_rate),
      d_start_fc(start_fc),
      d_stop_fc(stop_fc),
      d_norm_gain(norm_gain),
      d_bandwidth(bandwidth),
      d_fft_size(fft_size == 0 ? 2048 : fft_size),
      d_overlap(clamp_overlap(overlap)),
      d_lock_time(lock_time),
      d_lo_setup_time(lo_setup_time),
      d_ref(ref),
      d_master_clock(master_clock),
      d_output_db(output_db),
      d_average_alpha(clamp_average_alpha(average_alpha)),
      d_sweep_size(0),
      d_num_slots(1),
      d_bin_size(0.0),
      d_device_open(false),
      d_fft_plan(nullptr),
      d_fft_in(nullptr),
      d_fft_out(nullptr),
      d_avg_reset(true),
      d_circ_buffer(static_cast<std::size_t>(clamp_buffer_capacity(buffer_capacity))),
      d_running(false),
      d_reconfig_requested(false),
      d_pending_offset(0),
      d_prefill_target(0),
      d_display_ready(false)
{
    // Prefill the entire circular buffer before the GUI sees any spectrum.
    d_prefill_target = d_circ_buffer.capacity();

    message_port_register_out(pmt::mp("meta"));
    message_port_register_out(pmt::mp("num_points"));
    message_port_register_out(pmt::mp("status"));

    std::lock_guard<std::mutex> lock(d_param_mutex);
    rebuild_fft_locked();
    update_geometry_locked();

    std::cerr << "usrp_sweep: buffer_capacity=" << d_circ_buffer.capacity()
              << " (prefill all before display), num_slots="
              << d_num_slots.load() << ", sweep_size=" << d_sweep_size.load()
              << ", average_alpha=" << d_average_alpha << std::endl;
}

usrp_sweep_impl::~usrp_sweep_impl()
{
    stop();
    destroy_fft();
}

void usrp_sweep_impl::destroy_fft()
{
    if (d_fft_plan != nullptr) {
        fftw_destroy_plan(d_fft_plan);
        d_fft_plan = nullptr;
    }
    if (d_fft_in != nullptr) {
        fftw_free(d_fft_in);
        d_fft_in = nullptr;
    }
    if (d_fft_out != nullptr) {
        fftw_free(d_fft_out);
        d_fft_out = nullptr;
    }
}

void usrp_sweep_impl::rebuild_fft_locked()
{
    destroy_fft();
    d_fft_in = fftw_alloc_complex(d_fft_size);
    d_fft_out = fftw_alloc_complex(d_fft_size);
    d_fft_plan = fftw_plan_dft_1d(static_cast<int>(d_fft_size),
                                  d_fft_in,
                                  d_fft_out,
                                  FFTW_FORWARD,
                                  FFTW_ESTIMATE);
    d_hamming.resize(d_fft_size);
    for (size_t i = 0; i < d_fft_size; ++i) {
        d_hamming[i] = hamming(i, d_fft_size);
    }
    d_avg_reset.store(true, std::memory_order_relaxed);
}

void usrp_sweep_impl::update_geometry_locked()
{
    const size_t slots =
        compute_num_slots(d_start_fc, d_stop_fc, d_sample_rate, d_overlap);
    const uint32_t total =
        compute_sweep_size(d_start_fc, d_stop_fc, d_sample_rate, d_fft_size);
    d_num_slots.store(slots, std::memory_order_relaxed);
    d_sweep_size.store(total, std::memory_order_relaxed);
    d_bin_size = (d_fft_size > 0) ? (d_sample_rate / static_cast<double>(d_fft_size))
                                  : 0.0;
    d_avg_reset.store(true, std::memory_order_relaxed);
}

void usrp_sweep_impl::ensure_slot_avg_locked()
{
    const size_t slots = d_num_slots.load(std::memory_order_relaxed);
    const size_t n = d_fft_size;
    const bool need_resize =
        (d_slot_avg.size() != slots) ||
        (!d_slot_avg.empty() && d_slot_avg[0].size() != n);
    const bool did_reset =
        need_resize || d_avg_reset.exchange(false, std::memory_order_acq_rel);
    if (did_reset) {
        d_slot_avg.assign(slots, std::vector<float>(n, 0.f));
    }
}

void usrp_sweep_impl::apply_slot_average_locked(size_t slot,
                                                std::vector<float>& mag)
{
    ensure_slot_avg_locked();
    if (slot >= d_slot_avg.size()) {
        return;
    }
    const float a = d_average_alpha;
    auto& avg = d_slot_avg[slot];
    const size_t n = std::min(mag.size(), avg.size());
    if (a >= 1.f) {
        // No averaging: keep mag as-is, but refresh avg state.
        for (size_t i = 0; i < n; ++i) {
            avg[i] = mag[i];
        }
        return;
    }
    const float one_m_a = 1.f - a;
    for (size_t i = 0; i < n; ++i) {
        avg[i] = avg[i] * one_m_a + mag[i] * a;
        mag[i] = avg[i];
    }
}

void usrp_sweep_impl::request_reconfigure()
{
    if (d_running.load(std::memory_order_relaxed)) {
        d_reconfig_requested.store(true, std::memory_order_release);
    }
}

void usrp_sweep_impl::publish_sweep_meta()
{
    const uint32_t sweep_size = d_sweep_size.load(std::memory_order_relaxed);
    pmt::pmt_t meta = pmt::make_dict();
    meta = pmt::dict_add(meta, pmt::mp("sweep_size"), pmt::from_long(sweep_size));
    meta = pmt::dict_add(meta, pmt::mp("bin_size"), pmt::from_double(d_bin_size));
    meta = pmt::dict_add(meta, pmt::mp("start_freq"), pmt::from_double(d_start_fc));
    meta = pmt::dict_add(
        meta,
        pmt::mp("num_slots"),
        pmt::from_long(static_cast<long>(d_num_slots.load(std::memory_order_relaxed))));
    message_port_pub(pmt::mp("meta"), meta);
    message_port_pub(pmt::mp("num_points"),
                     pmt::cons(pmt::mp("num_points"), pmt::from_long(sweep_size)));
}

void usrp_sweep_impl::publish_status(const char* phase,
                                     std::size_t filled,
                                     std::size_t target)
{
    try {
        pmt::pmt_t st = pmt::make_dict();
        st = pmt::dict_add(st, pmt::mp("phase"), pmt::string_to_symbol(phase));
        st = pmt::dict_add(
            st, pmt::mp("filled"), pmt::from_long(static_cast<long>(filled)));
        st = pmt::dict_add(
            st, pmt::mp("target"), pmt::from_long(static_cast<long>(target)));
        message_port_pub(pmt::mp("status"), st);
    } catch (...) {
        // Port may already be torn down during stop()/destructor.
    }
}

bool usrp_sweep_impl::apply_device_config_locked()
{
    if (!(d_sample_rate > 0.0)) {
        std::cerr << "usrp_sweep: invalid sample_rate" << std::endl;
        return false;
    }
    if (!(d_stop_fc > d_start_fc)) {
        std::cerr << "usrp_sweep: invalid start/stop (start=" << d_start_fc
                  << " stop=" << d_stop_fc << ")" << std::endl;
        return false;
    }
    if (d_overlap < 0.0 || d_overlap >= 1.0) {
        d_overlap = clamp_overlap(d_overlap);
    }

    try {
        if (!d_rx_subdev.empty()) {
            d_usrp->set_rx_subdev_spec(d_rx_subdev);
        }
    } catch (const std::exception& e) {
        std::cerr << "usrp_sweep: RX subdev warning: " << e.what() << std::endl;
        try {
            d_usrp->set_rx_subdev_spec(std::string(""));
        } catch (...) {
        }
    }

    try {
        if (d_master_clock > 0.0) {
            d_usrp->set_master_clock_rate(d_master_clock);
        }
    } catch (const std::exception& e) {
        std::cerr << "usrp_sweep: master clock warning: " << e.what() << std::endl;
    }

    try {
        if (!d_ref.empty()) {
            d_usrp->set_clock_source(d_ref);
        }
    } catch (const std::exception& e) {
        std::cerr << "usrp_sweep: clock source warning: " << e.what() << std::endl;
    }

    // Requested rate may be coerced by UHD (common on N200/USRP2).
    // Sweep geometry MUST use the actual hardware rate or slots/stitch drift.
    const double requested_rate = d_sample_rate;
    d_usrp->set_rx_rate(d_sample_rate);
    try {
        const double actual_rate = d_usrp->get_rx_rate(0);
        if (actual_rate > 0.0) {
            const double rel =
                std::abs(actual_rate - requested_rate) / requested_rate;
            if (rel > 1e-6) {
                std::cerr << "usrp_sweep: sample_rate coerced " << requested_rate
                          << " -> " << actual_rate
                          << " (recomputing sweep geometry)" << std::endl;
                d_sample_rate = actual_rate;
            }
        }
    } catch (...) {
    }
    update_geometry_locked();

    uhd::tune_request_t tune_req(d_start_fc, 0.0);
    tune_req.args = uhd::device_addr_t("mode_n=integer");
    d_usrp->set_rx_freq(tune_req, 0);

    if (d_norm_gain > 0.f && d_norm_gain <= 1.f) {
        d_usrp->set_normalized_rx_gain(d_norm_gain, 0);
    }

    if (d_bandwidth > 0.f) {
        d_usrp->set_rx_bandwidth(d_bandwidth, 0);
    }

    if (!d_antenna.empty()) {
        std::string ant = d_antenna;
        bool ant_ok = false;
        try {
            d_usrp->set_rx_antenna(ant, 0);
            ant_ok = true;
        } catch (const std::exception& e) {
            // Case-insensitive match (GRC often has "Rx2" vs hardware "RX2").
            try {
                const auto names = d_usrp->get_rx_antennas(0);
                for (const auto& n : names) {
                    if (n.size() == ant.size() &&
                        std::equal(n.begin(),
                                   n.end(),
                                   ant.begin(),
                                   [](char a, char b) {
                                       return std::tolower(static_cast<unsigned char>(
                                                  a)) ==
                                              std::tolower(static_cast<unsigned char>(
                                                  b));
                                   })) {
                        ant = n;
                        d_usrp->set_rx_antenna(ant, 0);
                        ant_ok = true;
                        std::cerr << "usrp_sweep: antenna matched as '" << ant
                                  << "'" << std::endl;
                        break;
                    }
                }
            } catch (...) {
            }
            if (!ant_ok) {
                std::cerr << "usrp_sweep: antenna warning: " << e.what()
                          << std::endl;
                try {
                    d_usrp->set_rx_antenna(std::string(""), 0);
                } catch (...) {
                }
            }
        }
    }


    d_circ_buffer.clear();
    publish_sweep_meta();

    std::cout << "usrp_sweep: slots=" << d_num_slots.load()
              << " sweep_size=" << d_sweep_size.load()
              << " rate=" << d_sample_rate
              << " bins. Time Sink: Trigger=Tag, Tag Key=sweep_start" << std::endl;
    return true;
}

bool usrp_sweep_impl::configure_device()
{
    try {
        d_usrp = uhd::usrp::multi_usrp::make(d_args);
        try {
            d_usrp->set_rx_dc_offset(true);
        } catch (...) {
        }
        d_device_open = true;
    } catch (const std::exception& e) {
        std::cerr << "usrp_sweep: failed to create USRP: " << e.what() << std::endl;
        return false;
    }

    std::lock_guard<std::mutex> lock(d_param_mutex);
    rebuild_fft_locked();
    return apply_device_config_locked();
}

void usrp_sweep_impl::close_device()
{
    d_rx_stream.reset();
    d_usrp.reset();
    d_device_open = false;
}

bool usrp_sweep_impl::wait_lo_locked()
{
    if (!d_usrp) {
        return false;
    }
    try {
        const auto names = d_usrp->get_rx_sensor_names(0);
        if (std::find(names.begin(), names.end(), "lo_locked") == names.end()) {
            return true;
        }
        const auto deadline =
            std::chrono::steady_clock::now() +
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(d_lo_setup_time));
        while (std::chrono::steady_clock::now() < deadline) {
            if (d_usrp->get_rx_sensor("lo_locked", 0).to_bool()) {
                return true;
            }
            if (!d_running.load(std::memory_order_relaxed)) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return d_usrp->get_rx_sensor("lo_locked", 0).to_bool();
    } catch (...) {
        return true;
    }
}

void usrp_sweep_impl::flush_rx_stream()
{
    if (!d_rx_stream) {
        return;
    }
    try {
        uhd::stream_cmd_t stop(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);
        stop.stream_now = true;
        d_rx_stream->issue_stream_cmd(stop);
    } catch (...) {
    }

    std::vector<std::complex<short>> drain(4096);
    uhd::rx_metadata_t md;
    for (int i = 0; i < 64; ++i) {
        try {
            const size_t n = d_rx_stream->recv(drain.data(), drain.size(), md, 0.01);
            if (n == 0 && md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
                break;
            }
        } catch (...) {
            break;
        }
    }
}

bool usrp_sweep_impl::receive_slot(std::vector<std::complex<short>>& iq,
                                   double settle_s,
                                   bool discard_burst)
{
    const size_t fft_size = d_fft_size;
    if (fft_size == 0 || !d_usrp) {
        return false;
    }

    // Reuse one RX streamer for the whole acquisition thread.
    // Creating/destroying a stream every slot + START_CONTINUOUS + discarding
    // ~100k samples caused LIBUSB_TRANSFER_OVERFLOW on USB2 (return -6).
    if (!d_rx_stream) {
        try {
            uhd::stream_args_t stream_args("sc16", d_wire);
            stream_args.channels = { 0 };
            d_rx_stream = d_usrp->get_rx_stream(stream_args);
        } catch (const std::exception& e) {
            std::cerr << "usrp_sweep: get_rx_stream failed: " << e.what()
                      << std::endl;
            return false;
        }
    }

    // Hardware settle via sleep (avoid large USB discard floods).
    if (settle_s > 0.0) {
        std::this_thread::sleep_for(std::chrono::duration<double>(settle_s));
    }

    auto recv_burst = [&](size_t nsamps, bool keep) -> bool {
        uhd::stream_cmd_t stream_cmd(
            uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE);
        stream_cmd.num_samps = nsamps;
        stream_cmd.stream_now = true;
        stream_cmd.time_spec = uhd::time_spec_t();
        try {
            d_rx_stream->issue_stream_cmd(stream_cmd);
        } catch (const std::exception& e) {
            std::cerr << "usrp_sweep: issue_stream_cmd failed: " << e.what()
                      << std::endl;
            return false;
        }

        if (keep) {
            iq.assign(nsamps, std::complex<short>(0, 0));
        }
        std::vector<std::complex<short>> buff(nsamps);
        uhd::rx_metadata_t md;
        const double timeout = 3.0;
        size_t got = 0;

        while (got < nsamps && d_running.load(std::memory_order_relaxed)) {
            size_t n = 0;
            try {
                n = d_rx_stream->recv(buff.data(), nsamps - got, md, timeout);
            } catch (const std::exception& e) {
                std::cerr << "usrp_sweep: recv exception: " << e.what()
                          << std::endl;
                flush_rx_stream();
                d_rx_stream.reset();
                return false;
            }

            if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
                flush_rx_stream();
                return false;
            }
            if (n == 0 || md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
                flush_rx_stream();
                d_rx_stream.reset();
                return false;
            }
            if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE) {
                flush_rx_stream();
                d_rx_stream.reset();
                return false;
            }

            if (keep) {
                std::memcpy(iq.data() + got,
                            buff.data(),
                            n * sizeof(std::complex<short>));
            }
            got += n;
        }
        return got >= nsamps;
    };

    // Optional short discard after retune (only when settle was long / first slot).
    if (discard_burst) {
        const size_t discard_n =
            std::min<size_t>(2048, std::max<size_t>(fft_size / 2, 512));
        if (!recv_burst(discard_n, false)) {
            return false;
        }
    }

    if (!recv_burst(fft_size, true)) {
        return false;
    }
    return true;
}

void usrp_sweep_impl::compute_slot_spectrum(
    const std::vector<std::complex<short>>& iq, std::vector<float>& mag_out)
{
    const size_t N = d_fft_size;
    mag_out.resize(N);
    for (size_t i = 0; i < N; ++i) {
        const float re =
            (static_cast<float>(iq[i].real()) / 32768.f) * d_hamming[i];
        const float im =
            (static_cast<float>(iq[i].imag()) / 32768.f) * d_hamming[i];
        d_fft_in[i][0] = re;
        d_fft_in[i][1] = im;
    }
    fftw_execute(d_fft_plan);

    // fftshift (same rotate as Panorama_FFT)
    size_t rotate_idx = N - (N >> 1);
    for (size_t i = 0; i < N; ++i) {
        const double re = d_fft_out[i][0];
        const double im = d_fft_out[i][1];
        mag_out[rotate_idx] = static_cast<float>(std::hypot(re, im));
        rotate_idx = (rotate_idx + 1) % N;
    }
}

std::vector<float> usrp_sweep_impl::stitch_slots(
    const std::vector<std::vector<float>>& slot_spectra,
    uint32_t total_points) const
{
    std::vector<float> out(total_points, 0.f);
    if (slot_spectra.empty() || total_points == 0) {
        return out;
    }

    const size_t num_slots = slot_spectra.size();
    const size_t fft_size = d_fft_size;
    const size_t overlap_value =
        static_cast<size_t>(fft_size * d_overlap / 2.0);
    size_t slot_offset = 0;

    for (size_t slot = 0; slot < num_slots; ++slot) {
        const auto& spectrum = slot_spectra[slot];
        size_t start_idx = (slot == 0) ? 0 : overlap_value;
        size_t stop_idx;
        if (slot == num_slots - 1) {
            if (fft_size - start_idx + slot_offset < total_points) {
                stop_idx = fft_size;
            } else {
                stop_idx = total_points - slot_offset + start_idx;
            }
        } else {
            stop_idx = fft_size - overlap_value;
        }
        if (stop_idx > spectrum.size()) {
            stop_idx = spectrum.size();
        }
        if (start_idx >= stop_idx) {
            continue;
        }

        for (size_t i = start_idx, j = 0; i < stop_idx; ++i, ++j) {
            const size_t dst = slot_offset + j;
            if (dst >= total_points) {
                break;
            }
            float v = spectrum[i];
            if (d_output_db) {
                constexpr float eps = 1e-20f;
                v = 10.f * std::log10(std::max(v, eps));
            }
            out[dst] = v;
        }
        slot_offset += (stop_idx - start_idx);
        if (slot_offset >= total_points) {
            break;
        }
    }
    return out;
}

void usrp_sweep_impl::acquisition_loop()
{
    while (d_running.load(std::memory_order_relaxed)) {
        if (d_reconfig_requested.exchange(false, std::memory_order_acq_rel)) {
            bool ok = false;
            {
                std::lock_guard<std::mutex> lock(d_param_mutex);
                if (d_start_fc == d_stop_fc) {
                    d_stop_fc = d_start_fc + 1e6;
                }
                rebuild_fft_locked();
                ok = apply_device_config_locked();
            }
            d_circ_buffer.clear();
            d_last_good.clear();
            d_display_ready.store(false, std::memory_order_relaxed);
            publish_status("prefill", 0, d_prefill_target);
            if (!ok) {
                std::cerr << "usrp_sweep: runtime reconfigure failed" << std::endl;
                break;
            }
            continue;
        }

        double sample_rate = 0;
        double start_fc = 0;
        double overlap = 0;
        size_t num_slots = 0;
        uint32_t total_points = 0;
        float norm_gain = 0.f;
        {
            std::lock_guard<std::mutex> lock(d_param_mutex);
            sample_rate = d_sample_rate;
            start_fc = d_start_fc;
            overlap = d_overlap;
            num_slots = d_num_slots.load(std::memory_order_relaxed);
            total_points = d_sweep_size.load(std::memory_order_relaxed);
            norm_gain = d_norm_gain;
        }

        if (total_points == 0 || num_slots == 0) {
            break;
        }

        try {
            if (norm_gain > 0.f && norm_gain <= 1.f) {
                d_usrp->set_normalized_rx_gain(norm_gain, 0);
            }
        } catch (...) {
        }

        const double half_fs = sample_rate / 2.0;
        const double freq_step = sample_rate * (1.0 - overlap);
        double rx_freq = start_fc + half_fs;

        std::vector<std::vector<float>> slot_spectra(num_slots);
        std::vector<std::complex<short>> iq;

        bool frame_ok = true;
        for (size_t slot = 0; slot < num_slots; ++slot) {
            if (!d_running.load(std::memory_order_relaxed)) {
                frame_ok = false;
                break;
            }
            if (d_reconfig_requested.load(std::memory_order_relaxed)) {
                frame_ok = false;
                break;
            }

            try {
                uhd::tune_request_t tune_req(rx_freq, 0.0);
                tune_req.args = uhd::device_addr_t("mode_n=integer");
                d_usrp->set_rx_freq(tune_req, 0);
            } catch (const std::exception& e) {
                std::cerr << "usrp_sweep: tune failed: " << e.what() << std::endl;
                frame_ok = false;
                break;
            }

            // Every hop: wait LO lock + settle (required for Ethernet USRPs;
            // also correct on USB B200 / B210).
            if (!wait_lo_locked()) {
                frame_ok = false;
                break;
            }

            const double settle_s = d_lock_time;
            if (!receive_slot(iq, settle_s, /*discard_burst=*/true)) {
                std::cerr << "usrp_sweep: receive_slot failed" << std::endl;
                frame_ok = false;
                break;
            }

            {
                std::lock_guard<std::mutex> lock(d_param_mutex);
                compute_slot_spectrum(iq, slot_spectra[slot]);
                // Qt pano EMA on linear |FFT| before stitch / optional dB.
                apply_slot_average_locked(slot, slot_spectra[slot]);
            }

            rx_freq += freq_step;
        }

        if (frame_ok && d_running.load(std::memory_order_relaxed)) {
            std::vector<float> panorama;
            {
                std::lock_guard<std::mutex> lock(d_param_mutex);
                panorama = stitch_slots(slot_spectra, total_points);
            }

            // Always push latest sweep (circ buffer overwrites when full).
            d_last_good = panorama;
            d_circ_buffer.push(std::move(panorama));
            const std::size_t filled = d_circ_buffer.size();
            if (!d_display_ready.load(std::memory_order_relaxed)) {
                publish_status("prefill", filled, d_prefill_target);
                if (filled >= d_prefill_target) {
                    d_display_ready.store(true, std::memory_order_release);
                    publish_status("ready", filled, d_prefill_target);
                }
            }
        } else if (d_running.load(std::memory_order_relaxed)) {
            // Keep Time Sink tagged so trigger does not fall back to scrap buffer.
            if (d_display_ready.load(std::memory_order_relaxed) &&
                !d_last_good.empty()) {
                d_circ_buffer.push(d_last_good);
            }
        }

        // No artificial inter-frame sleep: next sweep starts immediately so wide
        // spans remain as real-time as the LO hop rate allows.
    }
}

bool usrp_sweep_impl::start()
{
    if (d_running.load()) {
        return true;
    }
    d_reconfig_requested.store(false);
    d_circ_buffer.set_active(true);
    d_circ_buffer.clear();
    d_pending.clear();
    d_pending_offset = 0;
    d_last_good.clear();
    d_display_ready.store(false, std::memory_order_relaxed);
    if (!configure_device()) {
        throw std::runtime_error("usrp_sweep: failed to configure USRP device");
    }
    d_running.store(true);
    publish_status("prefill", 0, d_prefill_target);
    d_acq_thread = std::thread(&usrp_sweep_impl::acquisition_loop, this);
    return true;
}

bool usrp_sweep_impl::stop()
{
    if (!d_running.exchange(false)) {
        d_circ_buffer.set_active(false);
        close_device();
        return true;
    }
    d_circ_buffer.set_active(false);
    if (d_acq_thread.joinable()) {
        d_acq_thread.join();
    }
    // Publish after acq stopped; swallow teardown races on message ports.
    publish_status("ready", d_prefill_target, d_prefill_target);
    close_device();
    return true;
}

int usrp_sweep_impl::work(int noutput_items,
                          gr_vector_const_void_star& /*input_items*/,
                          gr_vector_void_star& output_items)
{
    auto* out = static_cast<float*>(output_items[0]);
    std::int32_t* num_points_out = nullptr;
    if (output_items.size() > 1 && output_items[1] != nullptr) {
        num_points_out = static_cast<std::int32_t*>(output_items[1]);
    }
    const auto np =
        static_cast<std::int32_t>(d_sweep_size.load(std::memory_order_relaxed));

    if (!d_pending.empty() &&
        static_cast<std::int32_t>(d_pending.size()) != np) {
        d_pending.clear();
        d_pending_offset = 0;
    }

    int produced = 0;

    while (produced < noutput_items) {
        if (d_pending_offset < d_pending.size()) {
            const int available =
                static_cast<int>(d_pending.size() - d_pending_offset);
            const int to_copy = std::min(available, noutput_items - produced);
            std::memcpy(out + produced,
                        d_pending.data() + d_pending_offset,
                        static_cast<std::size_t>(to_copy) * sizeof(float));
            if (num_points_out != nullptr) {
                for (int i = 0; i < to_copy; ++i) {
                    num_points_out[produced + i] = np;
                }
            }
            d_pending_offset += static_cast<std::size_t>(to_copy);
            produced += to_copy;
            if (d_pending_offset >= d_pending.size()) {
                d_pending.clear();
                d_pending_offset = 0;
            }
            continue;
        }

        if (!d_running.load(std::memory_order_relaxed)) {
            break;
        }

        // Wait until the circular buffer is completely full, then stream.
        // Until then return 0 so the Time Sink does not show a half-ready pipeline.
        if (!d_display_ready.load(std::memory_order_acquire)) {
            break;
        }

        std::vector<float> sweep;
        // Always show the newest complete sweep (drop stale backlog).
        if (!d_circ_buffer.pop_latest(sweep, std::chrono::milliseconds(100))) {
            break;
        }

        message_port_pub(
            pmt::mp("num_points"),
            pmt::cons(pmt::mp("num_points"), pmt::from_long(np)));

        double start_fc = 0.0;
        {
            std::lock_guard<std::mutex> lock(d_param_mutex);
            start_fc = d_start_fc;
        }
        add_item_tag(0,
                     nitems_written(0) + produced,
                     pmt::mp("sweep_start"),
                     pmt::from_double(start_fc));
        if (num_points_out != nullptr) {
            add_item_tag(1,
                         nitems_written(1) + produced,
                         pmt::mp("num_points"),
                         pmt::from_long(np));
        }

        d_pending = std::move(sweep);
        d_pending_offset = 0;
    }

    if (produced == 0 && d_running.load(std::memory_order_relaxed)) {
        return 0;
    }
    return produced;
}

void usrp_sweep_impl::set_args(const std::string& args)
{
    {
        std::lock_guard<std::mutex> lock(d_param_mutex);
        if (d_args == args) {
            return;
        }
        d_args = args;
    }
    // Device args require full restart; request reconfigure for geometry only.
    request_reconfigure();
}

void usrp_sweep_impl::set_rx_subdev(const std::string& rx_subdev)
{
    {
        std::lock_guard<std::mutex> lock(d_param_mutex);
        if (d_rx_subdev == rx_subdev) {
            return;
        }
        d_rx_subdev = rx_subdev;
    }
    request_reconfigure();
}

void usrp_sweep_impl::set_antenna(const std::string& antenna)
{
    {
        std::lock_guard<std::mutex> lock(d_param_mutex);
        if (d_antenna == antenna) {
            return;
        }
        d_antenna = antenna;
    }
    request_reconfigure();
}

void usrp_sweep_impl::set_sample_rate(double sample_rate)
{
    {
        std::lock_guard<std::mutex> lock(d_param_mutex);
        if (d_sample_rate == sample_rate) {
            return;
        }
        d_sample_rate = sample_rate;
    }
    request_reconfigure();
}

void usrp_sweep_impl::set_start_fc(double start_fc)
{
    {
        std::lock_guard<std::mutex> lock(d_param_mutex);
        if (d_start_fc == start_fc) {
            return;
        }
        d_start_fc = start_fc;
    }
    request_reconfigure();
}

void usrp_sweep_impl::set_stop_fc(double stop_fc)
{
    {
        std::lock_guard<std::mutex> lock(d_param_mutex);
        if (d_stop_fc == stop_fc) {
            return;
        }
        d_stop_fc = stop_fc;
    }
    request_reconfigure();
}

void usrp_sweep_impl::set_norm_gain(float norm_gain)
{
    {
        std::lock_guard<std::mutex> lock(d_param_mutex);
        if (d_norm_gain == norm_gain) {
            return;
        }
        d_norm_gain = norm_gain;
    }
    request_reconfigure();
}

void usrp_sweep_impl::set_bandwidth(float bandwidth)
{
    {
        std::lock_guard<std::mutex> lock(d_param_mutex);
        if (d_bandwidth == bandwidth) {
            return;
        }
        d_bandwidth = bandwidth;
    }
    request_reconfigure();
}

void usrp_sweep_impl::set_fft_size(size_t fft_size)
{
    if (fft_size == 0) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(d_param_mutex);
        if (d_fft_size == fft_size) {
            return;
        }
        d_fft_size = fft_size;
    }
    request_reconfigure();
}

void usrp_sweep_impl::set_overlap(double overlap)
{
    const double o = clamp_overlap(overlap);
    {
        std::lock_guard<std::mutex> lock(d_param_mutex);
        if (d_overlap == o) {
            return;
        }
        d_overlap = o;
    }
    request_reconfigure();
}

void usrp_sweep_impl::set_lock_time(double lock_time)
{
    {
        std::lock_guard<std::mutex> lock(d_param_mutex);
        if (d_lock_time == lock_time) {
            return;
        }
        d_lock_time = lock_time;
    }
    request_reconfigure();
}

void usrp_sweep_impl::set_output_db(bool output_db)
{
    {
        std::lock_guard<std::mutex> lock(d_param_mutex);
        if (d_output_db == output_db) {
            return;
        }
        d_output_db = output_db;
    }
    request_reconfigure();
}

void usrp_sweep_impl::set_average_alpha(float average_alpha)
{
    const float a = clamp_average_alpha(average_alpha);
    {
        std::lock_guard<std::mutex> lock(d_param_mutex);
        if (d_average_alpha == a) {
            return;
        }
        d_average_alpha = a;
        d_avg_reset.store(true, std::memory_order_relaxed);
    }
}

} /* namespace usrp_sweep */
} /* namespace gr */
