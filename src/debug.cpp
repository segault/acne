#include "debug.h"

#include <iomanip>
#include <ostream>
#include <string>

namespace {

const char *component_kind_name(ComponentKind kind) {
    switch (kind) {
        case ComponentKind::Resistor:
            return "resistor";
        case ComponentKind::Capacitor:
            return "capacitor";
        case ComponentKind::Inductor:
            return "inductor";
        case ComponentKind::VoltageSource:
            return "voltage_source";
        case ComponentKind::InputSource:
            return "input_source";
        case ComponentKind::Diode:
            return "diode";
        case ComponentKind::NpnTransistor:
            return "npn_transistor";
        case ComponentKind::PnpTransistor:
            return "pnp_transistor";
    }
    return "unknown";
}

const char *source_mode_name(SourceMode mode) {
    switch (mode) {
        case SourceMode::Dc:
            return "dc";
        case SourceMode::Ac:
            return "ac";
    }
    return "unknown";
}

}  // namespace

void dump_graph(std::ostream &out, const CircuitGraph &graph) {
    out << "graph dump\n";
    out << "source mode: " << source_mode_name(graph.source.mode)
        << ", amplitude=" << graph.source.amplitude
        << ", frequency_hz=" << graph.source.frequency_hz
        << ", phase_rad=" << graph.source.phase_rad
        << ", dc_offset=" << graph.source.dc_offset << "\n";
    out << "ports: ground=n" << graph.ground << ", input=n" << graph.input
        << ", output=n" << graph.output << "\n";

    out << "nodes:\n";
    for (const Node &node : graph.nodes) {
        out << "  n" << node.id << " name=" << node.name;
        if (node.is_ground) {
            out << " ground";
        }
        if (node.id == graph.input) {
            out << " input";
        }
        if (node.id == graph.output) {
            out << " output";
        }
        out << "\n";
    }

    out << "components:\n";
    out << std::fixed << std::setprecision(6);
    for (const Component &component : graph.components) {
        out << "  c" << component.id << " kind=" << component_kind_name(component.kind)
            << " name=" << component.name
            << " a=n" << component.a
            << " b=n" << component.b
            << " value=" << component.value << "\n";
    }
}
