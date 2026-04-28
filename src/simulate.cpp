#include "simulate.h"

#include "linear.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace {

constexpr double kThermalVoltage = 0.02585;
constexpr double kSaturationCurrent = 1e-12;
constexpr double kGmin = 1e-9;
constexpr double kMaxExpArg = 40.0;
constexpr double kNewtonTolerance = 1e-6;
constexpr int kMaxNewtonIterations = 48;

double safe_exp(double x) {
    return std::exp(std::clamp(x, -100.0, kMaxExpArg));
}

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

double diode_current(double vd) {
    return kSaturationCurrent * (safe_exp(vd / kThermalVoltage) - 1.0);
}

double diode_conductance(double vd) {
    return (kSaturationCurrent / kThermalVoltage) * safe_exp(vd / kThermalVoltage) + kGmin;
}

struct TerminalCurrents {
    double ic = 0.0;
    double ib = 0.0;
    double ie = 0.0;
};

TerminalCurrents bjt_currents(bool npn, double beta_f, double beta_r, double vc, double vb,
                              double ve) {
    const double alpha_f = beta_f / (beta_f + 1.0);
    const double alpha_r = beta_r / (beta_r + 1.0);

    const double vbe = npn ? (vb - ve) : (ve - vb);
    const double vbc = npn ? (vb - vc) : (vc - vb);

    const double ifwd = kSaturationCurrent * (safe_exp(vbe / kThermalVoltage) - 1.0);
    const double irev = kSaturationCurrent * (safe_exp(vbc / kThermalVoltage) - 1.0);

    TerminalCurrents out;
    if (npn) {
        out.ic = alpha_f * ifwd - irev;
        out.ie = -(ifwd - alpha_r * irev);
    } else {
        out.ic = -(alpha_f * ifwd - irev);
        out.ie = ifwd - alpha_r * irev;
    }
    out.ib = -(out.ic + out.ie);
    return out;
}

void add_node_current(std::vector<double> &residual, const std::vector<int> &node_index, NodeId n,
                      double current_leaving) {
    const int i = node_index[n];
    if (i >= 0) {
        residual[static_cast<std::size_t>(i)] += current_leaving;
    }
}

void add_jacobian(std::vector<std::vector<double>> &jacobian, const std::vector<int> &node_index,
                  NodeId row_node, NodeId col_node, double value) {
    const int r = node_index[row_node];
    const int c = node_index[col_node];
    if (r >= 0 && c >= 0) {
        jacobian[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] += value;
    }
}

void stamp_diode_nonlinear(std::vector<double> &residual,
                           std::vector<std::vector<double>> &jacobian,
                           const std::vector<int> &node_index, const Component &component,
                           const std::vector<double> &x) {
    const double va = voltage_at(x, node_index, component.a);
    const double vb = voltage_at(x, node_index, component.b);
    const double vd = va - vb;
    const double i = diode_current(vd);
    const double g = diode_conductance(vd);

    add_node_current(residual, node_index, component.a, i);
    add_node_current(residual, node_index, component.b, -i);

    add_jacobian(jacobian, node_index, component.a, component.a, g);
    add_jacobian(jacobian, node_index, component.a, component.b, -g);
    add_jacobian(jacobian, node_index, component.b, component.a, -g);
    add_jacobian(jacobian, node_index, component.b, component.b, g);
}

void stamp_bjt_nonlinear(std::vector<double> &residual,
                         std::vector<std::vector<double>> &jacobian,
                         const std::vector<int> &node_index, const Component &component,
                         const std::vector<double> &x) {
    const bool npn = component.kind == ComponentKind::NpnTransistor;
    const double beta_f = component.value > 0.0 ? component.value : 100.0;
    const double beta_r = 1.0;

    const NodeId c = component.a;
    const NodeId b = component.b;
    const NodeId e = component.c;

    const double vc = voltage_at(x, node_index, c);
    const double vb = voltage_at(x, node_index, b);
    const double ve = voltage_at(x, node_index, e);

    const TerminalCurrents base = bjt_currents(npn, beta_f, beta_r, vc, vb, ve);
    add_node_current(residual, node_index, c, base.ic);
    add_node_current(residual, node_index, b, base.ib);
    add_node_current(residual, node_index, e, base.ie);

    constexpr double h = 1e-5;
    for (NodeId node : {c, b, e}) {
        std::vector<double> perturbed = x;
        const int idx = node_index[node];
        if (idx < 0) {
            continue;
        }
        perturbed[static_cast<std::size_t>(idx)] += h;
        const TerminalCurrents p = bjt_currents(
            npn, beta_f, beta_r, voltage_at(perturbed, node_index, c),
            voltage_at(perturbed, node_index, b), voltage_at(perturbed, node_index, e));

        add_jacobian(jacobian, node_index, c, node, (p.ic - base.ic) / h);
        add_jacobian(jacobian, node_index, b, node, (p.ib - base.ib) / h);
        add_jacobian(jacobian, node_index, e, node, (p.ie - base.ie) / h);
    }
}

double max_abs(const std::vector<double> &v) {
    double m = 0.0;
    for (double x : v) {
        m = std::max(m, std::abs(x));
    }
    return m;
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
            component.kind == ComponentKind::InputSource ||
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
    std::vector<double> x = state_.last_solution;
    if (x.empty()) {
        x.assign(static_cast<std::size_t>(system_size_), 0.0);
    }

    for (int iteration = 0; iteration < kMaxNewtonIterations; ++iteration) {
        std::vector<std::vector<double>> jacobian(
            static_cast<std::size_t>(system_size_),
            std::vector<double>(static_cast<std::size_t>(system_size_), 0.0));
        std::vector<double> rhs(static_cast<std::size_t>(system_size_), 0.0);

        for (const Component &component : graph_.components) {
            switch (component.kind) {
                case ComponentKind::Resistor: {
                    if (component.value == 0.0) {
                        throw std::runtime_error("Resistor value cannot be zero");
                    }
                    stamp_conductance(jacobian, node_index_, component.a, component.b,
                                      1.0 / component.value);
                    break;
                }
                case ComponentKind::Capacitor: {
                    if (!transient_) {
                        break;
                    }
                    const double g = component.value / options_.dt;
                    stamp_conductance(jacobian, node_index_, component.a, component.b, g);
                    stamp_rhs_current(rhs, node_index_, component.a, component.b,
                                      -g * state_.component_voltages[component.id]);
                    break;
                }
                case ComponentKind::Inductor: {
                    if (!transient_) {
                        stamp_conductance(jacobian, node_index_, component.a, component.b, 1e9);
                        break;
                    }
                    const int ia = node_index_[component.a];
                    const int ib = node_index_[component.b];
                    const int k = component_current_index_[component.id];
                    if (ia >= 0) {
                        jacobian[ia][k] += 1.0;
                        jacobian[k][ia] += 1.0;
                    }
                    if (ib >= 0) {
                        jacobian[ib][k] -= 1.0;
                        jacobian[k][ib] -= 1.0;
                    }
                    jacobian[k][k] -= component.value / options_.dt;
                    rhs[k] -= (component.value / options_.dt) *
                              state_.component_currents[component.id];
                    break;
                }
                case ComponentKind::VoltageSource:
                case ComponentKind::InputSource: {
                    const int ia = node_index_[component.a];
                    const int ib = node_index_[component.b];
                    const int k = component_current_index_[component.id];
                    if (ia >= 0) {
                        jacobian[ia][k] += 1.0;
                        jacobian[k][ia] += 1.0;
                    }
                    if (ib >= 0) {
                        jacobian[ib][k] -= 1.0;
                        jacobian[k][ib] -= 1.0;
                    }
                    rhs[k] += component.kind == ComponentKind::InputSource
                                  ? source_value(graph_.source, state_.time_s)
                                  : component.value;
                    break;
                }
                case ComponentKind::Diode:
                case ComponentKind::NpnTransistor:
                case ComponentKind::PnpTransistor:
                    break;
            }
        }

        std::vector<double> residual(static_cast<std::size_t>(system_size_), 0.0);
        for (std::size_t r = 0; r < jacobian.size(); ++r) {
            double sum = 0.0;
            for (std::size_t c = 0; c < jacobian[r].size(); ++c) {
                sum += jacobian[r][c] * x[c];
            }
            residual[r] = sum - rhs[r];
        }

        for (const Component &component : graph_.components) {
            if (component.kind == ComponentKind::Diode) {
                stamp_diode_nonlinear(residual, jacobian, node_index_, component, x);
            } else if (component.kind == ComponentKind::NpnTransistor ||
                       component.kind == ComponentKind::PnpTransistor) {
                stamp_bjt_nonlinear(residual, jacobian, node_index_, component, x);
            }
        }

        std::vector<double> neg_residual = residual;
        for (double &v : neg_residual) {
            v = -v;
        }
        const std::vector<double> delta = solve_linear_system(jacobian, neg_residual);
        const double residual_norm = max_abs(residual);
        const double delta_norm = max_abs(delta);

        double lambda = 1.0;
        std::vector<double> candidate = x;
        for (int line = 0; line < 8; ++line) {
            for (std::size_t i = 0; i < x.size(); ++i) {
                candidate[i] = x[i] + lambda * delta[i];
            }
            if (max_abs(candidate) < 1e6) {
                break;
            }
            lambda *= 0.5;
        }
        x = candidate;

        if (residual_norm < kNewtonTolerance && delta_norm < kNewtonTolerance) {
            break;
        }
    }

    state_.last_solution = x;

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
                current = (sample.node_voltages[component.a] - sample.node_voltages[component.b]) /
                          component.value;
                break;
            }
            case ComponentKind::Capacitor: {
                if (transient_) {
                    const double voltage =
                        sample.node_voltages[component.a] - sample.node_voltages[component.b];
                    current = component.value *
                              (voltage - state_.component_voltages[component.id]) / options_.dt;
                    state_.component_voltages[component.id] = voltage;
                }
                break;
            }
            case ComponentKind::Inductor: {
                current = transient_
                              ? state_.last_solution[component_current_index_[component.id]]
                              : (sample.node_voltages[component.a] -
                                 sample.node_voltages[component.b]) *
                                    1e9;
                break;
            }
            case ComponentKind::VoltageSource:
            case ComponentKind::InputSource: {
                current = state_.last_solution[component_current_index_[component.id]];
                break;
            }
            case ComponentKind::Diode: {
                current = diode_current(sample.node_voltages[component.a] -
                                        sample.node_voltages[component.b]);
                break;
            }
            case ComponentKind::NpnTransistor:
            case ComponentKind::PnpTransistor: {
                const TerminalCurrents t =
                    bjt_currents(component.kind == ComponentKind::NpnTransistor,
                                 component.value > 0.0 ? component.value : 100.0, 1.0,
                                 sample.node_voltages[component.a],
                                 sample.node_voltages[component.b],
                                 sample.node_voltages[component.c]);
                current = t.ic;
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

    double current_time = simulator.state().time_s;
    while (current_time <= t_stop + 1e-12) {
        trace.samples.push_back(simulator.step());
        current_time = simulator.state().time_s;
    }
    return trace;
}

SimulationTrace simulate_circuit(const CircuitGraph &graph, const SimulationOptions &options) {
    StreamingSimulator simulator(graph, options);
    return collect_trace(simulator, options.t_stop);
}
