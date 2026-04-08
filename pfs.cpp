#include "pfs.h"
#include "globals.h"
#include "readckt.h"
#include "utils.h"
#include <stdio.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <limits>
#include <numeric>
#include <functional>
#include "static_helpers.h"


void pfs() {
    // [cmd$] PFS <input-tp-file> <input-fl-file> <output-fl-file>
    char tpfile[MAXLINE], flfile[MAXLINE], outfile[MAXLINE];
    rstrip(cp);

    if (sscanf(cp, "%1023s %1023s %1023s", tpfile, flfile, outfile) != 3) {
        printf("Invalid Input!\n");
        return;
    }

    FILE *fd_tp = fopen(tpfile, "r");
    if (!fd_tp) {
        printf("Input file does not exist!\n");
        return;
    }
    FILE *fd_fl = fopen(flfile, "r");
    if (!fd_fl) {
        printf("Input file does not exist!\n");
        fclose(fd_tp);
        return;
    }
    FILE *fd_out = fopen(outfile, "w");
    if (!fd_out) {
        printf("Cannot open output file!\n");
        fclose(fd_tp);
        fclose(fd_fl);
        return;
    }

    // Build node_num -> node_index map for O(1) lookup
    std::unordered_map<int, int> idx_of_num;
    idx_of_num.reserve((size_t)Nnodes * 2);
    for (int i = 0; i < Nnodes; i++) idx_of_num[(int)Node[i].num] = i;

    // max level (assume already levelized elsewhere in your flow)
    int max_level = 0;
    for (int i = 0; i < Nnodes; i++) max_level = std::max(max_level, (int)Node[i].level);

    // -------- read fault list (fl-file-format: "n@sa") ----------
    std::vector<std::pair<int,int>> faults; // (node_num, sa_val)
    {
        char buf[MAXLINE];
        while (fgets(buf, MAXLINE, fd_fl)) {
            trim_inplace(buf);
            if (buf[0] == '\0') continue;

            int n = 0, sa = 0;
            // tolerate spaces: " 12@1 "
            if (sscanf(buf, "%d@%d", &n, &sa) != 2) continue;
            if (sa != 0 && sa != 1) continue;
            if (idx_of_num.find(n) == idx_of_num.end()) continue; // skip unknown nodes

            faults.push_back({n, sa});
        }
    }

    // Nothing to do
    if (faults.empty()) {
        fclose(fd_tp);
        fclose(fd_fl);
        fclose(fd_out);
        printf("==> OK");
        return;
    }

    // We'll store detected faults uniquely
    auto enc_fault = [](int node_num, int sa_val) -> unsigned long long {
        return ((unsigned long long)(unsigned int)node_num << 1) | (unsigned long long)(sa_val & 1);
    };
    std::unordered_set<unsigned long long> detected;
    detected.reserve(faults.size() * 2);

    // -------- read TP header (PI ids line) ----------
    std::string header;
    if (!read_full_line(fd_tp, header)) {
        printf("Empty input file!\n");
        fclose(fd_tp);
        fclose(fd_fl);
        fclose(fd_out);
        return;
    }

    std::vector<int> pi_ids;
    {
        std::stringstream ss(header);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            tok.erase(0, tok.find_first_not_of(" \t"));
            tok.erase(tok.find_last_not_of(" \t") + 1);
            if (!tok.empty()) pi_ids.push_back(std::atoi(tok.c_str()));
        }
    }

    // -------- PFS core ----------
    using Word = unsigned long;
    const int W = (int)(sizeof(Word) * 8);        // machine word bits
    const int KMAX = W - 1;                       // faults per pass
    const int F = (int)faults.size();

    auto make_mask = [&](int bits)->Word {
        // bits in [1..W]
        if (bits >= W) return (Word)(~(Word)0);
        return ((Word)1 << bits) - (Word)1;
    };

    // Temp storage per pass
    std::vector<Word> val((size_t)Nnodes, 0);
    std::vector<Word> force0((size_t)Nnodes, 0);
    std::vector<Word> force1((size_t)Nnodes, 0);

    // Read TP lines (may be long; your dfs uses an accumulator, keep that style)
    char buf[MAXLINE];
    std::string line_acc;
    line_acc.reserve(4096);

    while (fgets(buf, MAXLINE, fd_tp)) {
        line_acc += buf;
        if (strchr(buf, '\n') == NULL && !feof(fd_tp)) continue; // not a full line yet

        // normalize line
        std::vector<char> tmp(line_acc.begin(), line_acc.end());
        tmp.push_back('\0');
        trim_inplace(tmp.data());
        if (tmp[0] == '\0') {
            line_acc.clear();
            continue;
        }

        // parse PI values for this pattern
        std::vector<int> pi_vals;
        pi_vals.reserve(pi_ids.size());
        {
            char *saveptr = NULL;
            char *tok = strtok_r(tmp.data(), ",", &saveptr);
            while (tok) {
                trim_inplace(tok);
                int v = parse_3val(tok);
                // PFS here is binary-parallel; if 'x', treat as 0 (common/simple convention)
                if (v == 2) v = 0;
                pi_vals.push_back(v);
                tok = strtok_r(NULL, ",", &saveptr);
            }
        }
        while (pi_vals.size() < pi_ids.size()) pi_vals.push_back(0);

        // run PFS passes for this ONE test pattern
        for (int base = 0; base < F; base += KMAX) {
            const int K = std::min(KMAX, F - base);   // faults in this pass
            const int bits = K + 1;                   // include good bit
            const Word MASK = make_mask(bits);

            // reset pass data
            std::fill(val.begin(), val.end(), (Word)0);
            std::fill(force0.begin(), force0.end(), (Word)0);
            std::fill(force1.begin(), force1.end(), (Word)0);

            // build force masks for faults in this block
            for (int j = 0; j < K; j++) {
                int node_num = faults[base + j].first;
                int sa_val   = faults[base + j].second;
                int idx      = idx_of_num[node_num];
                Word bitmask = ((Word)1 << (j + 1)); // bit0 is good, bit(j+1) is this fault

                if (sa_val == 0) force0[idx] |= bitmask;
                else             force1[idx] |= bitmask;
            }

            // initialize PI words: replicate good value into all bits (then faults override)
            for (size_t p = 0; p < pi_ids.size(); p++) {
                int id = pi_ids[p];
                auto it = idx_of_num.find(id);
                if (it == idx_of_num.end()) continue;
                int idx = it->second;
                int gv  = pi_vals[p]; // 0/1

                Word w = gv ? MASK : (Word)0;  // replicate across all bits
                // apply fault forcing if PI itself is faulty in this block
                w = (w & ~force0[idx]) | force1[idx];
                val[idx] = w & MASK;
            }

            // simulate level-by-level
            for (int l = 0; l <= max_level; l++) {
                for (int i = 0; i < Nnodes; i++) {
                    NSTRUC *np = &Node[i];
                    if ((int)np->level != l) continue;

                    if (np->ntype == PI) continue;

                    Word outv = 0;

                    if (np->ntype == FB) {
                        // buffer/branch-like
                        if (np->fin > 0) outv = val[idx_of_num[(int)np->unodes[0]->num]];
                        else outv = 0;
                    } else {
                        // logic gate types (binary)
                        auto inw = [&](int k)->Word {
                            NSTRUC *u = np->unodes[k];
                            return val[idx_of_num[(int)u->num]];
                        };

                        switch (np->type) {
                            case BRCH:
                                outv = (np->fin > 0) ? inw(0) : 0;
                                break;
                            case NOT:
                                outv = (np->fin > 0) ? (~inw(0) & MASK) : 0;
                                break;
                            case AND: {
                                outv = MASK;
                                for (int k = 0; k < (int)np->fin; k++) outv &= inw(k);
                                outv &= MASK;
                                break;
                            }
                            case NAND: {
                                Word t = MASK;
                                for (int k = 0; k < (int)np->fin; k++) t &= inw(k);
                                outv = (~t) & MASK;
                                break;
                            }
                            case OR: {
                                outv = 0;
                                for (int k = 0; k < (int)np->fin; k++) outv |= inw(k);
                                outv &= MASK;
                                break;
                            }
                            case NOR: {
                                Word t = 0;
                                for (int k = 0; k < (int)np->fin; k++) t |= inw(k);
                                outv = (~t) & MASK;
                                break;
                            }
                            case XOR: {
                                Word t = 0;
                                if (np->fin > 0) {
                                    t = inw(0);
                                    for (int k = 1; k < (int)np->fin; k++) t ^= inw(k);
                                }
                                outv = t & MASK;
                                break;
                            }
                            default:
                                outv = 0;
                                break;
                        }
                    }

                    // apply stuck-at forcing for this node in this pass
                    outv = (outv & ~force0[i]) | force1[i];
                    val[i] = outv & MASK;
                }
            }

            // detect faults: if ANY PO differs between good(bit0) and fault bit
            Word detected_bits = 0; // bits 0..K (we’ll ignore bit0 later)
            for (int k = 0; k < Npo; k++) {
                int po_idx = idx_of_num[(int)Poutput[k]->num];
                Word y = val[po_idx] & MASK;

                Word good = (y & 1) ? MASK : (Word)0;     // replicate good across bits
                Word diff = (y ^ good) & MASK;            // bits that differ from good
                detected_bits |= (diff >> 1);             // align fault bits to [0..K-1]
            }

            // record detected faults from this pass
            for (int j = 0; j < K; j++) {
                if (detected_bits & ((Word)1 << j)) {
                    detected.insert(enc_fault(faults[base + j].first, faults[base + j].second));
                }
            }
        }

        line_acc.clear();
    }

    // -------- write output (unique, sorted by node then sa) ----------
    std::vector<std::pair<int,int>> out;
    out.reserve(detected.size());
    for (auto code : detected) {
        int node = (int)(code >> 1);
        int sa   = (int)(code & 1ULL);
        out.push_back({node, sa});
    }
    std::sort(out.begin(), out.end());

    for (auto &p : out) {
        fprintf(fd_out, "%d@%d\n", p.first, p.second);
    }

    fclose(fd_tp);
    fclose(fd_fl);
    fclose(fd_out);

    printf("==> OK");
}
