#include "dalg.h"
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
#include <limits>
#include <numeric>
#include <functional>
#include <deque>

/*=====================================================================
 *                        5-valued logic primitives
 *===================================================================*/
enum Val5 { VV0 = 0, VV1 = 1, VVX = 2, VVD = 3, VVDB = 4 };

static inline int good_of(int v) {
    if (v == VV0 || v == VVDB) return 0;
    if (v == VV1 || v == VVD)  return 1;
    return LX;
}
static inline int faulty_of(int v) {
    if (v == VV0 || v == VVD)  return 0;
    if (v == VV1 || v == VVDB) return 1;
    return LX;
}
static inline int make5(int g, int f) {
    if (g == LX || f == LX) return VVX;
    if (g == 0 && f == 0) return VV0;
    if (g == 1 && f == 1) return VV1;
    if (g == 1 && f == 0) return VVD;
    return VVDB;  // g=0, f=1
}
static inline bool is_error(int v)  { return v == VVD || v == VVDB; }
static inline bool is_known(int v)  { return v != VVX; }
static inline bool compatible(int a, int b) {
    // Two 5-valued values are compatible if they agree on both good and faulty,
    // allowing X to match anything on either side.
    int ag = good_of(a), af = faulty_of(a);
    int bg = good_of(b), bf = faulty_of(b);
    if (ag != LX && bg != LX && ag != bg) return false;
    if (af != LX && bf != LX && af != bf) return false;
    return true;
}
static inline int merge5(int a, int b) {
    // Intersect two compatible 5-valued values into the more specific one.
    int g = good_of(a);   if (g == LX) g = good_of(b);
    int f = faulty_of(a); if (f == LX) f = faulty_of(b);
    return make5(g, f);
}

/*=====================================================================
 *                        Gate type helpers
 *===================================================================*/
static inline int controlling_val(NSTRUC *np) {
    switch (np->type) {
        case AND: case NAND: return 0;
        case OR:  case NOR:  return 1;
        default: return -1;
    }
}
static inline int noncontrolling_val(NSTRUC *np) {
    int c = controlling_val(np);
    return (c == 0) ? 1 : (c == 1 ? 0 : -1);
}
static inline bool output_inverts(NSTRUC *np) {
    return (np->type == NAND || np->type == NOR || np->type == NOT);
}

/*=====================================================================
 *                  5-valued gate evaluation (forward)
 *===================================================================*/
static int eval5_gate(NSTRUC *np, const std::vector<int> &val) {
    auto gval = [&](int k){ return good_of(val[np->unodes[k]->indx]); };
    auto fval = [&](int k){ return faulty_of(val[np->unodes[k]->indx]); };

    auto eval_side = [&](const std::function<int(int)> &getv) -> int {
        int fin = (int)np->fin;
        switch (np->type) {
            case BRCH: return (fin > 0) ? getv(0) : LX;
            case NOT: { int v = getv(0); return (v == LX) ? LX : 1 - v; }
            case AND: {
                bool anyX = false;
                for (int i = 0; i < fin; i++) {
                    int v = getv(i);
                    if (v == 0) return 0;
                    if (v == LX) anyX = true;
                }
                return anyX ? LX : 1;
            }
            case NAND: {
                bool anyX = false;
                for (int i = 0; i < fin; i++) {
                    int v = getv(i);
                    if (v == 0) return 1;
                    if (v == LX) anyX = true;
                }
                return anyX ? LX : 0;
            }
            case OR: {
                bool anyX = false;
                for (int i = 0; i < fin; i++) {
                    int v = getv(i);
                    if (v == 1) return 1;
                    if (v == LX) anyX = true;
                }
                return anyX ? LX : 0;
            }
            case NOR: {
                bool anyX = false;
                for (int i = 0; i < fin; i++) {
                    int v = getv(i);
                    if (v == 1) return 0;
                    if (v == LX) anyX = true;
                }
                return anyX ? LX : 1;
            }
            case XOR: {
                int ones = 0;
                for (int i = 0; i < fin; i++) {
                    int v = getv(i);
                    if (v == LX) return LX;
                    ones += v;
                }
                return ones & 1;
            }
            default: return LX;
        }
    };

    int gv = eval_side(gval);
    int fv = eval_side(fval);
    return make5(gv, fv);
}

/*=====================================================================
 *                  Topology cache (built once per dalg() call)
 *===================================================================*/
struct TopoCache {
    std::vector<std::vector<int>> by_level; // nodes grouped by level
    std::vector<int> topo_order;            // ascending level order
    int max_level = 0;
    bool valid = false;
};
static TopoCache g_topo;

static void build_topo_cache() {
    g_topo.by_level.clear();
    g_topo.topo_order.clear();
    g_topo.max_level = 0;
    for (int i = 0; i < Nnodes; i++)
        if (Node[i].level > g_topo.max_level) g_topo.max_level = Node[i].level;
    g_topo.by_level.assign(g_topo.max_level + 1, {});
    for (int i = 0; i < Nnodes; i++)
        g_topo.by_level[Node[i].level].push_back(i);
    g_topo.topo_order.reserve(Nnodes);
    for (int l = 0; l <= g_topo.max_level; l++)
        for (int idx : g_topo.by_level[l]) g_topo.topo_order.push_back(idx);
    g_topo.valid = true;
}

int dalg_node_index_by_num(int node_num) {
    auto it = idx_of_num.find(node_num);
    if (it == idx_of_num.end()) return -1;
    return it->second;
}

/*=====================================================================
 *           SCOAP computation (topological-order, single pass)
 *===================================================================*/
static int cost_other_non_controlling(NSTRUC *np, int index) {
    int cost = 0;
    switch (np->type) {
        case NOT: case BRCH: break;
        case XOR: break; // treat as 0 (not in ISCAS-85 netlists)
        case OR: case NOR:
            for (int i = 0; i < (int)np->fin; i++) {
                if (i == index) continue;
                cost += np->unodes[i]->scoap.CC0;
            }
            break;
        case NAND: case AND:
            for (int i = 0; i < (int)np->fin; i++) {
                if (i == index) continue;
                cost += np->unodes[i]->scoap.CC1;
            }
            break;
        default: break;
    }
    return cost;
}

void dalg_compute_scoap_internal() {
    if (!g_topo.valid) build_topo_cache();
    int big = std::numeric_limits<int>::max() / 4;

    for (int i = 0; i < Nnodes; i++) {
        Node[i].scoap.CC0 = -1;
        Node[i].scoap.CC1 = -1;
        Node[i].scoap.CO  = big;
    }
    for (int i = 0; i < Npi; i++) { Pinput[i]->scoap.CC0 = 1; Pinput[i]->scoap.CC1 = 1; }

    // Forward pass (ascending level) - one iteration is enough.
    for (int l = 0; l <= g_topo.max_level; l++) {
        for (int idx : g_topo.by_level[l]) {
            NSTRUC *np = &Node[idx];
            if (np->ntype == PI) continue;
            switch (np->type) {
                case BRCH:
                    np->scoap.CC0 = np->unodes[0]->scoap.CC0;
                    np->scoap.CC1 = np->unodes[0]->scoap.CC1;
                    break;
                case NOT:
                    np->scoap.CC0 = np->unodes[0]->scoap.CC1 + 1;
                    np->scoap.CC1 = np->unodes[0]->scoap.CC0 + 1;
                    break;
                case OR: case NOR: case NAND: case AND: {
                    std::vector<int> v0, v1;
                    v0.reserve(np->fin); v1.reserve(np->fin);
                    for (int j = 0; j < (int)np->fin; j++) {
                        v0.push_back(np->unodes[j]->scoap.CC0);
                        v1.push_back(np->unodes[j]->scoap.CC1);
                    }
                    if (np->type == OR) {
                        np->scoap.CC0 = 1 + std::accumulate(v0.begin(), v0.end(), 0);
                        np->scoap.CC1 = 1 + *std::min_element(v1.begin(), v1.end());
                    } else if (np->type == NOR) {
                        np->scoap.CC1 = 1 + std::accumulate(v0.begin(), v0.end(), 0);
                        np->scoap.CC0 = 1 + *std::min_element(v1.begin(), v1.end());
                    } else if (np->type == NAND) {
                        np->scoap.CC1 = 1 + *std::min_element(v0.begin(), v0.end());
                        np->scoap.CC0 = 1 + std::accumulate(v1.begin(), v1.end(), 0);
                    } else {
                        np->scoap.CC0 = 1 + *std::min_element(v0.begin(), v0.end());
                        np->scoap.CC1 = 1 + std::accumulate(v1.begin(), v1.end(), 0);
                    }
                    break;
                }
                default: break;
            }
        }
    }

    for (int i = 0; i < Npo; i++) Poutput[i]->scoap.CO = 0;
    // Backward pass (descending level) - one iteration converges.
    for (int l = g_topo.max_level; l >= 0; l--) {
        for (int idx : g_topo.by_level[l]) {
            NSTRUC *np = &Node[idx];
            if (np->scoap.CO >= big) continue;
            for (int j = 0; j < (int)np->fin; j++) {
                NSTRUC *in = np->unodes[j];
                int contrib = np->scoap.CO + cost_other_non_controlling(np, j);
                if (np->type != BRCH) contrib += 1;
                if (contrib < in->scoap.CO) in->scoap.CO = contrib;
            }
        }
    }
}

/*=====================================================================
 *              D-Algorithm state, trail, and frontiers
 *===================================================================*/
struct TrailEntry {
    enum Kind { VAL, DF_ADD, DF_DEL, JF_ADD, JF_DEL } kind;
    int key;
    int old_val;  // only for VAL
};

struct DalgState {
    std::vector<int> val;                   // 5-valued, indexed by node idx
    std::unordered_set<int> d_frontier;     // gate indices
    std::unordered_set<int> j_frontier;     // gate (or branch) indices needing justification
    std::vector<TrailEntry> trail;
    int fault_idx = -1;
    int stuck_at = 0;
    std::vector<char> fault_cone;           // 1 if node is in fanout cone of fault
};
static DalgState g_st;

static inline size_t checkpoint() { return g_st.trail.size(); }

static void restore_to(size_t cp) {
    while (g_st.trail.size() > cp) {
        const TrailEntry &e = g_st.trail.back();
        switch (e.kind) {
            case TrailEntry::VAL:    g_st.val[e.key] = e.old_val; break;
            case TrailEntry::DF_ADD: g_st.d_frontier.erase(e.key); break;
            case TrailEntry::DF_DEL: g_st.d_frontier.insert(e.key); break;
            case TrailEntry::JF_ADD: g_st.j_frontier.erase(e.key); break;
            case TrailEntry::JF_DEL: g_st.j_frontier.insert(e.key); break;
        }
        g_st.trail.pop_back();
    }
}

static inline void log_val(int idx, int old) {
    g_st.trail.push_back({TrailEntry::VAL, idx, old});
}
static inline void df_add(int idx) {
    if (g_st.d_frontier.insert(idx).second)
        g_st.trail.push_back({TrailEntry::DF_ADD, idx, 0});
}
static inline void df_del(int idx) {
    if (g_st.d_frontier.erase(idx))
        g_st.trail.push_back({TrailEntry::DF_DEL, idx, 0});
}
static inline void jf_add(int idx) {
    if (g_st.j_frontier.insert(idx).second)
        g_st.trail.push_back({TrailEntry::JF_ADD, idx, 0});
}
static inline void jf_del(int idx) {
    if (g_st.j_frontier.erase(idx))
        g_st.trail.push_back({TrailEntry::JF_DEL, idx, 0});
}

/* A gate g is on the D-frontier iff:
 *   - val[g] == VVX
 *   - at least one input has an error value (D or D')
 */
static bool is_d_frontier_gate(NSTRUC *np) {
    if (np->ntype == PI) return false;
    if (g_st.val[np->indx] != VVX) return false;
    for (int k = 0; k < (int)np->fin; k++) {
        if (is_error(g_st.val[np->unodes[k]->indx])) return true;
    }
    return false;
}

/* A line (gate output) is on the J-frontier iff:
 *   - val[l] is known (0 or 1) and not yet consistent with its gate output
 *     computed from its inputs (i.e., needs at least one more input decision)
 * For PIs: never in J-frontier. */
static bool is_j_frontier_gate(NSTRUC *np) {
    if (np->ntype == PI) return false;
    int v = g_st.val[np->indx];
    if (!is_known(v)) return false;
    int ev = eval5_gate(np, g_st.val);
    if ((int)np->indx == g_st.fault_idx) {
        // At the fault site, the faulty side is pinned by stuck-at;
        // only the good side has to be justified from the inputs.
        int need = good_of(v);
        int got  = good_of(ev);
        return got != need;
    }
    if (ev == v) return false;
    return true;
}

static void refresh_gate_frontiers(int gidx) {
    NSTRUC *np = &Node[gidx];
    if (is_d_frontier_gate(np)) df_add(gidx); else df_del(gidx);
    if (is_j_frontier_gate(np)) jf_add(gidx); else jf_del(gidx);
}

/*=====================================================================
 *              Forward implication with queue
 *===================================================================*/
// Attempt to set val[idx] = newv; returns false on conflict.
static bool assign_and_imply(int start_idx, int newv, std::deque<int> &queue);

// Evaluate forward from an imply queue. Returns false on any conflict.
static bool imply_forward(std::deque<int> &queue) {
    while (!queue.empty()) {
        int idx = queue.front(); queue.pop_front();
        NSTRUC *np = &Node[idx];
        // Re-evaluate every fanout gate of np.
        for (int k = 0; k < (int)np->fout; k++) {
            NSTRUC *g = np->dnodes[k];
            int gidx = g->indx;
            int newv = eval5_gate(g, g_st.val);
            int cur  = g_st.val[gidx];

            // Fault-site pinning: its value is fixed as VVD or VVDB by the
            // initial injection. The faulty side is *forced* by stuck-at,
            // so imply_forward must only check good-side consistency and
            // must never overwrite the pinned 5-valued value.
            if (gidx == g_st.fault_idx) {
                int got_g  = good_of(newv);
                int want_g = good_of(cur);
                if (got_g != LX && want_g != LX && got_g != want_g) return false;
                refresh_gate_frontiers(gidx);
                continue;
            }

            if (cur == VVX) {
                if (newv != VVX) {
                    // Fill in new value
                    log_val(gidx, cur);
                    g_st.val[gidx] = newv;
                    queue.push_back(gidx);
                }
            } else {
                // cur is known/D/D'. The newly computed newv must be compatible.
                if (newv != VVX && !compatible(cur, newv)) return false;
                // If newv is more specific (compatible) than cur, tighten.
                int merged = (newv == VVX) ? cur : merge5(cur, newv);
                if (merged != cur) {
                    log_val(gidx, cur);
                    g_st.val[gidx] = merged;
                    queue.push_back(gidx);
                }
            }
            refresh_gate_frontiers(gidx);
        }
    }
    return true;
}

static bool assign_and_imply(int start_idx, int newv, std::deque<int> &queue) {
    int cur = g_st.val[start_idx];
    if (cur == newv) return true;
    if (cur != VVX) {
        if (!compatible(cur, newv)) return false;
        newv = merge5(cur, newv);
        if (newv == cur) return true;
    }
    log_val(start_idx, cur);
    g_st.val[start_idx] = newv;
    refresh_gate_frontiers(start_idx);
    queue.push_back(start_idx);
    return imply_forward(queue);
}

static bool assign_and_imply(int start_idx, int newv) {
    std::deque<int> q;
    return assign_and_imply(start_idx, newv, q);
}

/*=====================================================================
 *              X-path pruning (from any D-frontier to some PO)
 *===================================================================*/
static bool xpath_to_po_exists() {
    // Any D already at a PO?
    for (int i = 0; i < Npo; i++) {
        if (is_error(g_st.val[Poutput[i]->indx])) return true;
    }
    if (g_st.d_frontier.empty()) return false;

    // BFS from each D-frontier gate through X/D nodes.
    std::vector<char> vis(Nnodes, 0);
    std::deque<int> q;
    for (int gidx : g_st.d_frontier) { q.push_back(gidx); vis[gidx] = 1; }

    while (!q.empty()) {
        int idx = q.front(); q.pop_front();
        NSTRUC *np = &Node[idx];
        // A PO on the frontier counts as reachable.
        if (np->ntype == PO) return true;
        // Some circuits flag POs via Poutput only; double-check below.
        for (int k = 0; k < (int)np->fout; k++) {
            NSTRUC *g = np->dnodes[k];
            int gi = g->indx;
            if (vis[gi]) continue;
            int v = g_st.val[gi];
            if (v == VVX || is_error(v)) {
                vis[gi] = 1;
                q.push_back(gi);
            }
        }
    }
    // Fallback: any Poutput visited?
    for (int i = 0; i < Npo; i++) if (vis[Poutput[i]->indx]) return true;
    return false;
}

/*=====================================================================
 *              Phase 4 selection strategies
 *===================================================================*/
struct DalgOptions {
    enum DFSel { DF_DEFAULT, DF_NL, DF_NH, DF_LH, DF_CC } df_sel = DF_DEFAULT;
    enum JFSel { JF_DEFAULT, JF_V0 } jf_sel = JF_DEFAULT;
    int  backtrack_limit = 500000;   // safety cap (XOR-heavy circuits like c1355/c499
                                     // are a known weakness of D-algorithm; on those
                                     // this returns false and the caller can fall back
                                     // to PODEM)
    int  recursion_limit = 4000;     // max recursion depth before bailing out. Each
                                     // frame is ~0.5-1KB (locals + a small vector), so
                                     // 4000 ≈ 4MB of stack worst-case, safely under the
                                     // typical 8MB soft cap on Linux.
};
static DalgOptions g_opts;

static int select_d_frontier_gate() {
    if (g_st.d_frontier.empty()) return -1;
    int best = -1;
    for (int gidx : g_st.d_frontier) {
        if (best < 0) { best = gidx; continue; }
        NSTRUC *a = &Node[gidx];
        NSTRUC *b = &Node[best];
        bool pick = false;
        switch (g_opts.df_sel) {
            case DalgOptions::DF_NL: pick = (a->num < b->num); break;
            case DalgOptions::DF_NH: pick = (a->num > b->num); break;
            case DalgOptions::DF_LH: pick = (a->level > b->level); break;
            case DalgOptions::DF_CC: pick = (a->scoap.CO < b->scoap.CO); break;
            case DalgOptions::DF_DEFAULT:
            default:                 pick = (a->level > b->level); break; // sensible default
        }
        if (pick) best = gidx;
    }
    return best;
}

// Choose which input of a J-frontier gate to assign next, and what value.
// Returns index (0..fin-1) of the input to branch on. The caller tries
// the two values: controlling-value to "break" the gate, then non-controlling.
static int select_j_input(NSTRUC *np) {
    // Scan X-valued inputs and pick by strategy.
    int best = -1;
    for (int k = 0; k < (int)np->fin; k++) {
        if (g_st.val[np->unodes[k]->indx] != VVX) continue;
        if (best < 0) { best = k; continue; }
        NSTRUC *a = np->unodes[k];
        NSTRUC *b = np->unodes[best];
        bool pick = false;
        switch (g_opts.jf_sel) {
            case DalgOptions::JF_V0: {
                int ca = std::min(a->scoap.CC0, a->scoap.CC1);
                int cb = std::min(b->scoap.CC0, b->scoap.CC1);
                pick = (ca < cb);
                break;
            }
            default: pick = false; // first encountered wins
        }
        if (pick) best = k;
    }
    return best;
}

/*=====================================================================
 *              Error-at-PO test
 *===================================================================*/
static bool error_at_any_po() {
    for (int i = 0; i < Npo; i++)
        if (is_error(g_st.val[Poutput[i]->indx])) return true;
    return false;
}

/*=====================================================================
 *              Recursive D-Algorithm core
 *===================================================================*/
static long g_bt_count = 0;
static int  g_rec_depth = 0;
static bool g_aborted   = false;   // once true, every frame returns false ASAP

// RAII guard so we never forget to decrement the depth counter when a frame
// exits early (any return path, including after a failed recursive call).
struct DepthGuard {
    DepthGuard()  { ++g_rec_depth; }
    ~DepthGuard() { --g_rec_depth; }
};

static bool dalg_recursive();

// Try to justify a single J-frontier line `np` with required known value v.
// For AND/OR style gates with controlling/non-controlling semantics we pick
// one input at a time. For NOT/BRCH the value is propagated mechanically.
static bool justify_gate(NSTRUC *np) {
    int required = g_st.val[np->indx];
    if (!is_known(required)) return true;

    // Simple cases first.
    int req_good0 = good_of(required);
    if (np->type == BRCH) {
        // Branch: stem must carry the same good value.
        int want = (req_good0 == LX) ? VVX : (req_good0 == 0 ? VV0 : VV1);
        return assign_and_imply(np->unodes[0]->indx, want) && dalg_recursive();
    }
    if (np->type == NOT) {
        if (req_good0 == LX) return dalg_recursive();
        int need = (req_good0 == 0) ? VV1 : VV0;
        return assign_and_imply(np->unodes[0]->indx, need) && dalg_recursive();
    }
    if (np->type == XOR) {
        // Not used in ISCAS-85; fall back to forcing any X-input to 0 and recurse.
        int k = select_j_input(np);
        if (k < 0) return true; // already justified by implication
        size_t cp = checkpoint();
        if (assign_and_imply(np->unodes[k]->indx, 0) && dalg_recursive()) return true;
        restore_to(cp);
        cp = checkpoint();
        if (assign_and_imply(np->unodes[k]->indx, 1) && dalg_recursive()) return true;
        restore_to(cp);
        return false;
    }

    // AND/NAND/OR/NOR.
    int ctrl    = controlling_val(np);
    int nonctrl = noncontrolling_val(np);
    bool out_inv = output_inverts(np);
    // Justify the good side only. At fault sites the faulty side is pinned
    // by stuck-at; outside the fault cone good == faulty anyway.
    int req_good = good_of(required);
    if (req_good == LX) return dalg_recursive();
    int eff_req = out_inv ? (1 - req_good) : req_good;

    if (eff_req == nonctrl) {
        // All X-inputs must be non-controlling: force them (no decision).
        for (int k = 0; k < (int)np->fin; k++) {
            int v = g_st.val[np->unodes[k]->indx];
            if (v == VVX) {
                if (!assign_and_imply(np->unodes[k]->indx, nonctrl)) return false;
            } else if (v != VVX && good_of(v) != nonctrl && good_of(v) != LX) {
                return false; // conflict: should have been caught earlier
            }
        }
        return dalg_recursive();
    }

    // eff_req == ctrl: at least one input must be controlling.
    int k = select_j_input(np);
    if (k < 0) {
        // No X-input: the gate must already be consistent.
        return dalg_recursive();
    }
    size_t cp = checkpoint();
    // Try controlling value on the selected input.
    if (assign_and_imply(np->unodes[k]->indx, ctrl) && dalg_recursive()) return true;
    restore_to(cp);
    // Try non-controlling on that input (other inputs will need to carry it).
    cp = checkpoint();
    if (assign_and_imply(np->unodes[k]->indx, nonctrl) && dalg_recursive()) return true;
    restore_to(cp);
    return false;
}

// Rank D-frontier gates according to the active strategy. Returns a sorted
// list (best first) so that the recursion can try alternatives on failure.
static std::vector<int> ranked_d_frontier() {
    std::vector<int> cands(g_st.d_frontier.begin(), g_st.d_frontier.end());
    auto key = [](int gidx) -> long long {
        NSTRUC *np = &Node[gidx];
        switch (g_opts.df_sel) {
            case DalgOptions::DF_NL: return (long long) np->num;
            case DalgOptions::DF_NH: return -(long long) np->num;
            case DalgOptions::DF_LH: return -(long long) np->level;
            case DalgOptions::DF_CC: return (long long) np->scoap.CO;
            default:                 return -(long long) np->level;
        }
    };
    std::stable_sort(cands.begin(), cands.end(),
                     [&](int a, int b){ return key(a) < key(b); });
    return cands;
}

// Try each D-frontier gate in rank order. For a chosen gate, assign all X
// inputs to its non-controlling value and recurse.
static bool propagate_d_frontier() {
    std::vector<int> cands = ranked_d_frontier();
    for (int gidx : cands) {
        NSTRUC *np = &Node[gidx];
        int nonctrl = noncontrolling_val(np);

        size_t cp = checkpoint();
        bool ok = true;

        if (nonctrl < 0) {
            // NOT / BRCH: error propagates mechanically via imply_forward.
            // XOR: try one X-input with 0 then 1.
            if (np->type == XOR) {
                int k = -1;
                for (int i = 0; i < (int)np->fin; i++)
                    if (g_st.val[np->unodes[i]->indx] == VVX) { k = i; break; }
                if (k >= 0) {
                    size_t cp2 = checkpoint();
                    if (assign_and_imply(np->unodes[k]->indx, 0) &&
                        dalg_recursive()) return true;
                    restore_to(cp2);
                    cp2 = checkpoint();
                    if (assign_and_imply(np->unodes[k]->indx, 1) &&
                        dalg_recursive()) return true;
                    restore_to(cp2);
                    continue; // try next D-frontier gate
                }
            }
            // NOT/BRCH (or XOR with no X-inputs): re-evaluate from inputs and
            // assign the result directly. If this single-input gate is still in
            // the D-frontier, imply has not yet propagated through it — do it
            // now, otherwise a plain recursion would see the same state and
            // loop forever.
            int ev = eval5_gate(np, g_st.val);
            if (ev == VVX) {
                // No way to make progress from here: inputs don't yet force a
                // value. Drop this candidate and try the next.
                restore_to(cp);
                continue;
            }
            if (!assign_and_imply(gidx, ev)) { restore_to(cp); continue; }
            if (dalg_recursive()) return true;
            restore_to(cp);
            continue;
        }

        for (int k = 0; k < (int)np->fin; k++) {
            int v = g_st.val[np->unodes[k]->indx];
            if (v == VVX) {
                if (!assign_and_imply(np->unodes[k]->indx, nonctrl)) {
                    ok = false; break;
                }
            }
        }
        if (ok && dalg_recursive()) return true;
        restore_to(cp);
    }
    return false;
}

static bool dalg_recursive() {
    if (g_aborted) return false;   // fast unwind once the backtrack budget fires
    DepthGuard _dg;
    // recursion_limit is a stack-safety cap: it aborts THIS path only so the
    // search can try sibling alternatives instead of crashing.
    if (g_rec_depth > g_opts.recursion_limit) return false;
    if (++g_bt_count > g_opts.backtrack_limit) { g_aborted = true; return false; }

    // (1) Pruning: X-path must still exist unless error already at PO.
    if (!error_at_any_po() && !xpath_to_po_exists()) return false;

    // (2) Success: error at PO and J-frontier empty.
    if (error_at_any_po() && g_st.j_frontier.empty()) return true;

    // (3) Otherwise: pick next action.
    //     Prefer D-frontier propagation if the error has not reached a PO yet.
    if (!error_at_any_po()) {
        return propagate_d_frontier();
    }

    // Error reached a PO but some J-frontier lines still unjustified.
    // Pick one (SCOAP-guided) and try to justify it.
    int best = -1; int best_score = std::numeric_limits<int>::max();
    for (int jidx : g_st.j_frontier) {
        NSTRUC *np = &Node[jidx];
        int score = std::min(np->scoap.CC0, np->scoap.CC1);
        if (score < best_score) { best_score = score; best = jidx; }
    }
    if (best < 0) return true; // shouldn't happen: j_frontier empty handled above
    return justify_gate(&Node[best]);
}

/*=====================================================================
 *              Fault injection and top-level entry
 *===================================================================*/
static void build_fault_cone(int fault_idx) {
    g_st.fault_cone.assign(Nnodes, 0);
    std::deque<int> q;
    q.push_back(fault_idx); g_st.fault_cone[fault_idx] = 1;
    while (!q.empty()) {
        int i = q.front(); q.pop_front();
        NSTRUC *np = &Node[i];
        for (int k = 0; k < (int)np->fout; k++) {
            int c = np->dnodes[k]->indx;
            if (!g_st.fault_cone[c]) { g_st.fault_cone[c] = 1; q.push_back(c); }
        }
    }
}

// Returns true on success and writes the binary PI assignment to pi_out
// (size Npi), unassigned PIs remain as LX.
static bool dalg_generate_test(int fault_num, int stuck_at, std::vector<int> &pi_out) {
    pi_out.assign(Npi, LX);

    int fault_idx = dalg_node_index_by_num(fault_num);
    if (fault_idx < 0) return false;

    // Initialize state
    g_st.val.assign(Nnodes, VVX);
    g_st.d_frontier.clear();
    g_st.j_frontier.clear();
    g_st.trail.clear();
    g_st.fault_idx = fault_idx;
    g_st.stuck_at = stuck_at;
    build_fault_cone(fault_idx);
    g_bt_count = 0;
    g_rec_depth = 0;
    g_aborted = false;

    // Inject the error: set val[fault] = D or D'.
    NSTRUC *fn = &Node[fault_idx];
    int err = (stuck_at == 0) ? VVD : VVDB;  // SA0 => good=1, faulty=0 => D
    {
        // We place the error directly; fault line still needs justification
        // of its GOOD value by its drivers (unless it's a PI).
        g_st.val[fault_idx] = err;
        g_st.trail.push_back({TrailEntry::VAL, fault_idx, VVX});
        // Propagate D forward through all fanouts via imply.
        std::deque<int> q;
        q.push_back(fault_idx);
        if (!imply_forward(q)) return false;
        // Fault site itself enters J-frontier unless it is a PI.
        if (fn->ntype != PI) jf_add(fault_idx);
        refresh_gate_frontiers(fault_idx);
    }

    // Run the recursive D-algorithm.
    bool r = dalg_recursive();
    if (!r) return false;

    // Read off PI assignments. For each PI, the good value (if known) goes out.
    for (int i = 0; i < Npi; i++) {
        int v = g_st.val[Pinput[i]->indx];
        int g = good_of(v);
        pi_out[i] = (g == LX) ? LX : g;
    }
    return true;
}

/*=====================================================================
 *              Legacy 3-valued simulation helpers (for tpg.cpp)
 *===================================================================*/
struct dalgSimResult { std::vector<int> good; std::vector<int> faulty; };

dalgSimResult dalg_simulate_pattern(const std::vector<int> &assign,
                                    int fault_node_num, int stuck_at) {
    if (!g_topo.valid) build_topo_cache();
    dalgSimResult res;
    res.good.assign(Nnodes, LX);
    res.faulty.assign(Nnodes, LX);

    for (int i = 0; i < Npi; i++) {
        int v = (i < (int)assign.size()) ? assign[i] : LX;
        int idx = Pinput[i]->indx;
        res.good[idx]   = v;
        res.faulty[idx] = ((int)Pinput[i]->num == fault_node_num) ? stuck_at : v;
    }

    for (int l = 0; l <= g_topo.max_level; l++) {
        for (int idx : g_topo.by_level[l]) {
            NSTRUC *np = &Node[idx];
            if (np->ntype == PI) continue;
            int gv, fv;
            if (np->type == BRCH) {
                gv = res.good[np->unodes[0]->indx];
                fv = res.faulty[np->unodes[0]->indx];
            } else {
                gv = eval_gate_from_inputs(np, res.good);
                fv = eval_gate_from_inputs(np, res.faulty);
            }
            res.good[idx] = gv;
            res.faulty[idx] = ((int)np->num == fault_node_num) ? stuck_at : fv;
        }
    }
    return res;
}

bool dalg_pattern_detects_fault(const std::vector<int> &assign,
                                int fault_node_num, int stuck_at) {
    dalgSimResult s = dalg_simulate_pattern(assign, fault_node_num, stuck_at);
    for (int i = 0; i < Npo; i++) {
        int idx = Poutput[i]->indx;
        int g = s.good[idx], f = s.faulty[idx];
        if (g != LX && f != LX && g != f) return true;
    }
    return false;
}

// Kept for tpg.cpp compatibility. Not used by the new search.
bool dalg_can_still_activate(const std::vector<int> &assign,
                             int fault_node_num, int stuck_at) {
    int fi = dalg_node_index_by_num(fault_node_num);
    if (fi < 0) return false;
    dalgSimResult s = dalg_simulate_pattern(assign, fault_node_num, stuck_at);
    int g = s.good[fi], f = s.faulty[fi];
    if (g != LX && f != LX && g != f) return true;
    return (g == LX || f == LX);
}
bool dalg_has_possible_propagation(const std::vector<int> &assign,
                                   int fault_node_num, int stuck_at) {
    dalgSimResult s = dalg_simulate_pattern(assign, fault_node_num, stuck_at);
    for (int i = 0; i < Npo; i++) {
        int idx = Poutput[i]->indx;
        int g = s.good[idx], f = s.faulty[idx];
        if (g != LX && f != LX && g != f) return true;
        if (g == LX || f == LX) return true;
    }
    return false;
}

/*=====================================================================
 *              Ternary compression (keeps old Phase 3 behavior)
 *===================================================================*/
std::vector<int> dalg_compress_to_ternary(const std::vector<int> &binary_assign,
                                          int fault_node_num, int stuck_at) {
    std::vector<int> out = binary_assign;
    // Drop the "most expensive" PIs first for a better ternary result.
    std::vector<int> order(Npi);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [](int a, int b){
        NSTRUC *pa = Pinput[a]; NSTRUC *pb = Pinput[b];
        int ca = std::min(pa->scoap.CC0, pa->scoap.CC1) + pa->scoap.CO;
        int cb = std::min(pb->scoap.CC0, pb->scoap.CC1) + pb->scoap.CO;
        return ca > cb;
    });
    for (int i : order) {
        int oldv = out[i];
        if (oldv == LX) continue;
        out[i] = LX;
        if (!dalg_pattern_detects_fault(out, fault_node_num, stuck_at)) out[i] = oldv;
    }
    return out;
}

/*=====================================================================
 *              Legacy wrapper: dalg_backtrack_search
 *              (called by tpg.cpp). Delegates to the new D-algorithm.
 *===================================================================*/
bool dalg_backtrack_search(const std::vector<int> & /*order*/,
                           int /*depth*/,
                           std::vector<int> &assign,
                           int fault_node_num,
                           int stuck_at,
                           std::vector<int> &solution) {
    if (!dalg_generate_test(fault_node_num, stuck_at, assign)) return false;
    solution = assign;
    return true;
}

/*=====================================================================
 *              Output file writing
 *===================================================================*/
static bool write_tp_file(const char *outfile, const std::vector<int> &assign) {
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

/*=====================================================================
 *              Phase 4 option parsing for the dalG command
 *              Usage: dalG <node> <sa 0/1> <outfile> [-df nl|nh|lh|cc]
 *                                                   [-jf v0]
 *===================================================================*/
static void parse_dalg_options(const char *args) {
    g_opts = DalgOptions{};
    if (!args) return;
    std::vector<std::string> toks;
    std::string cur;
    for (const char *p = args; *p; ++p) {
        if (*p == ' ' || *p == '\t') {
            if (!cur.empty()) { toks.push_back(cur); cur.clear(); }
        } else cur.push_back(*p);
    }
    if (!cur.empty()) toks.push_back(cur);

    for (size_t i = 0; i < toks.size(); i++) {
        if (toks[i] == "-df" && i + 1 < toks.size()) {
            const std::string &s = toks[++i];
            if      (s == "nl") g_opts.df_sel = DalgOptions::DF_NL;
            else if (s == "nh") g_opts.df_sel = DalgOptions::DF_NH;
            else if (s == "lh") g_opts.df_sel = DalgOptions::DF_LH;
            else if (s == "cc") g_opts.df_sel = DalgOptions::DF_CC;
        } else if (toks[i] == "-jf" && i + 1 < toks.size()) {
            const std::string &s = toks[++i];
            if (s == "v0") g_opts.jf_sel = DalgOptions::JF_V0;
        }
    }
}

/*=====================================================================
 *              Top-level dalG command
 *===================================================================*/
void dalg() {
    int fault_num, sa_val;
    char outfile[MAXLINE];
    char rest[MAXLINE];
    rest[0] = '\0';
    rstrip(cp);

    int n = sscanf(cp, "%d %d %1023s %1023[^\n]",
                   &fault_num, &sa_val, outfile, rest);
    if (n < 3) { printf("Invalid Input!\n"); return; }
    if (sa_val != 0 && sa_val != 1) { printf("Invalid Input!\n"); return; }

    parse_dalg_options(n >= 4 ? rest : nullptr);

    // (Re)build topology and index maps (cheap, cached where possible).
    idx_of_num.clear();
    for (int i = 0; i < Nnodes; i++) idx_of_num[(int)Node[i].num] = i;
    build_topo_cache();

    if (idx_of_num.find(fault_num) == idx_of_num.end()) {
        FILE *fd = fopen(outfile, "w");
        if (fd) fclose(fd);
        printf("Invalid fault node!\n");
        return;
    }

    // Compute SCOAP if not already available (needed by heuristics).
    bool need_scoap = false;
    for (int i = 0; i < Nnodes; i++) {
        if (Node[i].scoap.CC0 <= 0 || Node[i].scoap.CC1 <= 0 ||
            Node[i].scoap.CO  <  0) { need_scoap = true; break; }
    }
    if (need_scoap) dalg_compute_scoap_internal();

    std::vector<int> pi_bin;
    bool ok = dalg_generate_test(fault_num, sa_val, pi_bin);

    if (!ok) {
        // Redundant (or untestable under backtrack limit) -> empty tp file.
        FILE *fd = fopen(outfile, "w");
        if (!fd) { printf("Cannot open output file!\n"); return; }
        fclose(fd);
        printf("==> OK");
        return;
    }

    std::vector<int> tern = dalg_compress_to_ternary(pi_bin, fault_num, sa_val);
    if (!write_tp_file(outfile, tern)) {
        printf("Cannot open output file!\n");
        return;
    }
    printf("==> OK");
}
