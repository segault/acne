#include "simulate.h"

#include "linear.h"

#include <cmath>
#include <stdexcept>
#include <vector>

namespace {

double source_value(const SourceConfig &source, double time_s) {
    if (source.mode == SourceMode::Ac) {
        return source.dc_offset +
               source.amplitude *
                   std::sin(2.0 * M_PI * source.frequency_hz * time_s + source.phase_rad);
    }
    return source.dc_offset + source.amplitude;
}

double voltage_at(const std::vector<double> &solution, const std::vector<int> &node_index,
                  NodeId node) {
    if (node >= node_index.size()) {
        return 0.0;
    }
    const int index = node_index[node];
    if (index < 0) {
        return 0.0;
    }
    return solution[static_cast<std::size_t>(index)];
}

void stamp_conductance(std::vector<std::vector<double>> &matrix,
                       const std::vector<int> &node_index, NodeId a, NodeId b,
                       double conductance) {
    const int ia = node_index[a];
    const int ib = node_index[b];

    if (ia >= 0) {
        matrix[static_cast<std::size_t>(ia)][static_cast<std::size_t>(ia)] += conductance;
    }
    if (ib >= 0) {
        matrix[static_cast<std::size_t>(ib)][static_cast<std::size_t>(ib)] += conductance;
    }
    if (ia >= 0 && ib >= 0) {
        matrix[static_cast<std::size_t>(ia)][static_cast<std::size_t>(ib)] -= conductance;
        matrix[static_cast<std::size_t>(ib)][static_cast<std::size_t>(ia)] -= conductance;
    }
}

void stamp_rhs_current(std::vector<double> &rhs, const std::vector<int> &node_index, NodeId a,
                       NodeId b, double current_from_a_to_b) {
    const int ia = node_index[a];
    const int ib = node_index[b];

    if (ia >= 0) {
        rhs[static_cast<std::size_t>(ia)] -= current_from_a_to_b;
    }
    if (ib >= 0) {
        rhs[static_cast<std::size_t>(ib)] += current_from_a_to_b;
    }
}

}  // namespace

StreamingSimulator::StreamingSimulator(const CircuitGraph &graph,
                                       const SimulationOptions &options)
    : graph_(graph), options_(options) {
    if (graph_.nodes.empty()) {
        throw std::runtime_error("Cannot simulate an empty graph");
    }

    transient_ = options_.dt > 0.0 && options_.t_stop > 0.0;
    node_index_.assign(graph_.nodes.size(), -1);
    component_current_index_.assign(graph_.components.size(), -1);

    int next_index = 0;
    for (const Node &node : graph_.nodes) {
        if (node.is_ground) {
            continue;
        }
        node_index_[node.id] = next_index++;
    }

    for (const Component &component : graph_.components) {
        if (component.kind == ComponentKind::VoltageSource ||
            component.kind == ComponentKind::Inductor) {
            component_current_index_[component.id] = next_index++;
        }
    }

    system_size_ = next_index;
    reset();
}

void StreamingSimulator::reset() {
    state_.time_s = 0.0;
    state_.last_solution.assign(static_cast<std::size_t>(system_size_), 0.0);
    state_.component_voltages.assign(graph_.components.size(), 0.0);
    state_.component_currents.assign(graph_.components.size(), 0.0);
    first_step_ = true;
}

Sample StreamingSimulator::step() {
    std::vector<std::vector<double>> matrix(
        static_cast<std::size_t>(system_size_),
        std::vector<double>(static_cast<std::size_t>(system_size_), 0.0));
    std::vector<double> rhs(static_cast<std::size_t>(system_size_), 0.0);

    for (const Component &component : graph_.components) {
        switch (component.kind) {
            case ComponentKind::Resistor: {
                if (component.value == 0.0) {
                    throw std::runtime_error("Resistor value cannot be zero");
                }
                const double conductance = 1.0 / component.value;
                stamp_conductance(matrix, node_index_, component.a, component.b, conductance);
                break;
            }
            case ComponentKind::Capacitor: {
                if (!transient_) {
                    break;
                }
                const double conductance = component.value / options_.dt;
                stamp_conductance(matrix, node_index_, component.a, component.b, conductance);
                const double history_current =
                    conductance * state_.component_voltages[component.id];
                stamp_rhs_current(rhs, node_index_, component.a, component.b, -history_current);
                break;
            }
            case ComponentKind::Inductor: {
                if (!transient_) {
                    stamp_conductance(matrix, node_index_, component.a, component.b, 1e9);
                    break;
                }

                const int ia = node_index_[component.a];
                const int ib = node_index_[component.b];
                const int k = component_current_index_[component.id];
                if (ia >= 0) {
                    matrix[static_cast<std::size_t>(ia)][static_cast<std::size_t>(k)] += 1.0;
                    matrix[static_cast<std::size_t>(k)][static_cast<std::size_t>(ia)] += 1.0;
                }
                if (ib >= 0) {
                    matrix[static_cast<std::size_t>(ib)][static_cast<std::size_t>(k)] -= 1.0;
                    matrix[static_cast<std::size_t>(k)][static_cast<std::size_t>(ib)] -= 1.0;
                }
                matrix[static_cast<std::size_t>(k)][static_cast<std::size_t>(k)] -=
                    component.value / options_.dt;
                rhs[static_cast<std::size_t>(k)] -=
                    (component.value / options_.dt) * state_.component_currents[component.id];
                break;
            }
            case ComponentKind::VoltageSource: {
                const int ia = node_index_[component.a];
                const int ib = node_index_[component.b];
                const int k = component_current_index_[component.id];
                if (ia >= 0) {
                    matrix[static_cast<std::size_t>(ia)][static_cast<std::size_t>(k)] += 1.0;
                    matrix[static_cast<std::size_t>(k)][static_cast<std::size_t>(ia)] += 1.0;
                }
                if (ib >= 0) {
                    matrix[static_cast<std::size_t>(ib)][static_cast<std::size_t>(k)] -= 1.0;
                    matrix[static_cast<std::size_t>(k)][static_cast<std::size_t>(ib)] -= 1.0;
                }
                rhs[static_cast<std::size_t>(k)] += source_value(graph_.source, state_.time_s);
                break;
            }
        }
    }

    state_.last_solution = solve_linear_system(matrix, rhs);

    Sample sample;
    sample.time_s = state_.time_s;
    sample.node_voltages.resize(graph_.nodes.size(), 0.0);
    sample.branch_currents.resize(graph_.components.size(), 0.0);

    for (const Node &node : graph_.nodes) {
        sample.node_voltages[node.id] = voltage_at(state_.last_solution, node_index_, node.id);
    }

    for (const Component &component : graph_.components) {
        double current = 0.0;
        switch (component.kind) {
            case ComponentKind::Resistor: {
                const double va = sample.node_voltages[component.a];
                const double vb = sample.node_voltages[component.b];
                current = (va - vb) / component.value;
                break;
            }
            case ComponentKind::Capacitor: {
                if (transient_) {
                    const double va = sample.node_voltages[component.a];
                    const double vb = sample.node_voltages[component.b];
                    const double voltage = va - vb;
                    current = component.value *
                              (voltage - state_.component_voltages[component.id]) / options_.dt;
                    state_.component_voltages[component.id] = voltage;
                }
                break;
            }
            case ComponentKind::Inductor: {
                if (transient_) {
                    current = state_.last_solution[static_cast<std::size_t>(
                        component_current_index_[component.id])];
                    state_.component_currents[component.id] = current;
                } else {
                    const double va = sample.node_voltages[component.a];
                    const double vb = sample.node_voltages[component.b];
                    current = (va - vb) * 1e9;
                }
                break;
            }
            case ComponentKind::VoltageSource: {
                current = state_.last_solution[static_cast<std::size_t>(
                    component_current_index_[component.id])];
                break;
            }
        }
        sample.branch_currents[component.id] = current;
        state_.component_currents[component.id] = current;
    }

    if (transient_) {
        state_.time_s += options_.dt;
    } else if (first_step_) {
        first_step_ = false;
    }

    return sample;
}

const SimulationState &StreamingSimulator::state() const {
    return state_;
}

SimulationTrace collect_trace(StreamingSimulator &simulator, double t_stop) {
    SimulationTrace trace;
    if (t_stop <= 0.0) {
        trace.samples.push_back(simulator.step());
        return trace;
    }

    const SimulationState &initial_state = simulator.state();
    double current_time = initial_state.time_s;
    while (current_time <= t_stop + 1e-12) {
        Sample sample = simulator.step();
        current_time = sample.time_s;
        trace.samples.push_back(std::move(sample));
        current_time = simulator.state().time_s;
    }
    return trace;
}

SimulationTrace simulate_circuit(const CircuitGraph &graph, const SimulationOptions &options) {
    StreamingSimulator simulator(graph, options);
    return collect_trace(simulator, options.t_stop);
}
