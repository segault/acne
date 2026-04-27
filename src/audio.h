#pragma once

#include "graph.h"
#include "simulate.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

struct AudioOptions {
    int sample_rate = 44100;
    std::size_t chunk_size = 1024;
};

struct PcmBuffer {
    int sample_rate = 44100;
    std::vector<float> samples;
};

class AudioOutput {
public:
    virtual ~AudioOutput() = default;
    virtual void begin(int sample_rate) = 0;
    virtual void write_samples(const std::vector<float> &samples) = 0;
    virtual void end() = 0;
};

class WavFileOutput : public AudioOutput {
public:
    explicit WavFileOutput(std::string path);
    void begin(int sample_rate) override;
    void write_samples(const std::vector<float> &samples) override;
    void end() override;

private:
    std::string path_;
    int sample_rate_ = 44100;
    std::vector<float> samples_;
};

class SystemAudioOutput : public AudioOutput {
public:
    SystemAudioOutput();
    ~SystemAudioOutput() override;

    void begin(int sample_rate) override;
    void write_samples(const std::vector<float> &samples) override;
    void end() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

PcmBuffer render_trace_audio(const CircuitGraph &graph, const SimulationTrace &trace,
                             const AudioOptions &options);
void stream_audio(StreamingSimulator &simulator, double duration_s, const CircuitGraph &graph,
                  const AudioOptions &options, AudioOutput &output,
                  const std::atomic_bool *stop_flag = nullptr);
