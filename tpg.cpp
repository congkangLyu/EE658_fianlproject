#include "tpg.h"
#include "globals.h"
#include "utils.h"
#include "static_helpers.h"
#include "pfs.h"
#include <stdio.h>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <limits>
#include <numeric>
#include <utility>
#include <chrono>
#include <unordered_set>
#include <sstream>
#include <string>

// Structures and forward declarations for dalG and PODEM helpers
struct dalgSimResult { std::vector<int> good; std::vector<int> faulty; };

extern int dalg_node_index_by_num(int node_num);
extern dalgSimResult dalg_simulate_pattern(const std::vector<int> &assign, int fault_node_num, int stuck_at);
extern bool dalg_pattern_detects_fault(const std::vector<int> &assign, int fault_node_num, int stuck_at);
extern bool dalg_can_still_activate(const std::vector<int> &assign, int fault_node_num, int stuck_at);
extern bool dalg_has_possible_propagation(const std::vector<int> &assign, int fault_node_num, int stuck_at);
extern bool dalg_backtrack_search(const std::vector<int> &order, int depth, std::vector<int> &assign, int fault_node_num, int stuck_at, std::vector<int> &solution);
extern std::vector<int> dalg_compress_to_ternary(const std::vector<int> &binary_assign, int fault_node_num, int stuck_at);
extern void dalg_compute_scoap_internal();
extern void dalg_set_backtrack_limit(int limit);
extern int  dalg_get_backtrack_limit();
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

    // idx_of_num is already built by the TPG main function; skip redundant rebuild.
    if (idx_of_num.find(fault_node_num) == idx_of_num.end()) return false;

    bool need_scoap = false;
    for (int i = 0; i < Nnodes; i++) {
        if (Node[i].scoap.CC0 <= 0 || Node[i].scoap.CC1 <= 0 || Node[i].scoap.CO < 0) {
            need_scoap = true; break;
        }
    }
    if (need_scoap) dalg_compute_scoap_internal();

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
    bool ok = dalg_backtrack_search(order, 0, assign, fault_node_num, sa_val, solution);
    if (!ok) return false;

    // In TPG context: skip ternary compression (it weakens fault dropping)
    // and fill unspecified PIs with random binary values so the pattern
    // can detect additional faults during fault dropping.
    tp_out = solution;
    for (int i = 0; i < (int)tp_out.size(); i++) {
        if (tp_out[i] == LX) tp_out[i] = std::rand() & 1;
    }
    return true;
}

// Internal PODEM helper: returns true on success and fills tp_out.
static bool run_podem_internal(int fault_node_num, int sa_val, std::vector<int> &tp_out) {
    tp_out.clear();
    if (sa_val != 0 && sa_val != 1) return false;

    // idx_of_num already built by TPG main; skip redundant rebuild.
    auto it = idx_of_num.find(fault_node_num);
    if (it == idx_of_num.end()) return false;

    pi_assign.assign(Npi, LX);
    bool ok = podem_rec(it->second, sa_val);
    if (!ok) return false;

    // Fill X's with random values for better fault dropping.
    tp_out = pi_assign;
    for (int i = 0; i < (int)tp_out.size(); i++) {
        if (tp_out[i] == LX) tp_out[i] = std::rand() & 1;
    }
    return true;
}

// Ternary/binary fault simulation wrapper: does "assign" detect node@sa?
static inline bool tpg_pattern_detects(const std::vector<int> &assign,
                                       int fault_node_num, int sa_val) {
    return dalg_pattern_detects_fault(assign, fault_node_num, sa_val);
}

// Count how many of the currently remaining faults are detected by `assign`.
// Uses bit-parallel fault simulation for speed — up to 63 faults per pass.
// `assign` is expected in PI order (same convention as dalg_simulate_pattern):
// assign[i] is the value for Pinput[i].  flist is not modified.
static int num_detect_faults(const std::vector<int> &assign,
                                     std::vector<std::pair<int,int>> &flist) {
    if (flist.empty()) return 0;
    std::vector<char> detected;
    pfs_detect_batch(assign, flist, detected);
    int cnt = 0;
    for (size_t i = 0; i < detected.size(); i++) if (detected[i]) cnt++;
    return cnt;
}

// Drop every fault in `flist` that is detected by `assign` using bit-parallel
// fault simulation (PFS).  `assign` is in PI order (see above).  Short-circuits
// on an empty list.
static void tpg_drop_detected_faults(const std::vector<int> &assign,
                                     std::vector<std::pair<int,int>> &flist) {
    if (flist.empty()) return;
    std::vector<char> detected;
    pfs_detect_batch(assign, flist, detected);
    std::vector<std::pair<int,int>> kept;
    kept.reserve(flist.size());
    for (size_t i = 0; i < flist.size(); i++) {
        if (!detected[i]) kept.push_back(flist[i]);
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


// ---------- Fault ordering helpers ----------

// Compute SCOAP testability cost for a single fault (node_num @ sa_val).
// Cost = CC_sa_val(node) + CO(node).  Lower cost → easier to test.
static int scoap_fault_cost(int node_num, int sa_val) {
    auto it = idx_of_num.find(node_num);
    if (it == idx_of_num.end()) return std::numeric_limits<int>::max();
    NSTRUC *np = &Node[it->second];
    int cc = (sa_val == 0) ? np->scoap.CC0 : np->scoap.CC1;
    int co = np->scoap.CO;
    if (cc < 0) cc = std::numeric_limits<int>::max() / 2;
    if (co < 0) co = std::numeric_limits<int>::max() / 2;
    return cc + co;
}

// Reorder a fault list according to the chosen heuristic.
static void apply_fault_order(std::vector<std::pair<int,int>> &flist, int fo_mode) {
    if (fo_mode == FO_NONE) return;

    if (fo_mode == FO_RFL) {
        // Partition: RFL faults (PI and fanout-branch nodes) come first,
        // then the rest. Within each group, keep original order.
        std::unordered_set<int> rfl_nums;
        for (int i = 0; i < Nnodes; i++) {
            if (Node[i].ntype == PI || Node[i].ntype == FB) {
                rfl_nums.insert((int)Node[i].num);
            }
        }
        std::stable_partition(flist.begin(), flist.end(),
            [&](const std::pair<int,int> &f) {
                return rfl_nums.count(f.first) > 0;
            });
    }
    else if (fo_mode == FO_SCOAP_EASY) {
        // Sort by ascending SCOAP cost (easiest faults first).
        std::stable_sort(flist.begin(), flist.end(),
            [](const std::pair<int,int> &a, const std::pair<int,int> &b) {
                return scoap_fault_cost(a.first, a.second)
                     < scoap_fault_cost(b.first, b.second);
            });
    }
    else if (fo_mode == FO_SCOAP_HARD) {
        // Sort by descending SCOAP cost (hardest faults first).
        std::stable_sort(flist.begin(), flist.end(),
            [](const std::pair<int,int> &a, const std::pair<int,int> &b) {
                return scoap_fault_cost(a.first, a.second)
                     > scoap_fault_cost(b.first, b.second);
            });
    }
}

// ---------- end fault ordering helpers ----------

void tpg() {
    char alg_str[MAXLINE];
    int  rtpg_ver = -1; // rtpg version
    char outfile[MAXLINE];
    int covered;
    int rtpg_fc_limit;
    int fo_mode = FO_NONE;  // fault order heuristic (default: none)

    rstrip(cp);

    // ---------- Flexible argument parsing ----------
    // Format: TPG [options] <alg> <rtpg_ver> <rtpg_fc_limit> <outfile>
    // Options:  -fo rfl | scoap_easy | scoap_hard
    //
    // We tokenise the whole line and consume flag-value pairs first,
    // then treat the remaining positional tokens in order.

    std::vector<std::string> tokens;
    {
        std::istringstream iss(cp);
        std::string tok;
        while (iss >> tok) tokens.push_back(tok);
    }

    // Extract optional flags
    std::vector<std::string> positional;
    for (size_t t = 0; t < tokens.size(); ) {
        if (tokens[t] == "-fo" && t + 1 < tokens.size()) {
            std::string val = tokens[t+1];
            // to lower
            for (auto &c : val) c = Lowcase(c);
            if      (val == "rfl")        fo_mode = FO_RFL;
            else if (val == "scoap_easy") fo_mode = FO_SCOAP_EASY;
            else if (val == "scoap_hard") fo_mode = FO_SCOAP_HARD;
            else { printf("Unknown fault-order heuristic: %s\n", tokens[t+1].c_str()); return; }
            t += 2;
        } else {
            positional.push_back(tokens[t]);
            t++;
        }
    }

    if (positional.size() != 4) {
        printf("Invalid Input!\n");
        return;
    }

    strncpy(alg_str, positional[0].c_str(), MAXLINE-1); alg_str[MAXLINE-1] = '\0';
    rtpg_ver = std::atoi(positional[1].c_str());
    rtpg_fc_limit = std::atoi(positional[2].c_str());
    strncpy(outfile, positional[3].c_str(), MAXLINE-1); outfile[MAXLINE-1] = '\0';

    for (char *q = alg_str; *q; ++q) *q = Upcase(*q);

    bool use_dalg;
    if      (strcmp(alg_str, "DALG")  == 0) use_dalg = true;
    else if (strcmp(alg_str, "PODEM") == 0) use_dalg = false;
    else { printf("Invalid Input!\n"); return; }

    if (rtpg_ver > 4 || rtpg_ver < 0) { printf("Invalid RTPG Version!\n"); return; }

    auto start_full = std::chrono::high_resolution_clock::now();
    auto start_rtpg = std::chrono::high_resolution_clock::now();

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

    // Step B: RTPG stage (binary patterns).
    if ( rtpg_ver == 1) {
        int no_progress = 0;
        // Cap: stop RTPG after this many consecutive patterns that detect
        // zero new faults.  Prevents infinite loop when random coverage
        // saturates below rtpg_fc_limit.
        const int MAX_NO_PROGRESS = 10 * total_faults;
        while ( !flist.empty() ) {
            std::vector<int> pat(Npi, 0);
            for (int i = 0; i < Npi; i++) pat[i] = std::rand() & 1;
            if (std::find(final_tps.begin(), final_tps.end(), pat) != final_tps.end()) continue;
            int prev_sz = (int)flist.size();
            final_tps.push_back(pat);
            tpg_drop_detected_faults(pat, flist);

            if ((int)flist.size() == prev_sz) {
                if (++no_progress >= MAX_NO_PROGRESS) break;  // coverage stalled
            } else {
                no_progress = 0;
            }

            covered = total_faults - (int)flist.size();
            double fc = 100.0 * covered / total_faults;
            // printf("Fault Coverage: %f\n", fc);
            if ( fc > rtpg_fc_limit ) break;
        }
    } else if ( rtpg_ver == 2 ) {
        double prev_fc = 0.0;
        while ( !flist.empty() ) {
            std::vector<int> pat(Npi, 0);
            for (int i = 0; i < Npi; i++) pat[i] = std::rand() & 1;
            if (std::find(final_tps.begin(), final_tps.end(), pat) != final_tps.end()) continue;
            final_tps.push_back(pat);
            tpg_drop_detected_faults(pat, flist);

            covered = total_faults - (int)flist.size();
            double fc = 100.0 * covered / total_faults;
            // printf("Fault Coverage: %f\n", fc);
            if ( fc - prev_fc < rtpg_fc_limit ) break;
            prev_fc = fc;
        }
    } else if ( rtpg_ver == 3 ) {
        double prev_fc = 0.0;
        double k = 0.0;
        double avg_fc_imp = 0.0;    // average fault coverage improvement
        while ( !flist.empty() ) {
            std::vector<int> pat(Npi, 0);
            for (int i = 0; i < Npi; i++) pat[i] = std::rand() & 1;
            if (std::find(final_tps.begin(), final_tps.end(), pat) != final_tps.end()) continue;
            final_tps.push_back(pat);
            tpg_drop_detected_faults(pat, flist);

            k++;
            covered = total_faults - (int)flist.size();
            double fc = 100.0 * covered / total_faults;
            // printf("Fault Coverage: %f\n", fc);
            double fc_imp = fc - prev_fc;
            avg_fc_imp += (fc_imp - avg_fc_imp) / k;
            prev_fc = fc;
            if ( k < K_TPS ) continue;
            if ( avg_fc_imp < rtpg_fc_limit ) break;
            k = 0;
            avg_fc_imp = 0.0;
        }
        
    } else if ( rtpg_ver == 4 ) {  
        double prev_fc = 0.0; 
        while ( !flist.empty() ) {  
            std::vector<std::vector<int>> pats(Q_TPS, std::vector<int>(Npi));
            std::vector<int> detFaults(Q_TPS, 0);

            // Generate random TPs and check how many faults they cover
            for (int i = 0; i < Q_TPS; i++){
                for (int j = 0; j < Npi; j++) pats[i][j] = std::rand() & 1;
                if (std::find(final_tps.begin(), final_tps.end(), pats[i]) != final_tps.end()) continue;
                detFaults[i] = num_detect_faults(pats[i], flist);
            }
            
            // pick the best TP
            auto it = std::max_element(detFaults.begin(), detFaults.end());
            int index = std::distance(detFaults.begin(), it);
            final_tps.push_back(pats[index]);
            tpg_drop_detected_faults(pats[index], flist);

            // check to see if we should break
            covered = total_faults - (int)flist.size();
            double fc = 100.0 * covered / total_faults;
            // printf("Fault Coverage: %f\n", fc);
            if ( fc - prev_fc < rtpg_fc_limit ) break;
            prev_fc = fc;
        }
    }
    auto end_rtpg = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_rtpg = end_rtpg - start_rtpg;

    // Step C: Deterministic ATPG stage on remaining faults.

    // Lower the DALG backtrack limit for TPG: hard/redundant faults that
    // cannot be solved quickly are skipped rather than burning 500K backtracks.
    const int TPG_BACKTRACK_LIMIT = 500;
    int saved_bt_limit = dalg_get_backtrack_limit();
    dalg_set_backtrack_limit(TPG_BACKTRACK_LIMIT);

    // Ensure SCOAP values are computed when needed for fault ordering.
    if (fo_mode == FO_SCOAP_EASY || fo_mode == FO_SCOAP_HARD) {
        bool need_scoap = false;
        for (int i = 0; i < Nnodes; i++) {
            if (Node[i].scoap.CC0 <= 0 || Node[i].scoap.CC1 <= 0 || Node[i].scoap.CO < 0) {
                need_scoap = true; break;
            }
        }
        if (need_scoap) dalg_compute_scoap_internal();
    }

    // Apply fault ordering heuristic to the remaining fault list.
    apply_fault_order(flist, fo_mode);

    // Iterate over a snapshot so dropping during the loop is safe.
    // Use a set for O(1) "still_there" lookups instead of O(n) linear scan.
    std::vector<std::pair<int,int>> pending = flist;
    auto make_key = [](int node, int sa) -> long long { return ((long long)node << 2) | sa; };
    for (const auto &f : pending) {
        if (flist.empty()) break;
        // Rebuild the lookup set from flist (it shrinks after each drop).
        // This is O(flist) per iteration but avoids O(pending × flist) overall.
        std::unordered_set<long long> fset;
        fset.reserve(flist.size());
        for (const auto &g : flist) fset.insert(make_key(g.first, g.second));
        if (fset.find(make_key(f.first, f.second)) == fset.end()) continue;

        std::vector<int> tp;
        bool ok = use_dalg ? run_dalg_internal(f.first, f.second, tp)
                           : run_podem_internal(f.first, f.second, tp);
        if (!ok || (int)tp.size() != Npi) continue;

        final_tps.push_back(tp);
        tpg_drop_detected_faults(tp, flist);
        covered = total_faults - (int)flist.size();
        double fc = 100.0 * covered / total_faults;
        // printf("Fault Coverage (ATPG): %f\n", fc);
        if ( fc > FC_LIM ) break;
    }

    // Restore original backtrack limit.
    dalg_set_backtrack_limit(saved_bt_limit);

    if (!tpg_write_tp_file(outfile, final_tps)) {
        printf("Cannot open output file!\n");
        return;
    }

    covered = total_faults - (int)flist.size();
    double fc = total_faults ? (100.0 * covered / total_faults) : 0.0;

    auto end_full = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_full = end_full - start_full;

    const char *fo_name = (fo_mode == FO_RFL) ? "rfl" :
                          (fo_mode == FO_SCOAP_EASY) ? "scoap_easy" :
                          (fo_mode == FO_SCOAP_HARD) ? "scoap_hard" : "none";
    printf("TPG: alg=%s rtpg_ver=%d fo=%s tps=%d FC=%.2f%% (%d/%d), RTPG time=%fs, full time=%fs\n",
           use_dalg ? "DALG" : "PODEM",
           rtpg_ver, fo_name, (int)final_tps.size(), fc, covered, total_faults, elapsed_rtpg.count(), elapsed_full.count());
    printf("==> OK");
}