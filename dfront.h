#ifndef DFRONT_H
#define DFRONT_H

#include "globals.h"
#include <vector>

enum DFrontierMode {
    DF_BASELINE = 0, // keep current order
    DF_NL,           // lowest node number first
    DF_NH,           // highest node number first
    DF_LH,           // highest level first
    DF_CC            // lowest SCOAP observability first
};

// Parse optional "-df <nl|nh|lh|cc>" arguments and return selected mode.
DFrontierMode dfront_mode_from_args(const char *args);

// Return a sortable score (lower = better) for the selected heuristic.
long long dfront_priority(const NSTRUC *np, DFrontierMode mode);

// Sort D-frontier candidates using the selected heuristic.
std::vector<NSTRUC*> dfront_ranked(const std::vector<NSTRUC*> &cands,
                                   DFrontierMode mode);

#endif /* DFRONT_H */

