#pragma once

#include "ast.h"

#include <cstdint>
#include <string>
#include <vector>

using NodeId = std::uint32_t;
using ComponentId = std::uint32_t;

enum class ComponentKind {
    Resistor,
    Capacitor,
    Inductor,
    VoltageSource,
};

struct Node {
    NodeId id = 0;
    std::string name;
    bool is_ground = false;
};

struct Component {
    ComponentId id = 0;
    ComponentKind kind = ComponentKind::Resistor;
    NodeId a = 0;
    NodeId b = 0;
    double value = 0.0;
    double state_voltage = 0.0;
    double state_current = 0.0;
    std::string name;
};

struct SourceConfig {
    SourceMode mode = SourceMode::Dc;
    double amplitude = 0.0;
    double frequency_hz = 60.0;
    double phase_rad = 0.0;
    double dc_offset = 0.0;
};

struct CircuitGraph {
    std::vector<Node> nodes;
    std::vector<Component> components;
    NodeId ground = 0;
    NodeId input = 0;
    NodeId output = 0;
    SourceConfig source;
};
