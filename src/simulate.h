#pragma once

#include "graph.h"

#include <vector>

struct SimulationOptions {
    double dt = 1.0 / 1000.0;
    double t_stop = 1.0 / 60.0;
};

struct Sample {
    double time_s = 0.0;
    std::vector<double> node_voltages;
    std::vector<double> branch_currents;
};

struct SimulationTrace {
    std::vector<Sample> samples;
};

SimulationTrace simulate_circuit(const CircuitGraph &graph, const SimulationOptions &options);
