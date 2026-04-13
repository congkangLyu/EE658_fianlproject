#ifndef TPG_H
#define TPG_H

#define FC_LIM 97.5   // fault coverage % limit for test patterns
#define K_TPS 5   // 'K' test patters to check before checking average improvement
#define Q_TPS 5   // 'Q' test patterns to check before choosing the best one out of Q patterns

// Fault order heuristic codes
#define FO_NONE  0   // no fault ordering (baseline: iterate in circuit order)
#define FO_RFL   1   // RFL-first: PI & fanout-branch faults first, then rest
#define FO_SCOAP_EASY 2  // SCOAP easy-first: lowest testability cost first
#define FO_SCOAP_HARD 3  // SCOAP hard-first: highest testability cost first

void tpg();

#endif /* TPG_H */
