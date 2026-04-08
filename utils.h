#ifndef UTILS_H
#define UTILS_H

#include "globals.h"

/*=======================================================================
  Utility functions shared across multiple commands.
=======================================================================*/

/* Gate name lookup */
std::string gname(int tp);

/* String utilities - rstrip only */

/* Gate evaluation - used by logicsim, rfl, pfs, pode, dlag */
int eval_gate_from_inputs(NSTRUC *np, const std::vector<int> &val);

/* String trimming - used by multiple commands */
void rstrip(char *s);

#endif /* UTILS_H */
