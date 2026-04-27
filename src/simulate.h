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

struct SimulationState {
    double time_s = 0.0;
    std::vector<double> last_solution;
    std::vector<double> component_voltages;
    std::vector<double> component_currents;
};

class StreamingSimulator {
public:
    StreamingSimulator(const CircuitGraph &graph, const SimulationOptions &options);

    void reset();
    Sample step();
    const SimulationState &state() const;

private:
    CircuitGraph graph_;
    SimulationOptions options_;
    SimulationState state_;
    std::vector<int> node_index_;
    std::vector<int> component_current_index_;
    int system_size_ = 0;
    bool transient_ = false;
    bool first_step_ = true;
};

SimulationTrace collect_trace(StreamingSimulator &simulator, double t_stop);
SimulationTrace simulate_circuit(const CircuitGraph &graph, const SimulationOptions &options);
