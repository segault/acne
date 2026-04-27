#pragma once

#include "graph.h"
#include "simulate.h"

#include <string>

struct AudioOptions {
    int sample_rate = 44100;
};

void write_trace_wav(const std::string &path, const CircuitGraph &graph,
                     const SimulationTrace &trace, const AudioOptions &options);
