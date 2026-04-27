#include "audio.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace {

double interpolate_output_voltage(const CircuitGraph &graph, const SimulationTrace &trace,
                                  double time_s) {
    if (trace.samples.empty()) {
        return 0.0;
    }

    if (time_s <= trace.samples.front().time_s) {
        return trace.samples.front().node_voltages[graph.output];
    }
    if (time_s >= trace.samples.back().time_s) {
        return trace.samples.back().node_voltages[graph.output];
    }

    std::size_t hi = 1;
    while (hi < trace.samples.size() && trace.samples[hi].time_s < time_s) {
        ++hi;
    }
    const std::size_t lo = hi - 1;

    const Sample &a = trace.samples[lo];
    const Sample &b = trace.samples[hi];
    const double span = b.time_s - a.time_s;
    if (span <= 0.0) {
        return a.node_voltages[graph.output];
    }

    const double alpha = (time_s - a.time_s) / span;
    const double va = a.node_voltages[graph.output];
    const double vb = b.node_voltages[graph.output];
    return va + alpha * (vb - va);
}

void write_u16(std::ofstream &out, std::uint16_t value) {
    out.write(reinterpret_cast<const char *>(&value), sizeof(value));
}

void write_u32(std::ofstream &out, std::uint32_t value) {
    out.write(reinterpret_cast<const char *>(&value), sizeof(value));
}

}  // namespace

void write_trace_wav(const std::string &path, const CircuitGraph &graph,
                     const SimulationTrace &trace, const AudioOptions &options) {
    if (trace.samples.empty()) {
        throw std::runtime_error("cannot write WAV from an empty trace");
    }
    if (options.sample_rate <= 0) {
        throw std::runtime_error("audio sample rate must be positive");
    }

    const double duration_s =
        std::max(trace.samples.back().time_s, 1.0 / static_cast<double>(options.sample_rate));
    const std::size_t num_samples =
        static_cast<std::size_t>(duration_s * options.sample_rate) + 1;

    double peak = 0.0;
    for (const Sample &sample : trace.samples) {
        peak = std::max(peak, std::abs(sample.node_voltages[graph.output]));
    }
    if (peak < 1e-12) {
        peak = 1.0;
    }

    std::vector<std::int16_t> pcm;
    pcm.reserve(num_samples);
    for (std::size_t i = 0; i < num_samples; ++i) {
        const double t = static_cast<double>(i) / options.sample_rate;
        const double voltage = interpolate_output_voltage(graph, trace, t);
        const double normalized = std::clamp(voltage / peak, -1.0, 1.0);
        pcm.push_back(static_cast<std::int16_t>(normalized * 32767.0));
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("failed to open WAV output file: " + path);
    }

    const std::uint16_t num_channels = 1;
    const std::uint16_t bits_per_sample = 16;
    const std::uint32_t byte_rate =
        static_cast<std::uint32_t>(options.sample_rate * num_channels * bits_per_sample / 8);
    const std::uint16_t block_align =
        static_cast<std::uint16_t>(num_channels * bits_per_sample / 8);
    const std::uint32_t data_size =
        static_cast<std::uint32_t>(pcm.size() * sizeof(std::int16_t));
    const std::uint32_t riff_size = 36 + data_size;

    out.write("RIFF", 4);
    write_u32(out, riff_size);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    write_u32(out, 16);
    write_u16(out, 1);
    write_u16(out, num_channels);
    write_u32(out, static_cast<std::uint32_t>(options.sample_rate));
    write_u32(out, byte_rate);
    write_u16(out, block_align);
    write_u16(out, bits_per_sample);
    out.write("data", 4);
    write_u32(out, data_size);
    out.write(reinterpret_cast<const char *>(pcm.data()),
              static_cast<std::streamsize>(data_size));
}
