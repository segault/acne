#include "simulate.h"

#include "linear.h"

#include <cmath>
#include <stdexcept>
#include <unordered_map>

namespace {

double source_value(const SourceConfig &source, double time_s) {
    if (source.mode == SourceMode::Ac) {
        return source.dc_offset +
               source.amplitude *
                   std::sin(2.0 * M_PI * source.frequency_hz * time_s + source.phase_rad);
    }
    return source.dc_offset + source.amplitude;
}

double voltage_at(const std::vector<double> &solution,
                  const std::unordered_map<NodeId, int> &node_index, NodeId node) {
    const auto it = node_index.find(node);
    if (it == node_index.end()) {
        return 0.0;
    }
    return solution[it->second];
}

void stamp_conductance(std::vector<std::vector<double>> &matrix,
                       const std::unordered_map<NodeId, int> &node_index, NodeId a, NodeId b,
                       double conductance) {
    const auto ia = node_index.find(a);
    const auto ib = node_index.find(b);

    if (ia != node_index.end()) {
        matrix[ia->second][ia->second] += conductance;
    }
    if (ib != node_index.end()) {
        matrix[ib->second][ib->second] += conductance;
    }
    if (ia != node_index.end() && ib != node_index.end()) {
        matrix[ia->second][ib->second] -= conductance;
        matrix[ib->second][ia->second] -= conductance;
    }
}

void stamp_rhs_current(std::vector<double> &rhs,
                       const std::unordered_map<NodeId, int> &node_index, NodeId a, NodeId b,
                       double current_from_a_to_b) {
    const auto ia = node_index.find(a);
    const auto ib = node_index.find(b);

    if (ia != node_index.end()) {
        rhs[ia->second] -= current_from_a_to_b;
    }
    if (ib != node_index.end()) {
        rhs[ib->second] += current_from_a_to_b;
    }
}

struct RuntimeComponent {
    Component component;
    int current_index = -1;
};

}  // namespace

SimulationTrace simulate_circuit(const CircuitGraph &graph, const SimulationOptions &options) {
    if (graph.nodes.empty()) {
        throw std::runtime_error("Cannot simulate an empty graph");
    }

    std::unordered_map<NodeId, int> node_index;
    int next_index = 0;
    for (const Node &node : graph.nodes) {
        if (node.is_ground) {
            continue;
        }
        node_index[node.id] = next_index++;
    }

    std::vector<RuntimeComponent> components;
    components.reserve(graph.components.size());
    for (const Component &component : graph.components) {
        RuntimeComponent runtime;
        runtime.component = component;
        if (component.kind == ComponentKind::VoltageSource ||
            component.kind == ComponentKind::Inductor) {
            runtime.current_index = next_index++;
        }
        components.push_back(runtime);
    }

    SimulationTrace trace;
    const bool transient = options.dt > 0.0 && options.t_stop > 0.0;
    const int steps = transient ? static_cast<int>(std::floor(options.t_stop / options.dt)) + 1 : 1;

    for (int step = 0; step < steps; ++step) {
        const double time_s = transient ? step * options.dt : 0.0;
        std::vector<std::vector<double>> matrix(next_index,
                                                std::vector<double>(next_index, 0.0));
        std::vector<double> rhs(next_index, 0.0);

        for (const RuntimeComponent &runtime : components) {
            const Component &component = runtime.component;
            switch (component.kind) {
                case ComponentKind::Resistor: {
                    if (component.value == 0.0) {
                        throw std::runtime_error("Resistor value cannot be zero");
                    }
                    const double conductance = 1.0 / component.value;
                    stamp_conductance(matrix, node_index, component.a, component.b, conductance);
                    break;
                }
                case ComponentKind::Capacitor: {
                    if (!transient) {
                        break;
                    }
                    const double conductance = component.value / options.dt;
                    stamp_conductance(matrix, node_index, component.a, component.b, conductance);
                    const double history_current = conductance * component.state_voltage;
                    stamp_rhs_current(rhs, node_index, component.a, component.b,
                                      -history_current);
                    break;
                }
                case ComponentKind::Inductor: {
                    if (!transient) {
                        stamp_conductance(matrix, node_index, component.a, component.b, 1e9);
                        break;
                    }

                    const auto ia = node_index.find(component.a);
                    const auto ib = node_index.find(component.b);
                    const int k = runtime.current_index;
                    if (ia != node_index.end()) {
                        matrix[ia->second][k] += 1.0;
                        matrix[k][ia->second] += 1.0;
                    }
                    if (ib != node_index.end()) {
                        matrix[ib->second][k] -= 1.0;
                        matrix[k][ib->second] -= 1.0;
                    }
                    matrix[k][k] -= component.value / options.dt;
                    rhs[k] -= (component.value / options.dt) * component.state_current;
                    break;
                }
                case ComponentKind::VoltageSource: {
                    const auto ia = node_index.find(component.a);
                    const auto ib = node_index.find(component.b);
                    const int k = runtime.current_index;
                    if (ia != node_index.end()) {
                        matrix[ia->second][k] += 1.0;
                        matrix[k][ia->second] += 1.0;
                    }
                    if (ib != node_index.end()) {
                        matrix[ib->second][k] -= 1.0;
                        matrix[k][ib->second] -= 1.0;
                    }
                    rhs[k] += source_value(graph.source, time_s);
                    break;
                }
            }
        }

        const std::vector<double> solution = solve_linear_system(matrix, rhs);

        Sample sample;
        sample.time_s = time_s;
        sample.node_voltages.resize(graph.nodes.size(), 0.0);
        sample.branch_currents.resize(graph.components.size(), 0.0);

        for (const Node &node : graph.nodes) {
            sample.node_voltages[node.id] = voltage_at(solution, node_index, node.id);
        }

        for (std::size_t i = 0; i < components.size(); ++i) {
            RuntimeComponent &runtime = components[i];
            const Component &component = runtime.component;
            double current = 0.0;
            switch (component.kind) {
                case ComponentKind::Resistor: {
                    const double va = sample.node_voltages[component.a];
                    const double vb = sample.node_voltages[component.b];
                    current = (va - vb) / component.value;
                    break;
                }
                case ComponentKind::Capacitor: {
                    if (transient) {
                        const double va = sample.node_voltages[component.a];
                        const double vb = sample.node_voltages[component.b];
                        const double voltage = va - vb;
                        current = component.value * (voltage - component.state_voltage) / options.dt;
                        runtime.component.state_voltage = voltage;
                    }
                    break;
                }
                case ComponentKind::Inductor: {
                    if (transient) {
                        current = solution[runtime.current_index];
                        runtime.component.state_current = current;
                    } else {
                        const double va = sample.node_voltages[component.a];
                        const double vb = sample.node_voltages[component.b];
                        current = (va - vb) * 1e9;
                    }
                    break;
                }
                case ComponentKind::VoltageSource: {
                    current = solution[runtime.current_index];
                    break;
                }
            }
            sample.branch_currents[i] = current;
        }

        trace.samples.push_back(std::move(sample));
    }

    return trace;
}
