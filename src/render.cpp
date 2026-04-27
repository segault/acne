#include "render.h"

#include "utah.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

struct AdjacencyEdge {
    ComponentId component_id = 0;
    NodeId other = 0;
};

struct Placement {
    std::vector<int> node_x;
    std::vector<int> rank;
    std::vector<NodeId> backbone_nodes;
};

class Canvas {
public:
    Canvas(int width, int height)
        : width_(std::max(1, width)),
          height_(std::max(1, height)),
          rows_(static_cast<std::size_t>(height_),
                std::vector<std::string>(static_cast<std::size_t>(width_), " ")) {}

    void set(int x, int y, const std::string &c) {
        if (!in_bounds(x, y)) {
            return;
        }

        std::string &cell = rows_[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
        if (cell == " " || cell == c) {
            cell = c;
            return;
        }

        if (cell == "o" && (c == "-" || c == "|" || c == "+")) {
            return;
        }
        if (c == "o") {
            cell = c;
            return;
        }

        if ((is_horizontal(cell) && is_vertical(c)) ||
            (is_vertical(cell) && is_horizontal(c)) ||
            cell == "+" || c == "+") {
            cell = "+";
            return;
        }

        if (cell == "+" && is_vertical(c)) {
            cell = "+";
            return;
        }

        cell = c;
    }

    void text(int x, int y, const std::string &text) {
        for (std::size_t i = 0; i < text.size(); ++i) {
            set(x + static_cast<int>(i), y, std::string(1, text[i]));
        }
    }

    void hline(int x0, int x1, int y, const std::string &c = "-") {
        if (x1 < x0) {
            std::swap(x0, x1);
        }
        for (int x = x0; x <= x1; ++x) {
            set(x, y, c);
        }
    }

    void vline(int x, int y0, int y1, const std::string &c = "|") {
        if (y1 < y0) {
            std::swap(y0, y1);
        }
        for (int y = y0; y <= y1; ++y) {
            set(x, y, c);
        }
    }

    std::vector<std::string> lines() const {
        std::vector<std::string> out;
        out.reserve(rows_.size());
        for (const auto &row : rows_) {
            std::string line;
            for (const std::string &cell : row) {
                line += cell;
            }
            out.push_back(std::move(line));
        }
        return out;
    }

private:
    static bool is_horizontal(const std::string &cell) {
        return cell == "-" || cell == "+" || cell == "/" || cell == "\\" ||
               cell == "~" || cell == "(" || cell == ")" || cell == "o" || cell == "O";
    }

    static bool is_vertical(const std::string &cell) {
        return cell == "|" || cell == "+" || cell == "/" || cell == "\\" ||
               cell == "~" || cell == "(" || cell == ")" || cell == "o" || cell == "O";
    }

    bool in_bounds(int x, int y) const {
        return x >= 0 && x < width_ && y >= 0 && y < height_;
    }

    int width_ = 0;
    int height_ = 0;
    std::vector<std::vector<std::string>> rows_;
};

std::vector<std::string> horizontal_pattern(ComponentKind kind) {
    switch (kind) {
        case ComponentKind::Resistor:
            return {"/", "\\", "/", "\\"};
        case ComponentKind::Capacitor:
            return {"|", " ", "|"};
        case ComponentKind::Inductor:
            return {"~", "~", "~", "~"};
        case ComponentKind::VoltageSource:
            return {"(", "V", ")"};
    }
    return {"?"};
}

std::vector<std::string> vertical_pattern(ComponentKind kind) {
    switch (kind) {
        case ComponentKind::Resistor:
            return {"/", "\\", "/", "\\"};
        case ComponentKind::Capacitor:
            return {"-", " ", "-"};
        case ComponentKind::Inductor:
            return {"~", "~", "~", "~"};
        case ComponentKind::VoltageSource:
            return {"O"};
    }
    return {"?"};
}

std::string component_prefix(ComponentKind kind) {
    switch (kind) {
        case ComponentKind::Resistor:
            return "R";
        case ComponentKind::Capacitor:
            return "C";
        case ComponentKind::Inductor:
            return "L";
        case ComponentKind::VoltageSource:
            return "V";
    }
    return "?";
}

std::string short_value(double value) {
    std::ostringstream out;
    out.precision(4);
    out << value;
    return out.str();
}

std::string node_name(const Node &node, const CircuitGraph &graph) {
    if (node.id == graph.input) {
        return "IN";
    }
    if (node.id == graph.output) {
        return "OUT";
    }
    if (node.is_ground) {
        return "GND";
    }
    return node.name;
}

std::string component_label(const Component &component) {
    return component_prefix(component.kind) + std::to_string(component.id) + " " +
           short_value(component.value);
}

std::vector<std::vector<AdjacencyEdge>> build_adjacency(const CircuitGraph &graph) {
    std::vector<std::vector<AdjacencyEdge>> adjacency(graph.nodes.size());
    for (const Component &component : graph.components) {
        adjacency[component.a].push_back({component.id, component.b});
        adjacency[component.b].push_back({component.id, component.a});
    }
    return adjacency;
}

std::vector<NodeId> find_backbone_nodes(const CircuitGraph &graph,
                                        const std::vector<std::vector<AdjacencyEdge>> &adjacency) {
    if (graph.input == graph.output) {
        return {graph.input};
    }

    std::vector<bool> visited(graph.nodes.size(), false);
    struct Parent {
        bool has_parent = false;
        NodeId node = 0;
    };
    std::vector<Parent> parent(graph.nodes.size());
    std::queue<NodeId> queue;
    visited[graph.input] = true;
    queue.push(graph.input);

    while (!queue.empty()) {
        const NodeId node = queue.front();
        queue.pop();
        if (node == graph.output) {
            break;
        }
        for (const AdjacencyEdge &edge : adjacency[node]) {
            if (edge.other == graph.ground) {
                continue;
            }
            const Component &component = graph.components[edge.component_id];
            if (component.kind == ComponentKind::VoltageSource) {
                continue;
            }
            if (visited[edge.other]) {
                continue;
            }
            visited[edge.other] = true;
            parent[edge.other] = {true, node};
            queue.push(edge.other);
        }
    }

    if (!visited[graph.output]) {
        return {graph.input, graph.output};
    }

    std::vector<NodeId> nodes;
    NodeId current = graph.output;
    nodes.push_back(current);
    while (current != graph.input) {
        current = parent[current].node;
        nodes.push_back(current);
    }
    std::reverse(nodes.begin(), nodes.end());
    return nodes;
}

Placement place_nodes(const CircuitGraph &graph,
                      const std::vector<std::vector<AdjacencyEdge>> &adjacency) {
    Placement placement;
    placement.node_x.assign(graph.nodes.size(), 0);
    placement.rank.assign(graph.nodes.size(), -1);
    placement.backbone_nodes = find_backbone_nodes(graph, adjacency);

    std::set<NodeId> backbone_set(placement.backbone_nodes.begin(), placement.backbone_nodes.end());
    std::map<int, std::vector<NodeId>> extras_by_rank;

    std::queue<NodeId> queue;
    placement.rank[graph.input] = 0;
    queue.push(graph.input);
    while (!queue.empty()) {
        const NodeId node = queue.front();
        queue.pop();
        for (const AdjacencyEdge &edge : adjacency[node]) {
            if (edge.other == graph.ground) {
                continue;
            }
            const Component &component = graph.components[edge.component_id];
            if (component.kind == ComponentKind::VoltageSource) {
                continue;
            }
            if (placement.rank[edge.other] != -1) {
                continue;
            }
            placement.rank[edge.other] = placement.rank[node] + 1;
            queue.push(edge.other);
        }
    }

    int max_rank = 0;
    for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
        if (graph.nodes[i].is_ground) {
            continue;
        }
        if (placement.rank[i] == -1) {
            placement.rank[i] = max_rank + 1;
        }
        max_rank = std::max(max_rank, placement.rank[i]);
    }

    for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
        if (graph.nodes[i].is_ground || backbone_set.count(static_cast<NodeId>(i)) != 0) {
            continue;
        }
        extras_by_rank[placement.rank[i]].push_back(static_cast<NodeId>(i));
    }

    const int start_x = 8;
    const int main_spacing = 18;
    const int extra_spacing = 14;
    std::map<int, int> rank_anchor_x;

    for (std::size_t i = 0; i < placement.backbone_nodes.size(); ++i) {
        const NodeId node = placement.backbone_nodes[i];
        const int x = start_x + static_cast<int>(i) * main_spacing;
        placement.node_x[node] = x;
        rank_anchor_x[placement.rank[node]] = x;
    }

    int fallback_x = start_x + static_cast<int>(placement.backbone_nodes.size()) * main_spacing;
    for (const auto &[rank, nodes] : extras_by_rank) {
        int anchor_x = fallback_x;
        const auto anchor = rank_anchor_x.find(rank);
        if (anchor != rank_anchor_x.end()) {
            anchor_x = anchor->second;
        } else if (!rank_anchor_x.empty()) {
            auto lower = rank_anchor_x.lower_bound(rank);
            if (lower == rank_anchor_x.end()) {
                anchor_x = rank_anchor_x.rbegin()->second + main_spacing;
            } else {
                anchor_x = lower->second;
            }
        }

        for (std::size_t i = 0; i < nodes.size(); ++i) {
            placement.node_x[nodes[i]] = anchor_x + static_cast<int>(i + 1) * extra_spacing;
        }
        fallback_x = std::max(fallback_x, anchor_x + static_cast<int>(nodes.size() + 1) * extra_spacing);
    }

    placement.node_x[graph.ground] = start_x;
    return placement;
}

void draw_ground(Canvas &canvas, int x, int y) {
    canvas.text(x - 1, y, "_|_");
}

void draw_component_horizontal(Canvas &canvas, int x0, int x1, int y,
                               const Component &component) {
    if (x1 < x0) {
        std::swap(x0, x1);
    }
    const std::vector<std::string> pattern = horizontal_pattern(component.kind);
    const int span = x1 - x0;
    if (span <= 2) {
        canvas.hline(x0, x1, y);
        return;
    }

    const int body_start = x0 + std::max(1, (span - static_cast<int>(pattern.size())) / 2);
    const int body_end = body_start + static_cast<int>(pattern.size()) - 1;
    canvas.hline(x0, body_start - 1, y);
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        canvas.set(body_start + static_cast<int>(i), y, pattern[i]);
    }
    canvas.hline(body_end + 1, x1, y);
    canvas.text(std::max(0, body_start - 1), y - 1, component_label(component));
}

void draw_component_vertical(Canvas &canvas, int x, int y0, int y1,
                             const Component &component) {
    if (y1 < y0) {
        std::swap(y0, y1);
    }
    const int mid = (y0 + y1) / 2;

    if (component.kind == ComponentKind::Capacitor) {
        const int top_plate = std::max(y0 + 1, mid);
        const int bottom_plate = std::min(y1 - 1, top_plate + 1);
        canvas.vline(x, y0, top_plate - 1);
        canvas.text(x - 1, top_plate, "---");
        canvas.text(x - 1, bottom_plate, "---");
        canvas.vline(x, bottom_plate + 1, y1);
        canvas.text(x + 3, top_plate, component_label(component));
        return;
    }

    if (component.kind == ComponentKind::VoltageSource) {
        const int body_y = mid;
        canvas.vline(x, y0, body_y - 1);
        canvas.set(x, body_y, "O");
        canvas.text(x + 2, body_y, component_label(component));
        canvas.text(x - 1, std::max(0, body_y - 1), "+");
        canvas.text(x - 1, std::min(y1, body_y + 1), "-");
        canvas.vline(x, body_y + 1, y1);
        return;
    }

    const std::vector<std::string> pattern = vertical_pattern(component.kind);
    const int body_size = static_cast<int>(pattern.size());
    const int body_start = std::max(y0 + 1, mid - body_size / 2);
    const int body_end = body_start + body_size - 1;
    canvas.vline(x, y0, body_start - 1);
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        canvas.set(x, body_start + static_cast<int>(i), pattern[i]);
    }
    canvas.vline(x, body_end + 1, y1);
    canvas.text(x + 2, std::max(0, body_start), component_label(component));
}

void draw_node(Canvas &canvas, int x, int y, const std::string &label, bool above) {
    canvas.set(x, y, "o");
    canvas.text(std::max(0, x - static_cast<int>(label.size()) / 2), above ? y - 1 : y + 1, label);
}

std::vector<std::string> build_lines(const CircuitGraph &graph, int width) {
    const std::vector<std::vector<AdjacencyEdge>> adjacency = build_adjacency(graph);
    const Placement placement = place_nodes(graph, adjacency);

    std::vector<Component> horizontal_components;
    std::vector<Component> shunt_components;
    for (const Component &component : graph.components) {
        if (component.a == graph.ground || component.b == graph.ground) {
            shunt_components.push_back(component);
        } else {
            horizontal_components.push_back(component);
        }
    }

    std::sort(horizontal_components.begin(), horizontal_components.end(),
              [&](const Component &lhs, const Component &rhs) {
                  const int lhs_span = std::abs(placement.node_x[lhs.a] - placement.node_x[lhs.b]);
                  const int rhs_span = std::abs(placement.node_x[rhs.a] - placement.node_x[rhs.b]);
                  if (lhs_span != rhs_span) {
                      return lhs_span < rhs_span;
                  }
                  return lhs.id < rhs.id;
              });

    std::sort(shunt_components.begin(), shunt_components.end(),
              [&](const Component &lhs, const Component &rhs) {
                  NodeId lhs_node = lhs.a == graph.ground ? lhs.b : lhs.a;
                  NodeId rhs_node = rhs.a == graph.ground ? rhs.b : rhs.a;
                  if (placement.node_x[lhs_node] != placement.node_x[rhs_node]) {
                      return placement.node_x[lhs_node] < placement.node_x[rhs_node];
                  }
                  return lhs.id < rhs.id;
              });

    const int canvas_width = std::max(width, 140);
    const int header_y = 0;
    const int node_row_y = 7;
    const int base_track_y = 11;
    const int track_gap = 4;
    const int shunt_start_y = base_track_y + static_cast<int>(horizontal_components.size()) * track_gap + 3;
    const int shunt_gap = 4;
    const int ground_y = shunt_start_y + static_cast<int>(shunt_components.size()) * shunt_gap + 2;
    const int footer_y = ground_y + 3;
    Canvas canvas(canvas_width, footer_y + 3);

    canvas.text(0, header_y, "ACNE schematic view");
    canvas.text(0, header_y + 1, "q/ESC quit, j/k scroll");

    int max_x = 0;
    for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
        if (graph.nodes[i].is_ground) {
            continue;
        }
        max_x = std::max(max_x, placement.node_x[i]);
    }

    canvas.text(2, ground_y + 1, "GND");
    canvas.hline(6, max_x + 8, ground_y);

    for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
        const Node &node = graph.nodes[i];
        if (node.is_ground) {
            continue;
        }
        draw_node(canvas, placement.node_x[i], node_row_y, node_name(node, graph), true);
    }

    int track_index = 0;
    for (const Component &component : horizontal_components) {
        const int y = base_track_y + track_index * track_gap;
        const int xa = placement.node_x[component.a];
        const int xb = placement.node_x[component.b];
        canvas.vline(xa, node_row_y + 1, y - 1);
        canvas.vline(xb, node_row_y + 1, y - 1);
        draw_component_horizontal(canvas, std::min(xa, xb) + 1, std::max(xa, xb) - 1, y, component);
        canvas.set(xa, y, "+");
        canvas.set(xb, y, "+");
        ++track_index;
    }

    std::map<int, int> shunt_offset_per_x;
    for (const Component &component : shunt_components) {
        const NodeId node = component.a == graph.ground ? component.b : component.a;
        const int x = placement.node_x[node];
        int y0 = node_row_y + 1;
        const int offset = shunt_offset_per_x[x]++;
        const int y1 = ground_y - 2 - offset * 2;
        draw_component_vertical(canvas, x, y0, y1, component);
        draw_ground(canvas, x, ground_y + 1);
    }

    canvas.text(0, footer_y, "legend: horizontal tracks are placed by net span; ground connections share a rail");
    canvas.text(0, footer_y + 1, "non-ground nets are placed in columns, then two-terminal parts are routed between those columns");

    std::vector<std::string> lines = canvas.lines();
    for (std::string &line : lines) {
        while (!line.empty() && line.back() == ' ') {
            line.pop_back();
        }
    }
    return lines;
}

}  // namespace

void show_graph_view(const CircuitGraph &graph) {
    Tab tab;
    int cols = 80;
    int rows = 24;
    tab.get_window_size(&cols, &rows);

    const std::vector<std::string> lines = build_lines(graph, cols);
    const int max_rows = std::max(1, rows - 1);

    int start = 0;
    bool needs_redraw = true;
    while (true) {
        if (needs_redraw) {
            for (int i = 0; i < max_rows; ++i) {
                const int index = start + i;
                std::string line;
                if (index < static_cast<int>(lines.size())) {
                    line = lines[static_cast<std::size_t>(index)];
                }
                if (static_cast<int>(line.size()) > cols) {
                    line.resize(static_cast<std::size_t>(cols));
                }
                tab.add_line(line, cols);
                tab.add("\r\n");
            }
            tab.show();
            needs_redraw = false;
        }

        char c = '\0';
        tab.get_key(&c);
        if (c == 'q' || c == 'Q' || c == 27) {
            break;
        }
        if ((c == 'j' || c == 'J') && start + max_rows < static_cast<int>(lines.size())) {
            ++start;
            needs_redraw = true;
        }
        if ((c == 'k' || c == 'K') && start > 0) {
            --start;
            needs_redraw = true;
        }
    }
}
