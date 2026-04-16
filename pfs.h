#ifndef PFS_H
#define PFS_H

#include <vector>
#include <utility>

void pfs();

/* In-memory parallel fault simulation for TPG fault-dropping.
 * Inputs:
 *   pi_pattern : primary-input assignment indexed by Pinput order (values 0/1; LX treated as 0).
 *   faults     : list of (node_num, stuck_value) pairs to test under pi_pattern.
 * Output:
 *   is_detected: resized to faults.size(); entry i set to 1 iff faults[i] is detected
 *                (good PO value differs from faulty PO value) on any PO, else 0.
 * Notes:
 *   - Does not perform any file I/O.
 *   - Does not modify the circuit state beyond the internal simulation word arrays.
 *   - Uses 64-bit words; packs up to 63 faults per internal pass (bit 0 reserved for good value).
 */
void pfs_detect_batch(const std::vector<int> &pi_pattern,
                      const std::vector<std::pair<int,int> > &faults,
                      std::vector<char> &is_detected);

#endif /* PFS_H */
