# ACNE Architecture

This document turns the language sketch in `src/main.cpp` into a practical backend architecture for an analogue-first circuit emulator. It also reflects the current implementation direction in the repository: parsing, lowering, graph construction, transient simulation, WAV export, and static terminal visualization.

## Goals

The core system should:

1. Parse `.acne` or plain text circuit files.
2. Lower the source program into an internal circuit graph.
3. Solve node voltages and branch currents with modified nodal analysis (MNA).
4. Support transient stepping for capacitors and inductors.
5. Expose samples that can be printed, logged, plotted, rendered to audio, or visualized in the terminal UI.

## Language Shape

The current notation uses three layers:

1. Global simulation setup.
2. Circuit definitions (`cir name { ... }`).
3. Circuit instantiation and signal routing.

Example:

```txt
set ac
in = 5

cir lpf {
    gnd = cap(47000, in) + res(50, res(50, in));
    out = res(100, in);
}

out = lpf(in);
```

Minimal semantic rules:

- `in`, `out`, and `gnd` are reserved names inside a circuit body.
- `set ac` selects an AC source model for the top-level input signal.
- `in = 5` sets the source amplitude or DC level, depending on mode.
- `res(value, node_expr)`, `cap(value, node_expr)`, and `ind(value, node_expr)` create components between the current assignment target and the referenced node expression.
- `+` means multiple parallel branches from the same left-hand node.
- `cir name { ... }` defines a reusable subcircuit with one input port and one output port for the first version.

Current parser constraints:

- component values are numeric literals
- circuit calls currently accept one input argument
- nested component expressions terminate in an identifier or another component call

## Parser Design

Use a simple two-stage frontend.

### 1. Lexer

Convert the source into tokens:

- keywords: `set`, `cir`
- reserved identifiers: `in`, `out`, `gnd`
- identifiers: circuit names, user symbols
- numbers: integer or floating point
- punctuation: `{`, `}`, `(`, `)`, `,`, `=`, `+`
- end-of-line or semicolon as statement terminators

The lexer should track line and column so parse errors can be reported precisely.

### 2. AST Parser

A hand-written recursive descent parser is enough for this grammar size.

Suggested AST shape:

```cpp
enum class SourceMode { Dc, Ac };

struct ProgramAst {
    std::optional<SourceMode> source_mode;
    std::optional<double> input_value;
    std::vector<CircuitAst> circuits;
    std::vector<StatementAst> statements;
};

struct CircuitAst {
    std::string name;
    std::vector<AssignAst> body;
};

struct AssignAst {
    std::string lhs;
    ExprAst rhs;
};

struct CallAst {
    std::string callee;
    std::vector<ExprAst> args;
};

struct ComponentAst {
    enum class Kind { Resistor, Capacitor, Inductor };
    Kind kind;
    double value;
    std::unique_ptr<ExprAst> node_expr;
};
```

Expression forms needed initially:

- numeric literal
- identifier
- component constructor call
- circuit call
- binary `+` for parallel composition

### 3. Lowering Pass

Do not simulate directly from the AST. Lower it into an explicit intermediate representation first.

Lowering responsibilities:

1. Resolve named circuit definitions into a symbol table.
2. Rewrite nested component expressions into temporary internal nodes.
3. Expand circuit calls into graph fragments with unique node namespaces.
4. Replace reserved names:
   - `in` -> subcircuit input node
   - `out` -> subcircuit output node
   - `gnd` -> global node `0`

For the example:

```txt
gnd = cap(47000, in) + res(50, res(50, in));
```

can lower to a graph fragment like:

```txt
branch 1: gnd -- C(47000) -- in
branch 2: gnd -- R(50) -- n$1 -- R(50) -- in
```

The lowering phase is where the language stops being "function-like" and becomes a concrete circuit netlist. This is also the stable boundary for both simulation and visualization.

## Internal Graph Model

The simulator and renderer should operate on a netlist-like graph rather than the raw syntax tree.

### Core Types

```cpp
using NodeId = std::uint32_t;
using ComponentId = std::uint32_t;

enum class ComponentKind {
    Resistor,
    Capacitor,
    Inductor,
    VoltageSource
};

struct Node {
    NodeId id;
    std::string name;
    bool is_ground = false;
};

struct Component {
    ComponentId id;
    ComponentKind kind;
    NodeId a;
    NodeId b;
    double value;

    // Dynamic state for transient analysis.
    double state_voltage = 0.0;
    double state_current = 0.0;
};

struct CircuitGraph {
    std::vector<Node> nodes;
    std::vector<Component> components;
    NodeId ground = 0;
    NodeId input = 0;
    NodeId output = 0;
    SourceConfig source;
};
```

### Why This Model

- Components are always two-terminal in the first version, which keeps stamping simple.
- Nested syntax is flattened into nodes plus edges, which matches nodal analysis directly.
- Dynamic elements carry per-component state across time steps.
- The graph is reusable for both analysis and later static schematic rendering.

### Source Representation

Treat the external input as a stamped source component rather than a special case in the solver.

Minimal source model:

```cpp
struct SourceConfig {
    SourceMode mode = SourceMode::Dc;
    double amplitude = 0.0;
    double frequency_hz = 60.0;
    double phase_rad = 0.0;
    double dc_offset = 0.0;
};
```

At time `t`:

- DC: `V(t) = dc_offset + amplitude`
- AC: `V(t) = dc_offset + amplitude * sin(2 * pi * frequency_hz * t + phase_rad)`

## Simulation Algorithm

Use modified nodal analysis for both DC and transient solves.

### 1. Static Solve Structure

For each time step, assemble:

```txt
A * x = z
```

Where:

- `x` contains unknown node voltages and current variables for voltage-defined elements.
- `A` is the conductance/system matrix.
- `z` is the source vector.

### 2. Component Stamping

Stamp each component into `A` and `z`.

Resistor `R` between nodes `a` and `b`:

- conductance `g = 1 / R`
- add standard symmetric conductance stamps

Independent voltage source:

- add one extra current unknown
- stamp source constraints into `A`
- stamp source voltage into `z`

Capacitor and inductor in transient mode:

- replace each with a companion model for the current time step
- use backward Euler first because it is simple and numerically stable

Backward Euler companion models:

- capacitor: `G = C / dt`, plus a history current term from previous capacitor voltage
- inductor: add a current unknown with `L / dt` relation to previous current

This gives one consistent matrix solve per step.

### 3. Time Stepping

Minimal transient loop:

```cpp
for (double t = 0.0; t <= t_stop; t += dt) {
    assemble_system(graph, source, t, dt, previous_state);
    solve_linear_system();
    extract_node_voltages();
    update_component_history();
    store_sample(t, output_voltage, optional_branch_currents);
}
```

### 4. Output Sampling

Store results in a separate structure:

```cpp
struct Sample {
    double time_s;
    std::vector<double> node_voltages;
    std::vector<double> branch_currents;
};

struct SimulationTrace {
    std::vector<Sample> samples;
};
```

This keeps the solver independent from presentation. Different frontends can consume the same trace:

- CSV or debug dumps
- offline WAV generation
- future real-time audio streaming
- static or dynamic terminal visualization

## Audio Output

The current audio path should treat the simulated `out` voltage as a time-domain signal amplitude, not as a frequency control source.

### Offline WAV Export

The simplest audio pipeline is:

1. Run a transient simulation for a target duration.
2. Read `out` node voltage from the trace.
3. Resample or interpolate the trace onto a fixed audio sample rate.
4. Normalize or scale the signal.
5. Write mono PCM samples to a WAV file.

Minimal audio-facing types:

```cpp
struct AudioOptions {
    int sample_rate = 44100;
};

struct AudioBuffer {
    int sample_rate = 44100;
    std::vector<float> samples;
};
```

Responsibilities:

- the simulator owns physical time stepping
- the audio layer owns interpolation, scaling, and PCM encoding
- the file writer owns WAV container details

### Streaming Audio Plan

Real-time audio output should be designed as a separate execution mode from offline export.

The important architectural change is that the simulator must stop thinking only in terms of "run everything, then return a full trace". For streaming, it needs an incremental sample producer.

Suggested split:

```cpp
struct SimulationState {
    double time_s = 0.0;
    std::vector<double> dynamic_component_state;
    std::vector<double> last_solution;
};

class StreamingSimulator {
public:
    StreamingSimulator(CircuitGraph graph, SimulationOptions sim_options);
    double step();
    void reset();
};
```

Where:

- `step()` advances the circuit by one simulation tick and returns the current output voltage
- internal dynamic state is preserved between calls
- the audio system pulls samples continuously instead of waiting for a completed trace

Recommended streaming pipeline:

1. Parse and lower once.
2. Construct a persistent simulator state.
3. Run an audio callback or producer loop at a fixed output sample rate.
4. For each requested audio sample:
   - advance the simulator by one or more internal time steps
   - interpolate if simulation rate and audio rate differ
   - write the sample into an output ring buffer
5. Let the audio backend drain the ring buffer to the device

Key architectural pieces for streaming:

- `StreamingSimulator`: incremental transient stepping
- `AudioRingBuffer`: lock-minimized producer/consumer queue
- `AudioBackend`: platform layer for device output
- `AudioMixer` or `AudioScaler`: optional amplitude normalization and clipping protection

Important design constraints:

- audio callback code must avoid heavy allocation
- matrix shape should be reused across steps when possible
- the solver should preallocate working buffers
- simulation time step and audio sample rate should be decoupled
- underflow handling must prefer silence over blocking the audio thread

Two viable timing models:

1. `sim_dt == audio_dt`
   - simplest model
   - easiest to reason about
   - may constrain simulator accuracy or audio rate choice
2. `sim_dt < audio_dt`
   - better numerical control
   - requires interpolation or decimation
   - preferable long term for more complex circuits

Streaming should be implemented only after the solver API is reshaped around persistent state and incremental stepping.

## Visualization

The visualization path should be independent from simulation.

### Static Schematic View

The current terminal visualization goal is:

1. Parse source text.
2. Lower to `CircuitGraph`.
3. Build a placement from graph connectivity.
4. Render a static schematic in the terminal alternate buffer.

This mode should not require a transient solve.

### Net-Based Placement

The renderer should treat nodes as nets and components as two-terminal connections between them.

A practical layout model is:

- assign non-ground nets to columns
- preserve `in -> out` as the primary left-to-right backbone
- place extra internal nets by graph rank or connectivity depth
- route non-ground components horizontally between net columns
- route ground-connected components vertically down to a shared ground rail

This is not a full autorouter, but it gives a stable schematic-like display for the current language and lowering model.

### Rendering Constraints

- prefer ASCII-safe symbols by default
- keep orientation awareness for horizontal vs vertical parts
- avoid simulation dependencies in the renderer
- keep graph-to-layout deterministic for debugging
- allow future richer glyph sets behind a terminal capability option

## Suggested Module Split

Minimal file layout:

```txt
src/
  main.cpp
  lexer.h/.cpp
  parser.h/.cpp
  ast.h
  lower.h/.cpp
  graph.h
  simulate.h/.cpp
  linear.h/.cpp
  audio.h/.cpp
  debug.h/.cpp
  render.h/.cpp
  utah.h/.cpp
```

Responsibilities:

- `lexer`: token stream with source positions
- `parser`: source text -> AST
- `lower`: AST -> `CircuitGraph`
- `graph`: graph and component data types
- `simulate`: MNA assembly, stepping, trace collection
- `linear`: dense solver first, sparse later
- `audio`: trace-to-audio conversion and WAV output
- `debug`: graph dumping and inspection helpers
- `render`: net-based static schematic rendering
- `utah`: terminal output primitives and alternate-buffer handling

## Implementation Order

The minimum useful delivery path was:

1. Parse a single circuit file into an AST.
2. Lower resistors plus a voltage source into a graph.
3. Run a DC solve for purely resistive circuits.
4. Add transient stepping with capacitors using backward Euler.
5. Add inductors.
6. Feed the resulting trace into the terminal UI.

That baseline now extends naturally into:

7. Add offline audio export from the output trace.
8. Add static graph visualization without simulation.
9. Refactor the solver around persistent state for future streaming audio.

## First Constraints

To keep the first version tractable:

- support one top-level source
- support one `in` and one `out` per circuit
- support only two-terminal passive components
- use dense matrices first
- use SI base units in the backend
- postpone nonlinear devices like diodes and transistors

That gives a clean path from your current language sketch to a working analogue simulator without overbuilding the frontend or solver.

## Near-Term Next Steps

The most sensible next architectural upgrades are:

1. Refactor simulation state out of per-run local variables so stepping can be persistent.
2. Separate solver stepping from trace accumulation.
3. Add an explicit audio abstraction that can target either WAV files or a live output device.
4. Keep improving net-based visualization without coupling it to the solver.
