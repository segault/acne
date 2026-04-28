#include "lower.h"

#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {

struct LowerContext {
    std::unordered_map<std::string, NodeId> bindings;
    std::string prefix;
};

class Lowerer {
public:
    explicit Lowerer(const ProgramAst &program) : program_(program) {
        for (const CircuitAst &circuit : program_.circuits) {
            circuits_[circuit.name] = &circuit;
        }

        graph_.ground = add_named_node("gnd", true);
        graph_.input = add_named_node("in");
        graph_.output = add_named_node("out");

        if (program_.has_source_mode) {
            graph_.source.mode = program_.source_mode;
        }
        if (program_.has_input_value) {
            graph_.source.amplitude = program_.input_value;
        }

        add_component(ComponentKind::InputSource, graph_.input, graph_.ground,
                      graph_.source.amplitude, "vin");
    }

    CircuitGraph lower() {
        LowerContext root;
        root.bindings["gnd"] = graph_.ground;
        root.bindings["in"] = graph_.input;
        root.bindings["out"] = graph_.output;
        root.prefix = "root";

        for (const AssignAst &assign : program_.statements) {
            lower_assignment(assign, root);
        }

        return graph_;
    }

private:
    NodeId add_named_node(const std::string &name, bool is_ground = false) {
        const auto it = global_nodes_.find(name);
        if (it != global_nodes_.end()) {
            if (is_ground) {
                graph_.nodes[it->second].is_ground = true;
            }
            return it->second;
        }

        const NodeId id = static_cast<NodeId>(graph_.nodes.size());
        graph_.nodes.push_back({id, name, is_ground});
        global_nodes_[name] = id;
        return id;
    }

    NodeId create_temp_node(const std::string &prefix) {
        return add_named_node(prefix + ".n" + std::to_string(temp_node_counter_++));
    }

    ComponentId add_component(ComponentKind kind, NodeId a, NodeId b, double value,
                              const std::string &name, NodeId c = 0, double value2 = 0.0) {
        const ComponentId id = static_cast<ComponentId>(graph_.components.size());
        graph_.components.push_back({id, kind, a, b, c, value, value2, 0.0, 0.0, name});
        return id;
    }

    NodeId resolve_identifier(const std::string &name, const LowerContext &ctx) {
        const auto binding = ctx.bindings.find(name);
        if (binding != ctx.bindings.end()) {
            return binding->second;
        }
        return add_named_node(name);
    }

    void lower_assignment(const AssignAst &assign, const LowerContext &ctx) {
        const NodeId lhs = resolve_identifier(assign.lhs, ctx);
        lower_expression(lhs, assign.rhs, ctx);
    }

    void lower_expression(NodeId start, const Expr &expr, const LowerContext &ctx) {
        if (expr.kind == ExprKind::Parallel) {
            for (const Expr &branch : expr.args) {
                lower_expression(start, branch, ctx);
            }
            return;
        }

        if (expr.kind == ExprKind::Identifier) {
            const NodeId end = resolve_identifier(expr.text, ctx);
            add_component(ComponentKind::Resistor, start, end, 1e-9, "wire");
            return;
        }

        if (expr.kind != ExprKind::Call) {
            throw std::runtime_error("Unsupported expression in lowering");
        }

        if (expr.text == "res" || expr.text == "cap" || expr.text == "ind" ||
            expr.text == "vsrc" || expr.text == "dio" || expr.text == "npn" ||
            expr.text == "pnp") {
            lower_component_chain(start, expr, ctx);
            return;
        }

        lower_circuit_call(start, expr, ctx);
    }

    void lower_component_chain(NodeId start, const Expr &expr, const LowerContext &ctx) {
        if (expr.text == "dio") {
            if (expr.args.size() != 1 || expr.args[0].kind != ExprKind::Identifier) {
                throw std::runtime_error("Diode call 'dio' expects one node identifier argument");
            }
            const NodeId end = resolve_identifier(expr.args[0].text, ctx);
            add_component(ComponentKind::Diode, start, end, 0.0, "dio");
            return;
        }
        if (expr.text == "npn" || expr.text == "pnp") {
            if (expr.args.size() != 3 || expr.args[0].kind != ExprKind::Number ||
                expr.args[1].kind != ExprKind::Identifier ||
                expr.args[2].kind != ExprKind::Identifier) {
                throw std::runtime_error("Transistor call expects (beta, base, emitter)");
            }
            const ComponentKind kind =
                expr.text == "npn" ? ComponentKind::NpnTransistor : ComponentKind::PnpTransistor;
            const NodeId base = resolve_identifier(expr.args[1].text, ctx);
            const NodeId emitter = resolve_identifier(expr.args[2].text, ctx);
            add_component(kind, start, base, expr.args[0].number_value, expr.text, emitter);
            return;
        }

        if (expr.args.size() != 2) {
            throw std::runtime_error("Component call '" + expr.text + "' expects 2 arguments");
        }
        if (expr.args[0].kind != ExprKind::Number) {
            throw std::runtime_error("Component value for '" + expr.text + "' must be numeric");
        }

        const double value = expr.args[0].number_value;
        const Expr &next = expr.args[1];
        ComponentKind kind = ComponentKind::Resistor;
        if (expr.text == "cap") {
            kind = ComponentKind::Capacitor;
        } else if (expr.text == "ind") {
            kind = ComponentKind::Inductor;
        } else if (expr.text == "vsrc") {
            kind = ComponentKind::VoltageSource;
        }

        NodeId end = 0;
        if (next.kind == ExprKind::Identifier) {
            end = resolve_identifier(next.text, ctx);
        } else {
            end = create_temp_node(ctx.prefix);
        }

        add_component(kind, start, end, value, expr.text);

        if (next.kind == ExprKind::Identifier) {
            return;
        }
        if (next.kind == ExprKind::Call) {
            lower_expression(end, next, ctx);
            return;
        }

        throw std::runtime_error("Nested component expressions must terminate in a call or identifier");
    }

    void lower_circuit_call(NodeId output_node, const Expr &expr, const LowerContext &ctx) {
        const auto it = circuits_.find(expr.text);
        if (it == circuits_.end()) {
            throw std::runtime_error("Unknown circuit '" + expr.text + "'");
        }
        if (expr.args.size() != 1) {
            throw std::runtime_error("Circuit '" + expr.text + "' expects exactly 1 argument");
        }
        if (expr.args[0].kind != ExprKind::Identifier) {
            throw std::runtime_error("Circuit inputs currently must be node identifiers");
        }

        LowerContext child;
        child.bindings["gnd"] = graph_.ground;
        child.bindings["out"] = output_node;
        child.bindings["in"] = resolve_identifier(expr.args[0].text, ctx);
        child.prefix = ctx.prefix + "." + expr.text + std::to_string(circuit_instance_counter_++);

        for (const AssignAst &assign : it->second->body) {
            lower_assignment(assign, child);
        }
    }

    const ProgramAst &program_;
    CircuitGraph graph_;
    std::unordered_map<std::string, NodeId> global_nodes_;
    std::unordered_map<std::string, const CircuitAst *> circuits_;
    std::size_t temp_node_counter_ = 0;
    std::size_t circuit_instance_counter_ = 0;
};

}  // namespace

CircuitGraph lower_program(const ProgramAst &program) {
    Lowerer lowerer(program);
    return lowerer.lower();
}
