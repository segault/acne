# ACNE Mathematics

This file describes the main mathematical ideas used by the project.

## 1. Circuit As Graph

The source language is lowered into a graph:

- nodes = electrical nets
- components = connections between nets

For the current simulator, most parts are treated as two-terminal elements:

- resistor
- capacitor
- inductor
- diode
- voltage source

The transistor model is three-terminal, but it is still stamped into the same node-voltage system.

## 2. Node Voltages

The main unknowns are node voltages.

If a node is ground:

```txt
V_gnd = 0
```

Every other node gets solved relative to ground.

For some elements, extra unknowns are added:

- current through ideal voltage source
- current through inductor companion model

So the solver unknown vector looks like:

```txt
x = [node voltages..., source currents..., inductor currents...]
```

## 3. Kirchhoff Current Law

At each non-ground node:

```txt
sum of currents leaving node = 0
```

This is the base equation behind the whole simulator.

Every component contributes current terms to one or more node equations.

## 4. Modified Nodal Analysis

The project uses modified nodal analysis, MNA.

Linear solve form:

```txt
A x = z
```

Where:

- `A` = system matrix
- `x` = unknown voltages and some currents
- `z` = source vector

For nonlinear solve, the project effectively solves:

```txt
F(x) = 0
```

using repeated linearization.

## 5. Resistor Stamp

For resistor `R` between nodes `a` and `b`:

```txt
g = 1 / R
```

Contribution:

```txt
 +g at (a,a)
 +g at (b,b)
 -g at (a,b)
 -g at (b,a)
```

This is standard conductance stamping.

## 6. Ideal Voltage Source Stamp

For ideal source between `a` and `b`, add one current unknown `Ivs`.

Constraint:

```txt
V_a - V_b = Vs
```

This becomes extra row and column in MNA:

```txt
node a row gets +1 in source-current column
node b row gets -1 in source-current column
source row gets +1 at node a
source row gets -1 at node b
rhs gets source voltage
```

For the project:

- `InputSource` uses the time-varying input signal
- `VoltageSource` uses fixed DC value from `vsrc(...)`

## 7. AC Input Signal

For `set ac`, input is:

```txt
V(t) = dc_offset + amplitude * sin(2*pi*f*t + phase)
```

Current default:

- `f = 60 Hz`

For `set dc`, input is constant:

```txt
V(t) = dc_offset + amplitude
```

## 8. Transient Analysis

Reactive parts use time stepping.

Current implementation uses backward Euler.

### Capacitor

Capacitor equation:

```txt
i = C * dv/dt
```

Backward Euler:

```txt
i_n = C * (v_n - v_{n-1}) / dt
```

Rearrange:

```txt
i_n = (C/dt) * v_n - (C/dt) * v_{n-1}
```

So capacitor becomes:

- conductance `G = C/dt`
- history term from previous voltage

This is called a companion model.

### Inductor

Inductor equation:

```txt
v = L * di/dt
```

Backward Euler:

```txt
v_n = L * (i_n - i_{n-1}) / dt
```

This requires an extra current unknown.

Again:

- linear stamp per step
- history term from previous current

## 9. Nonlinear Devices

Diodes and BJTs are nonlinear.

They do not fit directly into one fixed linear matrix.

So the simulator linearizes them around the current guess and repeats.

## 10. Diode Model

Current implementation uses Shockley-style diode current:

```txt
I_d = I_s * (exp(V_d / V_t) - 1)
```

Where:

- `I_s` = saturation current
- `V_t` = thermal voltage

Small-signal conductance:

```txt
g_d = dI_d/dV_d = (I_s / V_t) * exp(V_d / V_t)
```

The simulator adds tiny `gmin` conductance too, to avoid singular behavior:

```txt
g_d ~= diode slope + gmin
```

Then the diode is linearized around the current guess:

```txt
I(V) ~= I(V0) + g(V0) * (V - V0)
```

Equivalent current source term:

```txt
I_eq = I(V0) - g(V0) * V0
```

This is what gets stamped into the linear system for each Newton step.

## 11. BJT Model

Current transistor work is more advanced than the original fake beta-only model, but still not SPICE-grade.

The simulator now uses an Ebers-Moll-like current calculation shape:

- forward base-emitter exponential
- reverse base-collector exponential
- transport factors from forward and reverse beta

At a high level:

```txt
I_f = I_s * (exp(V_be / V_t) - 1)
I_r = I_s * (exp(V_bc / V_t) - 1)
```

Then collector/emitter currents are formed from forward and reverse transport terms.

This gives:

- cutoff behavior
- active region behavior
- some saturation behavior

For Jacobian terms, the current implementation uses finite differences around the current guess, not full closed-form derivatives.

Meaning:

1. evaluate transistor current at current guess
2. perturb collector/base/emitter voltages slightly
3. estimate partial derivatives numerically
4. stamp those derivatives into the Newton matrix

This is slower than analytic derivatives, but easier to get working.

## 12. Newton Iteration

For nonlinear circuits, the project solves approximately:

```txt
F(x) = 0
```

by repeated linearization.

Current loop shape:

1. start from previous solution as initial guess
2. build linear part of system
3. evaluate nonlinear residuals
4. build Jacobian contributions
5. solve for update
6. apply damped update
7. repeat until small change

This is Newton-like iteration.

The current implementation checks:

- residual size
- update size

This is better than update-size-only convergence.

## 13. Damping

Raw Newton steps can blow up.

So the solver applies a simple damping strategy:

```txt
x_new = x_old + lambda * dx
```

With `lambda <= 1`.

Current damping is still basic.

Purpose:

- reduce overshoot
- keep nonlinear voltages from jumping too far
- improve chances of convergence

## 14. Numerical Safeguards

Current safeguards include:

- exponential clamping before `exp(...)`
- tiny `gmin`
- damped Newton update
- previous step as initial guess

These help, but they do not fully solve hard nonlinear convergence problems.

## 15. Linear Solver

The project uses dense Gaussian elimination with pivoting.

That means:

1. choose pivot
2. eliminate below pivot
3. back-substitute

Good:

- simple
- easy to debug

Bad:

- poor scaling for large circuits
- wastes work on sparse systems

Long term, sparse linear algebra is the correct path.

## 16. Audio Mathematics

The output voltage at node `out` is treated as a time-domain signal.

Offline path:

1. transient simulation creates samples in physical time
2. signal is interpolated to audio sample times
3. amplitude is normalized
4. samples are quantized to 16-bit PCM

Interpolation is linear:

```txt
v(t) = v0 + alpha * (v1 - v0)
```

Where:

```txt
alpha = (t - t0) / (t1 - t0)
```

## 17. Visualization Mathematics

The static schematic view does not solve the circuit.

It uses graph placement logic:

- nodes become columns
- components are routed between node columns
- ground-connected parts drop to a shared rail

This is graph layout, not circuit simulation.

## 18. Big Mathematical Limits Right Now

Current math is enough for:

- passive linear circuits
- simple transient RC/RL behavior
- basic nonlinear experimentation

Current math is not enough yet for:

- stable realistic transistor switching
- accurate saturation behavior
- large analog circuits
- robust convergence on hard nonlinear topologies
- SPICE-level confidence
