#include "tpg.h"
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
#include <utility>

// Structures and forward declarations for DLAG and PODEM helpers
struct DlagSimResult { std::vector<int> good; std::vector<int> faulty; };

extern int dlag_node_index_by_num(int node_num);
extern DlagSimResult dlag_simulate_pattern(const std::vector<int> &assign, int fault_node_num, int stuck_at);
extern bool dlag_pattern_detects_fault(const std::vector<int> &assign, int fault_node_num, int stuck_at);
extern bool dlag_can_still_activate(const std::vector<int> &assign, int fault_node_num, int stuck_at);
extern bool dlag_has_possible_propagation(const std::vector<int> &assign, int fault_node_num, int stuck_at);
extern bool dlag_backtrack_search(const std::vector<int> &order, int depth, std::vector<int> &assign, int fault_node_num, int stuck_at, std::vector<int> &solution);
extern std::vector<int> dlag_compress_to_ternary(const std::vector<int> &binary_assign, int fault_node_num, int stuck_at);
extern void dlag_compute_scoap_internal();
extern bool simulate_circuit(int fault_node_num, int sa_val);
extern bool fault_at_po();
extern bool fault_activated(int fault_idx, int sa_val);
extern std::vector<NSTRUC*> get_d_frontier();
extern int non_controlling_value(NSTRUC *g);
extern bool podem_rec(int fault_idx, int sa_val);
extern bool get_objective(int fault_idx, int sa_val, NSTRUC* &obj_node, int &obj_val);
extern void backtrace(NSTRUC *obj_node, int obj_val, NSTRUC* &pi_node, int &pi_val);
extern int pi_index_from_node(NSTRUC *pi);

/*======================= TPG (Phase 4) ================================*/

// Internal DALG helper: returns true on success and fills tp_out with the
// ternary PI assignment (indexed by Pinput[i]). No file I/O.
static bool run_dalg_internal(int fault_node_num, int sa_val, std::vector<int> &tp_out) {
    tp_out.clear();
    if (sa_val != 0 && sa_val != 1) return false;

    idx_of_num.clear();
    for (int i = 0; i < Nnodes; i++) idx_of_num[(int)Node[i].num] = i;
    if (idx_of_num.find(fault_node_num) == idx_of_num.end()) return false;

    bool need_scoap = false;
    for (int i = 0; i < Nnodes; i++) {
        if (Node[i].scoap.CC0 <= 0 || Node[i].scoap.CC1 <= 0 || Node[i].scoap.CO < 0) {
            need_scoap = true; break;
        }
    }
    if (need_scoap) dlag_compute_scoap_internal();

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
    bool ok = dlag_backtrack_search(order, 0, assign, fault_node_num, sa_val, solution);
    if (!ok) return false;

    tp_out = dlag_compress_to_ternary(solution, fault_node_num, sa_val);
    return true;
}

// Internal PODEM helper: returns true on success and fills tp_out.
static bool run_podem_internal(int fault_node_num, int sa_val, std::vector<int> &tp_out) {
    tp_out.clear();
    if (sa_val != 0 && sa_val != 1) return false;

    idx_of_num.clear();
    for (int i = 0; i < Nnodes; i++) idx_of_num[(int)Node[i].num] = i;
    auto it = idx_of_num.find(fault_node_num);
    if (it == idx_of_num.end()) return false;

    pi_assign.assign(Npi, LX);
    bool ok = podem_rec(it->second, sa_val);
    if (!ok) return false;

    tp_out = pi_assign;
    return true;
}

// Ternary/binary fault simulation wrapper: does "assign" detect node@sa?
static inline bool tpg_pattern_detects(const std::vector<int> &assign,
                                       int fault_node_num, int sa_val) {
    return dlag_pattern_detects_fault(assign, fault_node_num, sa_val);
}

// A simple single-fault single-pattern check loop used to drop detected faults
// after a new test pattern is added. For baseline we just reuse the
// good/faulty simulator that already handles ternary X values.
static void tpg_drop_detected_faults(const std::vector<int> &assign,
                                     std::vector<std::pair<int,int>> &flist) {
    std::vector<std::pair<int,int>> kept;
    kept.reserve(flist.size());
    for (auto &f : flist) {
        if (!tpg_pattern_detects(assign, f.first, f.second)) kept.push_back(f);
    }
    flist.swap(kept);
}

static bool tpg_write_tp_file(const char *outfile,
                              const std::vector<std::vector<int>> &tps) {
    FILE *fd = fopen(outfile, "w");
    if (!fd) return false;

    // header: PI node numbers sorted ascending
    std::vector<std::pair<int,int>> ordered;
    ordered.reserve(Npi);
    for (int i = 0; i < Npi; i++) ordered.push_back({(int)Pinput[i]->num, i});
    std::sort(ordered.begin(), ordered.end());

    for (int i = 0; i < (int)ordered.size(); i++) {
        if (i) fprintf(fd, ",");
        fprintf(fd, "%d", ordered[i].first);
    }
    fprintf(fd, "\n");

    for (const auto &tp : tps) {
        for (int i = 0; i < (int)ordered.size(); i++) {
            if (i) fprintf(fd, ",");
            int v = tp[ordered[i].second];
            char c = (v == 0) ? '0' : (v == 1) ? '1' : 'x';
            fprintf(fd, "%c", c);
        }
        fprintf(fd, "\n");
    }
    fclose(fd);
    return true;
}


void tpg() {
    char alg_str[MAXLINE];
    int  rtpg_cnt = -1;
    char outfile[MAXLINE];

    rstrip(cp);
    if (sscanf(cp, "%1023s %d %1023s", alg_str, &rtpg_cnt, outfile) != 3) {
        printf("Invalid Input!\n");
        return;
    }
    for (char *q = alg_str; *q; ++q) *q = Upcase(*q);

    bool use_dalg;
    if      (strcmp(alg_str, "DALG")  == 0) use_dalg = true;
    else if (strcmp(alg_str, "PODEM") == 0) use_dalg = false;
    else { printf("Invalid Input!\n"); return; }

    if (rtpg_cnt < 0) { printf("Invalid Input!\n"); return; }

    // Make sure node num -> idx map is ready for ATPG helpers.
    idx_of_num.clear();
    for (int i = 0; i < Nnodes; i++) idx_of_num[(int)Node[i].num] = i;

    // Step A: build full single stuck-at fault list (fault = {node_num, sa}).
    std::vector<std::pair<int,int>> flist;
    flist.reserve((size_t)Nnodes * 2);
    for (int i = 0; i < Nnodes; i++) {
        flist.push_back({(int)Node[i].num, 0});
        flist.push_back({(int)Node[i].num, 1});
    }
    int total_faults = (int)flist.size();

    std::vector<std::vector<int>> final_tps;
    final_tps.reserve((size_t)rtpg_cnt + 64);

    // Step B: RTPG stage (binary patterns).
    for (int t = 0; t < rtpg_cnt && !flist.empty(); t++) {
        std::vector<int> pat(Npi, 0);
        for (int i = 0; i < Npi; i++) pat[i] = std::rand() & 1;
        final_tps.push_back(pat);
        tpg_drop_detected_faults(pat, flist);
    }

    // Step C: Deterministic ATPG stage on remaining faults.
    // Iterate over a snapshot so dropping during the loop is safe.
    std::vector<std::pair<int,int>> pending = flist;
    for (const auto &f : pending) {
        if (flist.empty()) break;
        // skip already dropped faults
        bool still_there = false;
        for (const auto &g : flist) {
            if (g.first == f.first && g.second == f.second) { still_there = true; break; }
        }
        if (!still_there) continue;

        std::vector<int> tp;
        bool ok = use_dalg ? run_dalg_internal(f.first, f.second, tp)
                           : run_podem_internal(f.first, f.second, tp);
        if (!ok || (int)tp.size() != Npi) continue;

        final_tps.push_back(tp);
        tpg_drop_detected_faults(tp, flist);
    }

    if (!tpg_write_tp_file(outfile, final_tps)) {
        printf("Cannot open output file!\n");
        return;
    }

    int covered = total_faults - (int)flist.size();
    double fc = total_faults ? (100.0 * covered / total_faults) : 0.0;
    printf("TPG: alg=%s rtpg=%d tps=%d FC=%.2f%% (%d/%d)\n",
           use_dalg ? "DALG" : "PODEM",
           rtpg_cnt, (int)final_tps.size(), fc, covered, total_faults);
    printf("==> OK");
}

