# Task C: 1-D FEA, assumptions and verification

## Problem
Steady conduction in a rod, `-k A T'' = q A` on `0 < x < L`, with `L = 1 m`, `A = 1e-4 m^2`, `k = 10 W/(m K)`, `q = 1e4 W/m^3` (uniform volumetric generation), `T(0) = 100 C`, `T(L) = 20 C`.

Exact solution: `T(x) = T0 + (TL - T0) x / L + q x (L - x) / (2 k)`.

## Assumptions and units
Steady state; constant k and A; no lateral loss (1-D); uniform q; Dirichlet ends; SI units (m, W, K-differences in C). `q` is volumetric (W/m^3); `A` cancels in the equations but is kept in the element matrices so units stay explicit.

## Method
Two-node linear elements, `h = L/N`. Element stiffness `(kA/h) [[1,-1],[-1,1]]`, consistent load `q A h / 2` per node. Assembly is tridiagonal, element by element; Dirichlet values are moved to the right-hand side and the reduced system is solved with the Thomas algorithm.
Midpoint temperature is evaluated by **FE interpolation** `T_h(x_mid) = (T_i + T_{i+1}) / 2` and compared with the exact value.

## Results (`results/thermal_convergence.csv`, plots in `results/`)
| N | h (m) | max nodal error (C) | max midpoint error (C) | observed order |
|---|---|---|---|---|
| 10 | 0.1 | 2.8e-14 | 1.25 | - |
| 20 | 0.05 | 2.0e-13 | 0.3125 | 2.00 |
| 40 | 0.025 | 8.8e-13 | 0.078125 | 2.00 |
| 80 | 0.0125 | 1.2e-12 | 0.01953125 | 2.00 |

The midpoint error equals `q h^2 / (8 k) = 125 h^2` exactly (second-order convergence), confirmed by `fea_midpoint_error_equals_q_h2_over_8k`.

## Debugging insight
For this problem the FE **nodal values are exact** (to round-off), because the 1-D Galerkin solution is nodally exact for `-u'' = f`. So a "nodal error" check always passes, even for a solver that is subtly wrong in its *interpolation* or element size handling, and it also hides the real discretisation error. The error becomes visible only **between** nodes: evaluate at element midpoints using the FE interpolant (not the exact function). I therefore verify with (1) midpoint error against `q h^2 / (8 k)`, (2) a convergence slope of 2, and (3) an independent dense-matrix Gaussian-elimination solver on the same mesh (`fea_matches_dense_reference_solver`, also with random parameters).

## Mutation evidence (`tools/mutation_check.sh`, log in `results/mutation_check.log`)
Three deliberate bugs were injected into `solveRodInto`; the test suite failed each time (all "killed"), and passed again once restored:
source term `qAh/2 -> qAh/3`; left Dirichlet value not moved to the RHS; right Dirichlet value not moved to the RHS.

## Input validation
`validateRod` rejects non-finite or non-positive L, A, k; fewer than 1 element; absurd N; non-finite BCs; and returns clear messages (tested in `fea_validation_rejects_bad_input`).
