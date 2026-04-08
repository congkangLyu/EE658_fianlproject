#include "logicsim.h"
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


void logicsim() {
    char file1[MAXLINE];
    char file2[MAXLINE];
    FILE *fd = NULL;
    FILE *fd_out = NULL;
    char buf[MAXLINE];

    rstrip(cp);

    if (sscanf(cp, "%1023s %1023s", file1, file2) != 2) {
        printf("Invalid Input!\n");
        return;
    }

    if ((fd = fopen(file1, "r")) == NULL) {
        printf("Input file does not exist!\n");
        return;
    }

    if ((fd_out = fopen(file2, "w")) == NULL) {
        printf("Cannot open output file!\n");
        fclose(fd);
        return;
    }

    // 0) compute max_level once
    int max_level = 0;
    for (int i = 0; i < Nnodes; i++) {
        if (Node[i].level > max_level) max_level = Node[i].level;
    }

    // 1) read header (PI ids line)
    std::string line;
    if (!read_full_line(fd, line)) {
        printf("Empty input file!\n");
        fclose(fd);
        fclose(fd_out);
        return;
    }

    // parse PI IDs from line (comma separated)
    std::vector<int> pi_ids;
    {
        std::stringstream ss(line);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            // trim tok（简单版）
            tok.erase(0, tok.find_first_not_of(" \t"));
            tok.erase(tok.find_last_not_of(" \t") + 1);
            if (!tok.empty()) pi_ids.push_back(std::atoi(tok.c_str()));
        }
    }

    // 2) write output header (PO ids)
    for (int k = 0; k < Npo; k++) {
        if (k) fputc(',', fd_out);
        fprintf(fd_out, "%d", (int)Poutput[k]->num);
    }
    fputc('\n', fd_out);

    // 3) for each pattern line
// 3) for each pattern line (read FULL line even if longer than MAXLINE)
std::string line_acc;
line_acc.reserve(4096);

while (fgets(buf, MAXLINE, fd)) {
    // accumulate fragments until we see a newline -> one full pattern line
    line_acc += buf;

    // if this chunk didn't include '\n', line is not complete yet
    if (strchr(buf, '\n') == NULL && !feof(fd)) {
        continue;
    }

    // Now we have a full line in line_acc (maybe last line w/o '\n' also ok)
    // Copy into a mutable C buffer for strtok_r
    // Strip trailing \r\n and spaces using your trim_inplace
    std::vector<char> tmp(line_acc.begin(), line_acc.end());
    tmp.push_back('\0');

    trim_inplace(tmp.data());
    if (tmp[0] == '\0') {
        line_acc.clear();
        continue;
    }

    // reset all nodes to X for this pattern
    for (int i = 0; i < Nnodes; i++) Node[i].gate_output = 2;

    // parse values from pattern line
    std::vector<int> vals;
    vals.reserve(pi_ids.size());
    {
        char *saveptr = NULL;
        char *tok = strtok_r(tmp.data(), ",", &saveptr);
        while (tok) {
            trim_inplace(tok);
            vals.push_back(parse_3val(tok));
            tok = strtok_r(NULL, ",", &saveptr);
        }
    }

    // pad with X if missing
    while (vals.size() < pi_ids.size()) vals.push_back(2);

    // assign PI values according to header order (match by node num)
    for (size_t i = 0; i < pi_ids.size(); i++) {
        int id = pi_ids[i];
        int v  = vals[i];

        for (int n = 0; n < Nnodes; n++) {
            if ((int)Node[n].num == id) {
                Node[n].gate_output = v;
                break;
            }
        }
    }

    // simulate level by level
    for (int l = 0; l <= max_level; l++) {
        for (int i = 0; i < Nnodes; i++) {
            NSTRUC *np = &Node[i];
            if (np->level != l) continue;

            if (np->ntype == PI) continue;

            if (np->ntype == FB) { // branch/buffer node
                np->gate_output = (np->fin > 0) ? np->unodes[0]->gate_output : 2;
                continue;
            }

            np->gate_output = eval_gate_3val(np);
        }
    }

    // output PO values for this pattern
    for (int k = 0; k < Npo; k++) {
        if (k) fputc(',', fd_out);
        fputc(print_3val(Poutput[k]->gate_output), fd_out);
    }
    fputc('\n', fd_out);

    // clear accumulator for next line
    line_acc.clear();
    }

    // in case the file doesn't end with '\n' and line_acc still has data:
    // (optional safety, but usually not needed)


    fclose(fd);
    fclose(fd_out);

    printf("==> OK");
}
