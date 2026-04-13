
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
#include <unordered_set>
#include <queue>
#include <limits>
#include <numeric>
#include <ctime>
#include <random>

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

static int backtrack_count;
static const int MAX_BACKTRACKS = 5000;
static const int MAX_SEARCH_DEPTH = 24;
static const double MAX_SEARCH_SEC = 1.5;
static clock_t dlag_search_start;

static inline bool dlag_time_exceeded() {
    return (double)(clock() - dlag_search_start) / CLOCKS_PER_SEC > MAX_SEARCH_SEC;
}

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

static std::vector<int> dlag_build_relevant_pi_order(int fault_node_num) {
    std::vector<int> order;
    int fault_idx = dlag_node_index_by_num(fault_node_num);
    if (fault_idx < 0) {
        for (int i = 0; i < Npi; i++) order.push_back(i);
        return order;
    }

    // 1) forward cone from fault to find reachable PO region
    std::vector<char> reach_from_fault(Nnodes, 0);
    std::queue<int> q;
    q.push(fault_idx);
    reach_from_fault[fault_idx] = 1;
    while (!q.empty()) {
        int u = q.front();
        q.pop();
        NSTRUC *np = &Node[u];
        for (unsigned k = 0; k < np->fout; k++) {
            int v = np->dnodes[k]->indx;
            if (!reach_from_fault[v]) {
                reach_from_fault[v] = 1;
                q.push(v);
            }
        }
    }

    // 2) backward from reachable POs to collect all PI ancestors that can matter
    std::vector<char> needed(Nnodes, 0);
    std::queue<int> qb;
    for (int i = 0; i < Npo; i++) {
        int idx = Poutput[i]->indx;
        if (reach_from_fault[idx]) {
            needed[idx] = 1;
            qb.push(idx);
        }
    }

    std::unordered_set<int> pi_set;
    while (!qb.empty()) {
        int u = qb.front();
        qb.pop();
        NSTRUC *np = &Node[u];
        if (np->ntype == PI) {
            pi_set.insert(u);
            continue;
        }
        for (unsigned k = 0; k < np->fin; k++) {
            int p = np->unodes[k]->indx;
            if (!needed[p]) {
                needed[p] = 1;
                qb.push(p);
            }
        }
    }

    for (int i = 0; i < Npi; i++) {
        if (pi_set.count(Pinput[i]->indx)) order.push_back(i);
    }
    if (order.empty()) {
        for (int i = 0; i < Npi; i++) order.push_back(i);
    }

    std::sort(order.begin(), order.end(), [](int a, int b) {
        NSTRUC *pa = Pinput[a];
        NSTRUC *pb = Pinput[b];
        int ca = std::min(pa->scoap.CC0, pa->scoap.CC1) + pa->scoap.CO;
        int cb = std::min(pb->scoap.CC0, pb->scoap.CC1) + pb->scoap.CO;
        if (ca != cb) return ca < cb;
        return pa->num < pb->num;
    });
    return order;
}

static bool dlag_try_guided_random(const std::vector<int> &order,
                                   int fault_node_num,
                                   int stuck_at,
                                   std::vector<int> &solution) {
    std::mt19937 rng((unsigned)time(nullptr));
    std::uniform_int_distribution<int> bit(0, 1);

    const int trials = 256;
    std::vector<int> assign(Npi, LX);

    for (int t = 0; t < trials; t++) {
        if (dlag_time_exceeded()) return false;
        std::fill(assign.begin(), assign.end(), LX);

        for (int pos = 0; pos < (int)order.size() && pos < MAX_SEARCH_DEPTH; pos++) {
            int pi_idx = order[pos];
            int v = bit(rng);

            if ((int)Pinput[pi_idx]->num == fault_node_num) {
                v = 1 - stuck_at;
            } else {
                int cc0 = Pinput[pi_idx]->scoap.CC0;
                int cc1 = Pinput[pi_idx]->scoap.CC1;
                int preferred = (cc1 >= 0 && cc0 >= 0 && cc1 < cc0) ? 1 : 0;

                // Mostly choose the preferred easy value, but allow randomness.
                if ((t & 3) != 0) v = preferred;
            }
            assign[pi_idx] = v;
        }

        if (dlag_pattern_detects_fault(assign, fault_node_num, stuck_at)) {
            solution = assign;
            return true;
        }
    }
    return false;
}

bool dlag_backtrack_search(const std::vector<int> &order,
                           int depth,
                           std::vector<int> &assign,
                           int fault_node_num,
                           int stuck_at,
                           std::vector<int> &solution) {
    if (++backtrack_count > MAX_BACKTRACKS) return false;
    if (dlag_time_exceeded()) return false;

    DlagSimResult sim = dlag_simulate_pattern(assign, fault_node_num, stuck_at);

    int fault_idx = dlag_node_index_by_num(fault_node_num);
    bool activated = false;
    if (fault_idx >= 0) {
        int g = sim.good[fault_idx];
        int f = sim.faulty[fault_idx];
        if (g != LX && f != LX && g == f) return false;
        activated = (g != LX && f != LX && g != f);
    }

    bool detected = false;
    bool any_po_possible = false;
    for (int i = 0; i < Npo; i++) {
        int idx = Poutput[i]->indx;
        int g = sim.good[idx];
        int f = sim.faulty[idx];
        if (g != LX && f != LX && g != f) {
            detected = true;
            break;
        }
        if (g == LX || f == LX) any_po_possible = true;
    }

    if (detected) {
        solution = assign;
        return true;
    }
    if (!any_po_possible) return false;

    if (depth >= (int)order.size() || depth >= MAX_SEARCH_DEPTH) return false;

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

        // Before activation, bias more strongly toward the easier value.
        if (!activated && depth > MAX_SEARCH_DEPTH / 2) {
            assign[pi_idx] = first;
            if (dlag_backtrack_search(order, depth + 1, assign, fault_node_num, stuck_at, solution)) return true;
            assign[pi_idx] = LX;
            return false;
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

static int cost_other_non_controlling(NSTRUC *np, int index) {
    int cost = 0;
    switch (np->type) {
        case NOT:
        case BRCH:
            break;
        case XOR:
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
        if (++co_iters > Nnodes + 1) break;
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

    std::vector<int> order = dlag_build_relevant_pi_order(fault_num);

    std::vector<int> solution;
    dlag_search_start = clock();

    // Fast guided-random attempt first.
    if (!dlag_try_guided_random(order, fault_num, sa_val, solution)) {
        std::vector<int> assign(Npi, LX);
        backtrack_count = 0;
        (void)dlag_backtrack_search(order, 0, assign, fault_num, sa_val, solution);
    }

    if (solution.empty()) {
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
