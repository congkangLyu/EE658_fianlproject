#include "dfront.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

static std::vector<std::string> split_tokens(const char *args) {
    std::vector<std::string> toks;
    if (!args) return toks;

    std::string cur;
    for (const char *p = args; *p; ++p) {
        if (*p == ' ' || *p == '\t') {
            if (!cur.empty()) {
                toks.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(*p);
        }
    }
    if (!cur.empty()) toks.push_back(cur);
    return toks;
}

DFrontierMode dfront_mode_from_args(const char *args) {
    DFrontierMode mode = DF_BASELINE;
    std::vector<std::string> toks = split_tokens(args);

    for (size_t i = 0; i < toks.size(); i++) {
        if (toks[i] == "-df" && i + 1 < toks.size()) {
            const std::string &v = toks[++i];
            if      (v == "nl") mode = DF_NL;
            else if (v == "nh") mode = DF_NH;
            else if (v == "lh") mode = DF_LH;
            else if (v == "cc") mode = DF_CC;
        }
    }

    return mode;
}

long long dfront_priority(const NSTRUC *np, DFrontierMode mode) {
    if (!np) return 0;

    switch (mode) {
        case DF_NL: return (long long)np->num;
        case DF_NH: return -(long long)np->num;
        case DF_LH: return -(long long)np->level;
        case DF_CC: return (long long)np->scoap.CO;
        case DF_BASELINE:
        default:
            return 0;
    }
}

std::vector<NSTRUC*> dfront_ranked(const std::vector<NSTRUC*> &cands,
                                   DFrontierMode mode) {
    std::vector<NSTRUC*> out = cands;
    if (out.empty() || mode == DF_BASELINE) return out;

    std::stable_sort(out.begin(), out.end(),
                     [mode](NSTRUC *a, NSTRUC *b) {
                         return dfront_priority(a, mode) < dfront_priority(b, mode);
                     });
    return out;
}

void dfront() {
    printf("D-frontier heuristic options:\n");
    printf("  -df nl : lowest node number first\n");
    printf("  -df nh : highest node number first\n");
    printf("  -df lh : highest level first\n");
    printf("  -df cc : lowest SCOAP CO first\n");
    printf("Usage examples:\n");
    printf("  DALG <fault_node> <sa(0|1)> <outfile> -df lh\n");
    printf("  PODEM <fault_node> <sa(0|1)> <outfile> -df cc\n");
}

