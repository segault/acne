// hello
#include "utah.h"
#include "audio.h"
#include "debug.h"
#include "lexer.h"
#include "lower.h"
#include "parser.h"
#include "simulate.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

/*
code:
set ac
in = 5

cir lpf {
    gnd = cap(47000, in) + res(50, res(50, in));
    out = res(100, in);
}

out = lpf(in);

intermediate:



ultimate:


*/

int main(int argc, char *argv[]) {
    try {
        bool dump_requested = false;
        const char *wav_path = nullptr;
        const char *input_path = nullptr;

        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--dump-graph") {
                dump_requested = true;
                continue;
            }
            if (arg == "--wav") {
                if (i + 1 >= argc) {
                    throw std::runtime_error("expected a path after --wav");
                }
                wav_path = argv[++i];
                continue;
            }
            if (input_path == nullptr) {
                input_path = argv[i];
                continue;
            }
            throw std::runtime_error("unexpected extra argument: " + arg);
        }

        if (input_path == nullptr) {
            std::cerr << "usage: " << argv[0]
                      << " [--dump-graph] [--wav output.wav] <file.acne>\n";
            return 1;
        }

        std::ifstream input(input_path);
        if (!input) {
            throw std::runtime_error("failed to open input file");
        }

        std::stringstream buffer;
        buffer << input.rdbuf();

        Lexer lexer(buffer.str());
        Parser parser(lexer.tokenize());
        ProgramAst program = parser.parse_program();
        CircuitGraph graph = lower_program(program);

        if (dump_requested) {
            dump_graph(std::cerr, graph);
        }

        SimulationOptions options;
        if (graph.source.mode == SourceMode::Dc) {
            options.dt = 0.001;
            options.t_stop = 0.0;
        } else {
            options.dt = 1.0 / 2000.0;
            options.t_stop = 1.0 / graph.source.frequency_hz;
        }

        for (const Component &component : graph.components) {
            if (component.kind == ComponentKind::Capacitor ||
                component.kind == ComponentKind::Inductor) {
                options.dt = 1.0 / 5000.0;
                options.t_stop = 0.05;
                break;
            }
        }

        if (wav_path != nullptr) {
            options.t_stop = std::max(options.t_stop, 10.0);
        }

        const SimulationTrace trace = simulate_circuit(graph, options);
        if (wav_path != nullptr) {
            AudioOptions audio_options;
            write_trace_wav(wav_path, graph, trace, audio_options);
        }

        std::cout << "time_s,input_v,output_v\n";
        std::cout << std::fixed << std::setprecision(6);
        for (const Sample &sample : trace.samples) {
            std::cout << sample.time_s << ","
                      << sample.node_voltages[graph.input] << ","
                      << sample.node_voltages[graph.output] << "\n";
        }
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
