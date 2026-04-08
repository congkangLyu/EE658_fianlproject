#include "podem.h"
#include "globals.h"
#include "utils.h"
#include <stdio.h>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <limits>
#include <numeric>

void simulate_circuit(int fault_node_num, int sa_val) {
    good_val.assign(Nnodes, LX);
    bad_val.assign(Nnodes, LX);

    for (int i = 0; i < Npi; i++) {
      int idx = Pinput[i]->indx;
      good_val[idx] = pi_assign[i];
      bad_val[idx] = pi_assign[i];

      // inject stuck-at fault even when the fault site is a PI
      if ((int)Pinput[i]->num == fault_node_num) {
        bad_val[idx] = sa_val;
      }
    }

    int max_level = 0;
    for (int i = 0; i < Nnodes; i++) {
        if (Node[i].level > max_level) max_level = Node[i].level;
    }

    for (int l = 0; l <= max_level; l++) {
        for (int i = 0; i < Nnodes; i++) {
            NSTRUC *np = &Node[i];
            if (np->level != l) continue;
            if (np->ntype == PI) continue;

            good_val[i] = eval_gate_from_inputs(np, good_val);
            bad_val[i] = eval_gate_from_inputs(np, bad_val);

            if ((int)np->num == fault_node_num) {
                bad_val[i] = sa_val;
            }
        }
    }
}

bool fault_at_po() {
    for (int i = 0; i < Npo; i++) {
        int idx = Poutput[i]->indx;
        if (good_val[idx] != LX && bad_val[idx] != LX && good_val[idx] != bad_val[idx]) {
            return true;
        }
    }
    return false;
}

bool fault_activated(int fault_idx, int sa_val) {
    return good_val[fault_idx] != LX &&
           good_val[fault_idx] == (1 - sa_val) &&
           bad_val[fault_idx] == sa_val;
}

static bool node_has_fault_effect(int idx) {
    return good_val[idx] != LX &&
           bad_val[idx] != LX &&
           good_val[idx] != bad_val[idx];
}

static bool gate_output_unresolved(int idx) {
    if (good_val[idx] == LX || bad_val[idx] == LX) return true;
    return good_val[idx] == bad_val[idx];
}

std::vector<NSTRUC*> get_d_frontier() {
    std::vector<NSTRUC*> df;

    for (int i = 0; i < Nnodes; i++) {
        NSTRUC *np = &Node[i];
        if (np->ntype == PI) continue;

        bool hasD = false;
        bool hasX = false;

        for (unsigned k = 0; k < np->fin; k++) {
            int u = np->unodes[k]->indx;
            if (node_has_fault_effect(u)) hasD = true;
            if (good_val[u] == LX && bad_val[u] == LX) hasX = true;
        }

        if (hasD && hasX && gate_output_unresolved(i)) {
            df.push_back(np);
        }
    }

    return df;
}

int non_controlling_value(NSTRUC *g) {
    switch (g->type) {
        case AND:
        case NAND:
            return 1;
        case OR:
        case NOR:
            return 0;
        default:
            return 1;
    }
}

bool get_objective(int fault_idx, int sa_val, NSTRUC* &obj_node, int &obj_val) {
    if (!fault_activated(fault_idx, sa_val)) {
        obj_node = &Node[fault_idx];
        obj_val = 1 - sa_val;
        return true;
    }

    std::vector<NSTRUC*> df = get_d_frontier();
    if (df.empty()) return false;

    std::sort(df.begin(), df.end(), [](NSTRUC *a, NSTRUC *b) {
        return a->scoap.CO < b->scoap.CO;
    });

    NSTRUC *g = df[0];
    int want = non_controlling_value(g);

    NSTRUC *best_in = nullptr;
    int best_cost = std::numeric_limits<int>::max();

    for (unsigned k = 0; k < g->fin; k++) {
        NSTRUC *u = g->unodes[k];
        int idx = u->indx;

        if (good_val[idx] == LX && bad_val[idx] == LX) {
            int c = (want == 0) ? u->scoap.CC0 : u->scoap.CC1;
            if (c < best_cost) {
                best_cost = c;
                best_in = u;
            }
        }
    }

    if (!best_in) return false;

    obj_node = best_in;
    obj_val = want;
    return true;
}

static bool is_inverting_gate(NSTRUC *g) {
    return (g->type == NOT || g->type == NAND || g->type == NOR);
}

static NSTRUC* choose_backtrace_input(NSTRUC *np, int val) {
    NSTRUC *best = nullptr;
    int best_cost = std::numeric_limits<int>::max();

    for (unsigned i = 0; i < np->fin; i++) {
        NSTRUC *u = np->unodes[i];
        int idx = u->indx;

        if (!(good_val[idx] == LX && bad_val[idx] == LX)) continue;

        int c = (val == 0) ? u->scoap.CC0 : u->scoap.CC1;
        if (c < best_cost) {
            best_cost = c;
            best = u;
        }
    }

    return best; // no fallback
}

void backtrace(NSTRUC *obj_node, int obj_val, NSTRUC* &pi_node, int &pi_val) {
    NSTRUC *cur = obj_node;
    int val = obj_val;

    while (cur->ntype != PI) {
        if (is_inverting_gate(cur)) {
            val = 1 - val;
        }
        cur = choose_backtrace_input(cur, val);
        if (cur == nullptr) break;
    }

    pi_node = cur;
    pi_val = val;
}

int pi_index_from_node(NSTRUC *pi) {
    for (int i = 0; i < Npi; i++) {
        if (Pinput[i] == pi) return i;
    }
    return -1;
}

static bool x_path_exists_from_dfrontier() {
    return !get_d_frontier().empty();
}

bool podem_rec(int fault_idx, int sa_val) {
    simulate_circuit((int)Node[fault_idx].num, sa_val);

    if (fault_at_po()) {
        return true;
    }

    if (fault_activated(fault_idx, sa_val) && !x_path_exists_from_dfrontier()) {
        return false;
    }

    NSTRUC *obj_node = nullptr;
    int obj_val = LX;
    if (!get_objective(fault_idx, sa_val, obj_node, obj_val)) {
        return false;
    }

    NSTRUC *pi_node = nullptr;
    int pi_val = LX;
    backtrace(obj_node, obj_val, pi_node, pi_val);

    if (pi_node == nullptr) return false;

    int pidx = pi_index_from_node(pi_node);
    if (pidx < 0) return false;

    int cur = pi_assign[pidx];
    if (cur != LX) return false;

    // Normal trial only if the PI is X:
    pi_assign[pidx] = pi_val;
    if (podem_rec(fault_idx, sa_val)) return true;

    pi_assign[pidx] = 1 - pi_val;
    if (podem_rec(fault_idx, sa_val)) return true;

    pi_assign[pidx] = LX;
    return false;
}

static int all_fanins_ready(NSTRUC *np) {
    for (unsigned i = 0; i < np->fin; i++) {
        if (np->unodes[i]->gate_output != 0 && np->unodes[i]->gate_output != 1)
            return 0;
    }
    return 1;
}


void podem() {
    int fault_num, sa_val;
    char outfile[MAXLINE];
    rstrip(cp);

    if (sscanf(cp, "%d %d %1023s", &fault_num, &sa_val, outfile) != 3) {
        printf("Invalid Input!\n");
        return;
    }
    if (sa_val != 0 && sa_val != 1) {
        printf("Invalid Input!\n");
        return;
    }

    FILE *fd = fopen(outfile, "w");
    if (!fd) {
        printf("Cannot open output file!\n");
        return;
    }

    idx_of_num.clear();
    for (int i = 0; i < Nnodes; i++) {
        idx_of_num[(int)Node[i].num] = i;
    }

    auto it = idx_of_num.find(fault_num);
    if (it == idx_of_num.end()) {
        fclose(fd);
        printf("Invalid fault node!\n");
        return;
    }

    pi_assign.assign(Npi, LX);

    bool ok = podem_rec(it->second, sa_val);

    if (ok) {
        for (int i = 0; i < Npi; i++) {
            if (i) fprintf(fd, ",");
            fprintf(fd, "%d", (int)Pinput[i]->num);
        }
        fprintf(fd, "\n");

        for (int i = 0; i < Npi; i++) {
            if (i) fprintf(fd, ",");
            char c = (pi_assign[i] == 0) ? '0' : (pi_assign[i] == 1) ? '1' : 'x';
            fprintf(fd, "%c", c);
        }
        fprintf(fd, "\n");
    }

    fclose(fd);
    printf("==> OK");
}

