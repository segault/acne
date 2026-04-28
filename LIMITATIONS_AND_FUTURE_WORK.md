# ACNE Limitations And Future Work

This file lists the current weak spots and the best upgrade path.

## 1. Frontend Limits

Current language is still small.

Limits:

- one top-level input source model
- values are plain numbers only
- no unit suffixes like `10k`, `100n`, `5u`
- no parameterized subcircuits
- no vector ports
- no conditional logic
- no proper digital primitives

Needed later:

- unit parser
- named parameters
- macros or reusable blocks
- multi-pin subcircuits
- cleaner source configuration syntax

Example future syntax:

```txt
set ac(freq=440, amp=2)
vcc = vsrc(5, gnd)
r1 = res(10k, in)
c1 = cap(100n, gnd)
```

## 2. Lowering Limits

Current lowering works for present syntax, but it is still rigid.

Weak spots:

- assumes simple component-call shapes
- assumes small fixed component families
- transistor encoding is awkward because original graph was mostly two-terminal
- internal node generation is simplistic

Needed later:

- explicit IR for multi-terminal devices
- cleaner separation between parsing and electrical semantics
- stronger validation before solve

## 3. Graph Model Limits

Current graph is workable, but stretched by transistors.

Problem:

- the graph started as two-terminal
- transistor support had to be bolted on with extra node fields

Better long-term design:

```txt
Component {
  kind
  terminals[]
  parameters[]
  dynamic_state[]
}
```

That gives:

- arbitrary pin count
- easier MOSFET/op-amp/device support
- cleaner stamping code

## 4. Solver Limits

This is the main technical bottleneck.

Current linear solver:

- dense
- simple
- easy to debug

Problems:

- scales badly
- wastes time and memory
- not good for larger circuits

Needed:

- sparse matrix storage
- sparse LU or iterative methods where appropriate
- matrix structure reuse between time steps

## 5. Nonlinear Solve Limits

Current nonlinear support is still early.

Problems:

- convergence not robust
- damping crude
- no proper line search
- no gmin stepping
- no source stepping
- no junction voltage limiting
- finite-difference Jacobian for transistor is slow and noisy

Needed:

1. proper Newton residual/Jacobian framework
2. analytic device derivatives
3. damping based on residual improvement
4. source stepping
5. gmin stepping
6. operating-point solve before transient

Without this, hard circuits will:

- oscillate numerically
- diverge
- converge to nonsense
- produce huge voltages

## 6. Diode Model Limits

Current diode model is basic.

Missing:

- series resistance
- junction capacitance
- breakdown
- temperature dependence

For now, okay.

Later:

- add parameterized diode model
- add transient diode capacitance

## 7. BJT Model Limits

Current BJT model is still rough.

Main issue:

- not yet trustworthy for realistic analog behavior

Missing or weak:

- analytic Jacobian
- Early effect
- base resistance
- emitter resistance
- collector resistance
- junction capacitances
- transit time
- charge storage
- realistic saturation behavior

Current result:

- can demonstrate topology
- cannot yet guarantee sane bias points
- can blow up to ugly voltages

Best upgrade order:

1. clean Ebers-Moll implementation
2. analytic derivatives
3. operating-point initialization
4. parasitic resistances
5. capacitances and charge storage
6. eventually Gummel-Poon if needed

## 8. Transient Solve Limits

Current transient support uses backward Euler only.

Good:

- stable

Bad:

- diffusive
- can smear fast transitions
- not ideal for audio precision or switching edges

Later options:

- trapezoidal integration
- Gear methods
- method selection per analysis mode

Need:

- proper initialization from DC operating point

## 9. Audio Limits

Current audio system works, but it is still first-pass.

Offline WAV path:

- okay

Live output path:

- backend exists
- environment/device failures still possible
- no strong device capability handling yet

Missing:

- proper ring buffer between producer and live backend
- underflow/overflow metrics
- latency control
- device selection
- stereo routing
- resampling policy
- clipping control beyond simple normalization

Needed later:

1. ring buffer between simulator producer and audio backend
2. explicit live backend abstraction per platform
3. error messages for device failures
4. sample-rate negotiation
5. optional limiter

## 10. Visualization Limits

Current viewer is static and useful, but primitive.

Missing:

- better autorouting
- multi-line labels
- nicer branch compaction
- overlapping-net conflict handling
- dynamic measurement overlays
- live simulation animation

Current net-based view is good for:

- topology debugging
- small circuits

Not good yet for:

- dense circuits
- polished schematics
- publication-quality diagrams

Later:

- dedicated placement pass
- routing conflict resolution
- viewport / pan / zoom
- live highlighting

## 11. Digital / Mixed-Signal Limits

Project goal includes digital later.

Current code is mostly analog.

Missing:

- logic thresholds
- discrete event timing
- clocks
- latches
- metastability handling
- mixed analog/digital bridge components

Best path:

- finish analog foundation first
- add comparator / threshold elements
- then add digital event engine
- then mixed-signal interface

## 12. Performance Limits

Current performance problems:

- dense matrices
- repeated allocations
- finite-difference transistor derivatives
- full rebuild every iteration
- long audio renders slow

Needed:

- preallocated work buffers
- matrix topology reuse
- sparse data structures
- analytic device derivatives
- optional fast mode for audio rendering

## 13. Reliability Limits

Current program still needs stronger failure behavior.

Missing:

- better singular-matrix diagnostics
- device model validation
- bad-netlist diagnostics
- convergence reports
- warnings for absurd voltages/currents

Needed:

- error messages that point to bad devices/nodes
- optional verbose solve diagnostics
- max-voltage safety cutoffs

## 14. Testing Limits

Current testing is mostly manual.

That is not enough.

Needed:

- parser tests
- lowering tests
- DC solve tests
- transient tests
- diode tests
- transistor bias tests
- audio-output tests
- visualization snapshot tests

Need known circuits:

- resistor divider
- RC low-pass
- RL step
- diode clipper
- transistor inverter
- Schmitt trigger

## 15. Priority Order

Best next technical order:

1. stabilize nonlinear solver
2. improve BJT model
3. add DC operating-point solve
4. add sparse or semi-sparse matrix path
5. improve audio live backend
6. improve visualization quality
7. expand language

If solver stays weak, everything above it stays shaky.

## 16. Practical Bottom Line

Right now project is good for:

- learning
- experimenting
- simple analog circuits
- audio experiments
- topology demos

Right now project is not yet good for:

- serious transistor analog design
- trustworthy switching thresholds
- large circuits
- robust real-time synthesis
- SPICE-class simulation quality

That is normal.

Core direction is good.

Main hill:

- nonlinear device accuracy
- nonlinear convergence
