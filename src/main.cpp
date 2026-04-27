// hello
#include "utah.h"
#include "audio.h"
#include "debug.h"
#include "lexer.h"
#include "lower.h"
#include "parser.h"
#include "render.h"
#include "simulate.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <csignal>

namespace {

std::atomic_bool g_stop_audio = false;

void handle_sigint(int) {
    g_stop_audio.store(true);
}

}

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
        bool view_requested = false;
        bool play_requested = false;
        bool seconds_requested = false;
        double stream_seconds = -1.0;
        const char *wav_path = nullptr;
        const char *input_path = nullptr;

        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--dump-graph") {
                dump_requested = true;
                continue;
            }
            if (arg == "--view" || arg == "-v") {
                view_requested = true;
                continue;
            }
            if (arg == "--play") {
                play_requested = true;
                continue;
            }
            if (arg == "--seconds") {
                if (i + 1 >= argc) {
                    throw std::runtime_error("expected a value after --seconds");
                }
                seconds_requested = true;
                stream_seconds = std::stod(argv[++i]);
                if (stream_seconds <= 0.0) {
                    throw std::runtime_error("--seconds must be positive");
                }
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
                      << " [--dump-graph] [--view] [--play] [--seconds n] [--wav output.wav] <file.acne>\n";
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
        if (view_requested) {
            show_graph_view(graph);
            return 0;
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
            options.t_stop = std::max(options.t_stop, seconds_requested ? stream_seconds : 5.0);
        }
        if (play_requested) {
            options.t_stop = std::max(options.t_stop, seconds_requested ? stream_seconds : 5.0);
        }

        StreamingSimulator simulator(graph, options);
        if (play_requested) {
            AudioOptions audio_options;
            SystemAudioOutput live_output;
            g_stop_audio.store(false);
            const auto old_handler = std::signal(SIGINT, handle_sigint);
            const double play_duration = seconds_requested ? stream_seconds : -1.0;
            stream_audio(simulator, play_duration, graph, audio_options, live_output,
                         &g_stop_audio);
            std::signal(SIGINT, old_handler);
            if (wav_path == nullptr) {
                return 0;
            }
            simulator.reset();
        }

        const SimulationTrace trace = collect_trace(simulator, options.t_stop);
        if (wav_path != nullptr) {
            AudioOptions audio_options;
            WavFileOutput wav_output(wav_path);
            stream_audio(simulator, options.t_stop, graph, audio_options, wav_output);
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
