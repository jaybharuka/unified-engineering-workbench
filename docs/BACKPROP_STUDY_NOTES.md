# Task F: study notes (for LEARNING ONLY)

**The graded handwritten appendix must be in YOUR handwriting (max 2 pages, scan or photo), and you must be able to explain every line.**
Use these notes to check your own working, not to copy. Model: `z = w x + b`, `h = tanh z`, `y_hat = v h + c`, `J = 1/2 (y_hat - y)^2`; x = 1, y = 2; w = 0.5, b = 0, v = 1, c = 0; learning rate eta = 0.1.

## Forward
`z = 0.5`, `h = tanh(0.5) = 0.4621171573`, `y_hat = 1*h + 0 = 0.4621171573`, `e = y_hat - y = -1.5378828427`, `J = 0.5 e^2 = 1.1825418190`.

## Backward (chain rule)
* `dJ/dy_hat = e = -1.5378828427`
* `dJ/dc = e * 1 = -1.5378828427`
* `dJ/dv = e * h = -0.7106820475`
* `dJ/dh = e * v = -1.5378828427`
* `dh/dz = 1 - h^2 = 0.7864477329` -> `dJ/dz = dJ/dh * (1 - h^2) = -1.2094644752`
* `dJ/dw = dJ/dz * x = -1.2094644752`,  `dJ/db = dJ/dz * 1 = -1.2094644752`

## Simultaneous update (all gradients use the ORIGINAL parameters)
`w = 0.5 + 0.1*1.2094644752 = 0.6209464475`, `b = 0.1209464475`, `v = 1 + 0.1*0.7106820475 = 1.0710682047`, `c = 0.1537882843`. New loss `J = 0.6857737650` (lower).

## Gradient direction
A negative gradient means increasing that parameter *lowers* J; gradient descent moves opposite the gradient (`theta <- theta - eta dJ/dtheta`). Here the prediction (0.46) is below the target (2), so every parameter moves to raise `y_hat`.

## Numerical check (C++: `src/core/backprop.cpp`, `results/backprop_report.txt`)
Central differences with epsilon = 1e-6: maximum absolute error **1.13e-10** (tolerance 1e-5) -> PASS for all four parameters.

## Loss over 100 subsequent steps (`results/backprop_loss.csv`, `plot_backprop_loss.png`)
Step 0: 1.1825, step 1 (the hand update): 0.6858, step 10: 1.0e-2, step 50: 1.3e-10, step 100: 1.7e-20 (log scale). Loss falls smoothly and roughly geometrically, because with one sample and four parameters the network can fit the target exactly (J -> 0); with eta = 0.1 the steps are small enough not to overshoot. This is memorisation of one point, not generalisation.
