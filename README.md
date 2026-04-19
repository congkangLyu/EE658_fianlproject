[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/B8JnW68L)
[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/kryfDs92)

# EE658 — Diagnosis and Design of Reliable Digital Systems

## Project Overview

A fault simulation and test pattern generation (TPG) system for combinational circuits in ISCAS-85 benchmark format. The simulator implements a complete ATPG flow: circuit reading, levelization, SCOAP testability analysis, fault simulation (DFS/PFS), and deterministic test generation using the D-Algorithm and PODEM, with multiple configurable heuristics to optimize fault coverage, runtime, and test volume.

## Build & Run

```
mkdir build && cd build
cmake ..
make
```

The simulator accepts an optional seed for the random number generator (default: 658):

```
./simulator              # uses default seed 658
./simulator 12345        # uses seed 12345
```

This ensures reproducible results even when RTPG is used.

Once inside the simulator, enter commands at the `Command>` prompt.

---

## Simulator Commands

All commands from previous phases remain functional. Below is the complete command reference.

### READ — Read Circuit

```
READ <circuit_file>
```

Reads an ISCAS-85 format circuit file and builds internal data structures.

**Example:**
```
READ ../ckts/c432.ckt
```

### LEV — Levelize

```
LEV
```

Computes the topological level of every node in the circuit. Must be run after `READ` and before simulation commands.

### PC — Print Circuit

```
PC
```

Prints circuit information (node details, fan-in/fan-out, gate types).

### LOGICSIM — Logic Simulation

```
LOGICSIM <input_file> <output_file>
```

Reads primary input values from `<input_file>`, performs logic simulation, and writes primary output values to `<output_file>`.

### RTPG — Random Test Pattern Generation

```
RTPG <num_patterns> <output_file>
```

Generates `<num_patterns>` random binary test patterns and writes them to `<output_file>`.

### RFL — Reduced Fault List

```
RFL <output_file>
```

Generates a collapsed fault list using the checkpoint theorem (PI and fanout-branch nodes only) and writes it to `<output_file>`.

### DFS — Deductive Fault Simulation

```
DFS <tp_file> <output_file>
```

Performs deductive fault simulation on the test patterns in `<tp_file>` and reports all detectable faults to `<output_file>`.

### PFS — Parallel Fault Simulation

```
PFS <fault_file> <tp_file> <output_file>
```

Takes a fault list and test patterns as input. Reports which faults are detected by the given patterns using bit-parallel simulation.

### SCOAP — Testability Analysis

```
SCOAP <output_file>
```

Computes SCOAP controllability (CC0, CC1) and observability (CO) values for every node and writes them to `<output_file>`.

### TPFC — Test Pattern Fault Coverage

```
TPFC <num_patterns> <output_file>
```

Randomly generates `<num_patterns>` test patterns, runs fault simulation, and produces a fault coverage report.

### DTPFC — Deterministic TPFC

```
DTPFC <tp_file> <freq> <tpfc_report_file>
```

Same as TPFC but reads the test patterns from `<tp_file>` instead of generating them randomly. `<freq>` controls the reporting frequency.

### DALG — D-Algorithm (Single Fault)

```
DALG <fault_node> <sa_val> <output_file> [-df nl|nh|lh|cc] [-jf v0]
```

Generates a test pattern for a single stuck-at fault using the D-Algorithm.

**Positional arguments:**

| Argument | Description |
|---|---|
| `fault_node` | Node number where the fault is injected |
| `sa_val` | Stuck-at value: `0` or `1` |
| `output_file` | Output file for the ternary test pattern (0/1/x) |

**Optional flags:**

| Flag | Values | Description |
|---|---|---|
| `-df` | `nl`, `nh`, `lh`, `cc` | D-Frontier selection heuristic (see below) |
| `-jf` | `v0` | J-Frontier justification heuristic: select the line with the lowest SCOAP controllability |

**Example:**
```
DALG 19 1 tp_19_sa1.txt -df cc -jf v0
```

### PODEM — PODEM Algorithm (Single Fault)

```
PODEM <fault_node> <sa_val> <output_file> [-df nl|nh|lh|cc]
```

Generates a test pattern for a single stuck-at fault using the PODEM algorithm.

Arguments are the same as DALG. The `-df` flag controls D-Frontier selection. The `-jf` flag is not applicable to PODEM (PODEM uses backtrace instead of J-Frontier justification).

**Example:**
```
PODEM 19 0 tp_19_sa0.txt -df lh
```

### DFRONT — D-Frontier Help

```
DFRONT
```

Prints available D-Frontier heuristic options and usage examples.

---

## TPG — Test Pattern Generation (Phase 4)

The `TPG` command is the core of Phase 4. It combines RTPG and deterministic ATPG with fault dropping to generate a complete test set for all single stuck-at faults in the circuit.

### Command Format

```
TPG [-fo <fault_order>] <algorithm> <rtpg_version> <rtpg_threshold> <output_file>
```

### Positional Arguments

| Argument | Description |
|---|---|
| `algorithm` | ATPG algorithm for the deterministic phase: `dalg` or `podem` |
| `rtpg_version` | RTPG heuristic version: `0`, `1`, `2`, `3`, or `4` (see below) |
| `rtpg_threshold` | Hyperparameter controlling RTPG termination (meaning depends on version) |
| `output_file` | Output file for the generated test patterns |

### Optional Flags

| Flag | Values | Description |
|---|---|---|
| `-fo` | `rfl`, `scoap_easy`, `scoap_hard` | Fault ordering heuristic for the ATPG phase (see below) |

### RTPG Versions (`rtpg_version`)

The RTPG stage runs first and generates random patterns to quickly reach easy-to-detect faults. The `rtpg_version` controls how RTPG terminates and selects patterns:

| Version | Behavior | `rtpg_threshold` meaning |
|---|---|---|
| `0` | No RTPG — skip directly to deterministic ATPG | (ignored, but a value must still be provided) |
| `1` | Generate random patterns until cumulative fault coverage reaches the threshold | Target FC percentage (e.g., `60` means stop RTPG at 60% FC) |
| `2` | Generate random patterns; stop when the FC improvement from a single new pattern drops below the threshold | Minimum delta-FC per pattern (e.g., `0.5` means stop when a pattern improves FC by less than 0.5%) |
| `3` | Evaluate average FC improvement over the last K patterns (K=5); stop when the average drops below the threshold | Minimum average delta-FC over K patterns |
| `4` | Generate Q candidate patterns (Q=5) per round, select the one that detects the most faults; stop when delta-FC drops below the threshold | Minimum delta-FC per round (same as v2 but with best-of-Q selection) |

### Fault Ordering Heuristics (`-fo`)

After RTPG completes, the remaining undetected faults are passed to the deterministic ATPG engine. The `-fo` flag controls the order in which these faults are targeted:

| Value | Description |
|---|---|
| *(none)* | Baseline — iterate faults in circuit order (no reordering) |
| `rfl` | RFL-first — prioritize faults on PI and fanout-branch nodes, then process the rest |
| `scoap_easy` | Easy-first — sort faults by ascending SCOAP testability cost (CC + CO), targeting the easiest faults first |
| `scoap_hard` | Hard-first — sort faults by descending SCOAP testability cost, targeting the hardest faults first |

### D-Frontier Selection Heuristics (`-df`)

Used by DALG and PODEM to choose which gate on the D-Frontier to expand next:

| Value | Description |
|---|---|
| *(none)* | Baseline — no specific order |
| `nl` | Select the gate with the **lowest** node number |
| `nh` | Select the gate with the **highest** node number |
| `lh` | Select the gate at the **highest** level |
| `cc` | Select the gate with the **lowest** SCOAP observability (CO) |

### J-Frontier Justification Heuristic (`-jf`)

Used by DALG to select which line to justify when multiple unjustified gates exist:

| Value | Description |
|---|---|
| *(none)* | Baseline — default justification order |
| `v0` | Select the line with the lowest SCOAP controllability |

### TPG Internal Flow

1. **Build fault list** — enumerate all 2N single stuck-at faults (SA0 and SA1 for each of N nodes).
2. **RTPG stage** — generate random patterns according to the selected version, run PFS-based fault dropping after each pattern to remove detected faults.
3. **Fault ordering** — reorder remaining faults according to `-fo` heuristic (if specified).
4. **Deterministic ATPG stage** — for each remaining fault, run DALG or PODEM to find a test pattern. Unspecified PIs are filled with random binary values to maximize fault dropping. A backtrack limit of 500 is used to skip hard/redundant faults efficiently.
5. **Fault dropping** — after each ATPG-generated pattern, run PFS to drop all additionally detected faults.
6. **Termination** — stop when FC exceeds 97.5% or all faults have been attempted.
7. **Output** — write the final test set to the output file (CSV format: header row of PI node numbers, followed by one pattern per line).

### TPG Examples

**Baseline (no heuristics):**
```
READ ../ckts/c432.ckt
LEV
TPG dalg 0 0 tp_c432_baseline.txt
```

**RTPG v1 with PODEM, stop RTPG at 60% FC:**
```
READ ../ckts/c1355.ckt
LEV
TPG podem 1 60 tp_c1355_v1.txt
```

**RTPG v3 with DALG, average delta-FC threshold of 0.1, easy-first fault order:**
```
READ ../ckts/c1908.ckt
LEV
TPG -fo scoap_easy dalg 3 0.1 tp_c1908_v3_easy.txt
```

**RTPG v4 (best-of-Q) with PODEM, hard-first fault order:**
```
READ ../ckts/c3540.ckt
LEV
TPG -fo scoap_hard podem 4 0.5 tp_c3540_v4_hard.txt
```

**RFL-first fault ordering with DALG, RTPG v1 at 50% FC:**
```
READ ../ckts/c5315.ckt
LEV
TPG -fo rfl dalg 1 50 tp_c5315_rfl.txt
```

### TPG Output Format

The output file is a CSV with the following structure:

```
<PI_node_1>,<PI_node_2>,...,<PI_node_n>
<val>,<val>,...,<val>
<val>,<val>,...,<val>
...
```

The first row contains the primary input node numbers in ascending order. Each subsequent row is a test pattern where each value is `0`, `1`, or `x`.

### TPG Console Output

After execution, TPG prints a summary line:

```
TPG: alg=DALG rtpg_ver=1 fo=scoap_easy tps=42 FC=98.50% (394/400), RTPG time=0.02s, full time=1.35s
```

This reports the algorithm used, RTPG version, fault ordering mode, number of test patterns generated, fault coverage achieved, RTPG time, and total runtime.

---

## Supported Benchmark Circuits

The `ckts/` directory contains ISCAS-85 benchmark circuits:

| Circuit | Description |
|---|---|
| c17 | Smallest benchmark (6 gates) — useful for debugging |
| c432 | 27-channel interrupt controller |
| c499 | 32-bit SEC circuit |
| c880 | 8-bit ALU |
| c1355 | 32-bit SEC circuit |
| c1908 | 16-bit SEC/DED circuit |
| c3540 | 8-bit ALU |
| c5315 | 9-bit ALU |
| c6288 | 16×16 multiplier |

Additional small circuits (`c1`–`c4`, `add2`, `cmini`, `x3mult`) are provided for unit testing.

---

## Testing

You can test your code with the provided test scripts:

```
python3 test_main.py --func atpg -alg dalg
python3 test_main.py --func atpg -alg podem
python3 test_main.py --func scoap
python3 test_main.py --func rtpg
python3 test_main.py --func rfl
python3 test_main.py --func pfs
python3 test_main.py --func dfs
python3 test_main.py --func logicsim
python3 test_main.py --func all
```

**Test script arguments:**

| Flag | Description |
|---|---|
| `-tlim` | Time limit per test case in seconds (default: 1) |
| `-tb` | Testbench version: `EZ` (easy, circuits with <1000 lines) |
| `-dnC` | Skip build/compile step |
| `-dnD` | Skip testbench download |
| `-dnR` | Keep result files after simulation |

---

## Checking Memory Leaks with Valgrind

Install Valgrind (Ubuntu):
```
sudo apt-get install valgrind
```

Run the simulator under Valgrind:
```
valgrind --leak-check=yes ./simulator
```

**Interpreting the output:**

- `definitely lost` — allocated memory with no remaining pointer
- `indirectly lost` — the pointer to the variable's pointer was lost
- `possibly lost` — memory not freed; Valgrind cannot confirm if a pointer exists
- `still reachable` — a global variable was not freed but still has a pointer
- `suppressed` — can be safely ignored

[Read more here.](https://web.stanford.edu/class/archive/cs/cs107/cs107.1174/guide_valgrind.html)
