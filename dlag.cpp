#include "dlag.h"
#include "globals.h"
#include "utils.h"
#include "static_helpers.h"
#include <stdio.h>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <limits>
#include <numeric>

// Make these functions non-static so tpg.cpp can use them
int dlag_node_index_by_num(int node_num) {
    auto it = idx_of_num.find(node_num);
    if (it == idx_of_num.end()) return -1;
    return it->second;
}

struct DlagSimResult {
    std::vector<int> good;
    std::vector<int> faulty;
};

DlagSimResult dlag_simulate_pattern(const std::vector<int> &assign, int fault_node_num, int stuck_at) {
    DlagSimResult res;
    res.good.assign(Nnodes, LX);
    res.faulty.assign(Nnodes, LX);

    for (int i = 0; i < Npi; i++) {
        int v = (i < (int)assign.size()) ? assign[i] : LX;
        int idx = Pinput[i]->indx;
        res.good[idx] = v;
        res.faulty[idx] = ((int)Pinput[i]->num == fault_node_num) ? stuck_at : v;
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

            std::vector<int> gins, fins;
            gins.reserve(np->fin);
            fins.reserve(np->fin);
            for (unsigned k = 0; k < np->fin; k++) {
                gins.push_back(res.good[np->unodes[k]->indx]);
                fins.push_back(res.faulty[np->unodes[k]->indx]);
            }

            int gv = eval_gate_from_inputs(np, res.good);
            int fv = eval_gate_from_inputs(np, res.faulty);
            if (np->ntype == FB) {
                gv = gins.empty() ? LX : gins[0];
                fv = fins.empty() ? LX : fins[0];
            }

            res.good[np->indx] = gv;
            res.faulty[np->indx] = ((int)np->num == fault_node_num) ? stuck_at : fv;
        }
    }

    return res;
}

bool dlag_pattern_detects_fault(const std::vector<int> &assign, int fault_node_num, int stuck_at) {
    DlagSimResult sim = dlag_simulate_pattern(assign, fault_node_num, stuck_at);
    for (int i = 0; i < Npo; i++) {
        int idx = Poutput[i]->indx;
        int g = sim.good[idx];
        int f = sim.faulty[idx];
        if (g != LX && f != LX && g != f) return true;
    }
    return false;
}

bool dlag_can_still_activate(const std::vector<int> &assign, int fault_node_num, int stuck_at) {
    int fault_idx = dlag_node_index_by_num(fault_node_num);
    if (fault_idx < 0) return false;
    DlagSimResult sim = dlag_simulate_pattern(assign, fault_node_num, stuck_at);
    int g = sim.good[fault_idx];
    int f = sim.faulty[fault_idx];
    if (g != LX && f != LX && g != f) return true;
    return (g == LX || f == LX);
}

bool dlag_has_possible_propagation(const std::vector<int> &assign, int fault_node_num, int stuck_at) {
    DlagSimResult sim = dlag_simulate_pattern(assign, fault_node_num, stuck_at);
    for (int i = 0; i < Npo; i++) {
        int idx = Poutput[i]->indx;
        int g = sim.good[idx];
        int f = sim.faulty[idx];
        if (g != LX && f != LX && g != f) return true;
        if (g == LX || f == LX) return true;
    }
    return false;
}

static int backtrack_count;
static const int MAX_BACKTRACKS = 100000;

bool dlag_backtrack_search(const std::vector<int> &order,
                                  int depth,
                                  std::vector<int> &assign,
                                  int fault_node_num,
                                  int stuck_at,
                                  std::vector<int> &solution) {
    if (++backtrack_count > MAX_BACKTRACKS) return false;  // abort: search space too large

    // Single simulation for all three checks (was 3 separate simulations before)
    DlagSimResult sim = dlag_simulate_pattern(assign, fault_node_num, stuck_at);

    // Check 1: can the fault still be activated?
    int fault_idx = dlag_node_index_by_num(fault_node_num);
    if (fault_idx >= 0) {
        int g = sim.good[fault_idx];
        int f = sim.faulty[fault_idx];
        if (g != LX && f != LX && g == f) return false;  // fault cannot activate
    }

    // Check 2 & 3: propagation possible? already detected?
    bool detected = false;
    bool any_po_possible = false;
    for (int i = 0; i < Npo; i++) {
        int idx = Poutput[i]->indx;
        int g = sim.good[idx];
        int f = sim.faulty[idx];
        if (g != LX && f != LX && g != f) { detected = true; break; }
        if (g == LX || f == LX) any_po_possible = true;
    }
    if (detected) {
        solution = assign;
        return true;
    }
    if (!any_po_possible) return false;  // no PO can show a difference

    if (depth >= (int)order.size()) return false;

    int pi_idx = order[depth];
    int first = 0, second = 1;

    if ((int)Pinput[pi_idx]->num == fault_node_num) {
        first = 1 - stuck_at;
        second = stuck_at;
    } else {
        int cc0 = Pinput[pi_idx]->scoap.CC0;
        int cc1 = Pinput[pi_idx]->scoap.CC1;
        if (cc0 >= 0 && cc1 >= 0 && cc1 < cc0) {
            first = 1;
            second = 0;
        }
    }

    assign[pi_idx] = first;
    if (dlag_backtrack_search(order, depth + 1, assign, fault_node_num, stuck_at, solution)) return true;

    assign[pi_idx] = second;
    if (dlag_backtrack_search(order, depth + 1, assign, fault_node_num, stuck_at, solution)) return true;

    assign[pi_idx] = LX;
    return false;
}

std::vector<int> dlag_compress_to_ternary(const std::vector<int> &binary_assign,
                                                 int fault_node_num,
                                                 int stuck_at) {
    std::vector<int> out = binary_assign;
    for (int i = 0; i < Npi; i++) {
        int oldv = out[i];
        out[i] = LX;
        if (!dlag_pattern_detects_fault(out, fault_node_num, stuck_at)) out[i] = oldv;
    }
    return out;
}


static int cost_other_non_controlling (NSTRUC *np, int index) {
    int cost = 0;
    switch (np->type) {
        case NOT:
        case BRCH:
            break;
        case XOR:
            // NOT IN NETLISTS
            break;
        case OR:
        case NOR:
            for (int i = 0; i < np->fin; i++) {
                if (i == index) continue;
                cost += np->unodes[i]->scoap.CC0;
            }
            break;
        case NAND:
        case AND:
            for (int i = 0; i < np->fin; i++) {
                if (i == index) continue;
                cost += np->unodes[i]->scoap.CC1;
            }
            break;
        default:
            printf("Unknown node type!\n");
            exit(-1);
    }
    return cost;
}

void dlag_compute_scoap_internal() {
    int big_number = std::numeric_limits<int>::max() / 4;
    for (int i = 0; i < Nnodes; i++) {
        Node[i].scoap.CC0 = -1;
        Node[i].scoap.CC1 = -1;
        Node[i].scoap.CO = big_number;
    }
    for (int i = 0; i < Npi; i++) {
        Pinput[i]->scoap.CC0 = 1;
        Pinput[i]->scoap.CC1 = 1;
    }
    for (int i = 0; i < Npo; i++) {
        Poutput[i]->scoap.CO = 0;
    }

    bool done = false;
    while (!done) {
        done = true;
        for (int i = 0; i < Nnodes; i++) {
            NSTRUC *np = &Node[i];
            if (np->ntype == PI) continue;

            std::vector<int> v0, v1;
            switch (np->type) {
                case BRCH:
                    if (np->unodes[0]->scoap.CC0 == -1 || np->unodes[0]->scoap.CC1 == -1) {
                        done = false;
                        break;
                    }
                    np->scoap.CC0 = np->unodes[0]->scoap.CC0;
                    np->scoap.CC1 = np->unodes[0]->scoap.CC1;
                    break;
                case NOT:
                    if (np->unodes[0]->scoap.CC0 == -1 || np->unodes[0]->scoap.CC1 == -1) {
                        done = false;
                        break;
                    }
                    np->scoap.CC0 = np->unodes[0]->scoap.CC1 + 1;
                    np->scoap.CC1 = np->unodes[0]->scoap.CC0 + 1;
                    break;
                case OR:
                case NOR:
                case NAND:
                case AND:
                    for (int j = 0; j < (int)np->fin; j++) {
                        if (np->unodes[j]->scoap.CC0 == -1 || np->unodes[j]->scoap.CC1 == -1) {
                            done = false;
                            v0.clear();
                            v1.clear();
                            break;
                        }
                        v0.push_back(np->unodes[j]->scoap.CC0);
                        v1.push_back(np->unodes[j]->scoap.CC1);
                    }
                    if (!v0.empty()) {
                        if (np->type == OR) {
                            np->scoap.CC0 = 1 + std::accumulate(v0.begin(), v0.end(), 0);
                            np->scoap.CC1 = 1 + *std::min_element(v1.begin(), v1.end());
                        } else if (np->type == NOR) {
                            np->scoap.CC1 = 1 + std::accumulate(v0.begin(), v0.end(), 0);
                            np->scoap.CC0 = 1 + *std::min_element(v1.begin(), v1.end());
                        } else if (np->type == NAND) {
                            np->scoap.CC1 = 1 + *std::min_element(v0.begin(), v0.end());
                            np->scoap.CC0 = 1 + std::accumulate(v1.begin(), v1.end(), 0);
                        } else if (np->type == AND) {
                            np->scoap.CC0 = 1 + *std::min_element(v0.begin(), v0.end());
                            np->scoap.CC1 = 1 + std::accumulate(v1.begin(), v1.end(), 0);
                        }
                    }
                    break;
                default:
                    break;
            }
        }
    }

    done = false;
    int co_iters = 0;
    while (!done) {
        if (++co_iters > Nnodes + 1) break;  // safety: avoid infinite loop on dangling nodes
        done = true;
        for (int i = 0; i < Nnodes; i++) {
            NSTRUC *np = &Node[i];
            if (np->scoap.CO == big_number) {
                done = false;
                continue;
            }
            for (int j = 0; j < (int)np->fin; j++) {
                NSTRUC *in = np->unodes[j];
                int contrib = np->scoap.CO + cost_other_non_controlling(np, j);
                if (np->type != BRCH) contrib += 1;
                if (contrib < in->scoap.CO) {
                    in->scoap.CO = contrib;
                    done = false;
                }
            }
        }
    }
}

static bool dlag_write_tp_output_file(const char *outfile, const std::vector<int> &assign) {
    FILE *fd = fopen(outfile, "w");
    if (!fd) return false;

    std::vector<std::pair<int,int>> ordered;
    ordered.reserve(Npi);
    for (int i = 0; i < Npi; i++) ordered.push_back({(int)Pinput[i]->num, i});
    std::sort(ordered.begin(), ordered.end());

    for (int i = 0; i < (int)ordered.size(); i++) {
        if (i) fprintf(fd, ",");
        fprintf(fd, "%d", ordered[i].first);
    }
    fprintf(fd, "\n");

    for (int i = 0; i < (int)ordered.size(); i++) {
        if (i) fprintf(fd, ",");
        fprintf(fd, "%c", print_3val(assign[ordered[i].second]));
    }
    fprintf(fd, "\n");
    fclose(fd);
    return true;
}

void dlag() {
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

    idx_of_num.clear();
    for (int i = 0; i < Nnodes; i++) idx_of_num[(int)Node[i].num] = i;

    if (idx_of_num.find(fault_num) == idx_of_num.end()) {
        FILE *fd = fopen(outfile, "w");
        if (fd) fclose(fd);
        printf("Invalid fault node!\n");
        return;
    }

    bool need_scoap = false;
    for (int i = 0; i < Nnodes; i++) {
        if (Node[i].scoap.CC0 <= 0 || Node[i].scoap.CC1 <= 0 || Node[i].scoap.CO < 0) {
            need_scoap = true;
            break;
        }
    }
    if (need_scoap) {
        dlag_compute_scoap_internal();
    }

    std::vector<int> order(Npi);
    for (int i = 0; i < Npi; i++) order[i] = i;
    std::sort(order.begin(), order.end(), [](int a, int b) {
        NSTRUC *pa = Pinput[a];
        NSTRUC *pb = Pinput[b];
        int ca = std::min(pa->scoap.CC0, pa->scoap.CC1) + pa->scoap.CO;
        int cb = std::min(pb->scoap.CC0, pb->scoap.CC1) + pb->scoap.CO;
        if (ca != cb) return ca < cb;
        return pa->num < pb->num;
    });

    std::vector<int> assign(Npi, LX);
    std::vector<int> solution;
    backtrack_count = 0;
    bool ok = dlag_backtrack_search(order, 0, assign, fault_num, sa_val, solution);

    if (!ok) {
        FILE *fd = fopen(outfile, "w");
        if (!fd) {
            printf("Cannot open output file!\n");
            return;
        }
        fclose(fd);
        printf("==> OK");
        return;
    }

    std::vector<int> ternary = dlag_compress_to_ternary(solution, fault_num, sa_val);
    if (!dlag_write_tp_output_file(outfile, ternary)) {
        printf("Cannot open output file!\n");
        return;
    }

    printf("==> OK");
}

