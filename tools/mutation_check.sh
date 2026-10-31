#!/usr/bin/env bash
# Mutation check: deliberately break the solver in 3 ways and confirm the test-suite FAILS each time.
# Usage: tools/mutation_check.sh   (Linux/WSL/Git-Bash with g++; restores the source afterwards)
set -u
cd "$(dirname "$0")/.."
F=src/core/fea1d.cpp; cp "$F" /tmp/fea1d.orig
build() { g++ -std=c++17 -O1 -Isrc/core -Itests -pthread src/core/*.cpp tests/test_*.cpp -o /tmp/wb_mut_tests 2>&1 | head -3; }
run() { /tmp/wb_mut_tests >/tmp/wb_mut.log 2>&1; }
check() {  # name, sed expression
  cp /tmp/fea1d.orig "$F"; sed -i "$2" "$F"
  if cmp -s "$F" /tmp/fea1d.orig; then echo "[$1] mutation did not apply"; return; fi
  build; if run; then echo "[$1] SURVIVED  (tests still pass -> weak tests!)"; else echo "[$1] killed ($(grep -ci fail /tmp/wb_mut.log) failing lines)"; fi
}
check "source term q*A*h/2 -> q*A*h/3"  's|p.q \* p.A \* h / 2.0|p.q * p.A * h / 3.0|'
check "Dirichlet T0 not moved to RHS"     's|ws.rhs\[1\] -= ws.off\[0\] \* p.T0;|;|'
check "Dirichlet TL not moved to RHS"     's|ws.rhs\[n - 1\] -= ws.off\[n - 1\] \* p.TL;|;|'
cp /tmp/fea1d.orig "$F"; build; run && echo "restored: original passes" || echo "ERROR: original fails"
