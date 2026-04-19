#ifndef PODEM_H
#define PODEM_H

void podem();

// Backtrack budget controls (mirror dalg_set/get_backtrack_limit).
// TPG lowers this so redundant/hard faults don't hang the whole run.
void podem_set_backtrack_limit(int limit);
int  podem_get_backtrack_limit();
// Clear the backtrack counter and abort flag before a fresh search.
void podem_reset_search();

#endif /* PODEM_H */
