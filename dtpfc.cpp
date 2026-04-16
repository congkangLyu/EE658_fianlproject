#include "dtpfc.h"
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

static std::vector<std::vector<int>> controlling_helper (int controlling_value, NSTRUC *np){
    std::vector<int> controlling;
    std::vector<int> non_controlling;
    std::vector<std::vector<int>> temp;
    std::vector<std::vector<int>> temp_control;
    for (unsigned i = 0; i < np->fin; i++) {
        if (np->unodes[i]->gate_output == controlling_value) {
            controlling.push_back(i);
        }
        else {
            non_controlling.push_back(i);
        }
    }
    // union all non controlling
    for (int i = 0; i < non_controlling.size(); i++) {
            std::vector<std::vector<int>> swapped;
            std::sort(np->unodes[non_controlling[i]]->fault_list.begin(), np->unodes[non_controlling[i]]->fault_list.end());
            std::sort(temp.begin(), temp.end());
            std::set_union(np->unodes[non_controlling[i]]->fault_list.begin(), 
                            np->unodes[non_controlling[i]]->fault_list.end(),
                            temp.begin(), temp.end(),
                            std::back_inserter(swapped));
            temp.swap(swapped);
    }
    if (!controlling.empty()) { // if controlling exists, intersect them and subtract controlling
        temp_control = np->unodes[controlling[0]]->fault_list;
        for (int i = 1; i < controlling.size(); i++) {
            std::vector<std::vector<int>> swapped;
            std::sort(np->unodes[controlling[i]]->fault_list.begin(), np->unodes[controlling[i]]->fault_list.end());
            std::sort(temp_control.begin(), temp_control.end());
            std::set_intersection(np->unodes[controlling[i]]->fault_list.begin(), 
                                    np->unodes[controlling[i]]->fault_list.end(),
                                    temp_control.begin(), temp_control.end(),
                                    std::back_inserter(swapped));
            temp_control.swap(swapped);
        }
        std::vector<std::vector<int>> swapped;
        std::sort(temp_control.begin(), temp_control.end());
        std::sort(temp.begin(), temp.end());
        std::set_difference(temp_control.begin(), temp_control.end(),
            temp.begin(), temp.end(),
            std::back_inserter(swapped));
        temp.swap(swapped);
    }
    return temp;
}

static int cost_other_non_controlling (NSTRUC *np, int index) {
    int cost = 0;
    switch (np->type) {
        case NOT:
        case BRCH:
        case BUF:
            break;
        case XOR:
            // NOT IN NETLISTS
            break;
        case OR:
        case NOR:
            for (int i = 0; i < np->fin; i++) {
                if (i == index) continue;
                cost += np->unodes[i]->scoap.CC0;
            }
            break;
        case NAND:
        case AND:
            for (int i = 0; i < np->fin; i++) {
                if (i == index) continue;
                cost += np->unodes[i]->scoap.CC1;
            }
            break;
        default:
            printf("Unknown node type!\n");
            exit(-1);
    }   
    return cost;
}

void dtpfc() {
    // DTPFC <tp-fname> <freq> <tpfc-report-fname>
    rstrip(cp);

    int freq = 0;
    char tpfile[MAXLINE];
    char repfile[MAXLINE];
    char buf[MAXLINE];

    if (sscanf(cp, "%1023s %d %1023s", tpfile, &freq, repfile) != 3) {
        printf("Invalid Input!\n");
        return;
    }
    if (freq <= 0) {
        printf("Invalid Input!\n");
        return;
    }

    FILE *fd_tp = fopen(tpfile, "r");
    if (!fd_tp) {
        printf("Cannot open input tp file!\n");
        return;
    }
    FILE *fd_rep = fopen(repfile, "w");
    if (!fd_rep) {
        printf("Cannot open report file!\n");
        fclose(fd_tp);
        return;
    }

    int total_faults = 2 * Nnodes;
    if (total_faults == 0) {
        fclose(fd_tp);
        fclose(fd_rep);
        printf("==> OK");
        return;
    }

    auto enc_fault = [](int node_num, int sa_val) -> unsigned long long {
        // sa_val expected 0 or 1
        // put node_num in high bits, sa in lowest bit
        return ((unsigned long long)(unsigned int)node_num << 1) | (unsigned long long)(sa_val & 1);
    };

    auto add_fault_vec = [&](std::unordered_set<unsigned long long> &S,
                             const std::vector<std::vector<int>> &fl) {
        for (const auto &p : fl) {
            if (p.size() < 2) continue;
            int n = p[0];
            int sa = p[1];
            if (sa != 0 && sa != 1) continue;
            S.insert(enc_fault(n, sa));
        }
    };

    int max_level = 0;
    for (int i = 0; i < Nnodes; i++) {
        if (Node[i].level > max_level) max_level = Node[i].level;
    }

    // Seed is set globally from main() so results are reproducible.

    std::unordered_set<unsigned long long> detected;
    detected.reserve((size_t)total_faults * 2);

    // 1) read header (PI ids line)
    std::string line;
    if (!read_full_line(fd_tp, line)) {
        printf("Empty input file!\n");
        fclose(fd_tp);
        fclose(fd_rep);
        return;
    }

    // parse PI IDs from line (comma separated)
    std::vector<int> pi_ids;
    {
        std::stringstream ss(line);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            tok.erase(0, tok.find_first_not_of(" \t"));
            tok.erase(tok.find_last_not_of(" \t") + 1);
            if (!tok.empty()) pi_ids.push_back(std::atoi(tok.c_str()));
        }
    }

    int generated = 0;
    int last_interval_detected = 0;

    std::string line_acc;
    line_acc.reserve(4096);

    while (fgets(buf, MAXLINE, fd_tp)) {
        // accumulate fragments until we see a newline -> one full pattern line
        line_acc += buf;

        // if this chunk didn't include '\n', line is not complete yet
        if (strchr(buf, '\n') == NULL && !feof(fd_tp)) {
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

        std::vector<int> pi_vals;
        pi_vals.reserve(Npi);
        
        char *saveptr = NULL;
        char *tok = strtok_r(tmp.data(), ",", &saveptr);
        while (tok) {
            trim_inplace(tok);
            pi_vals.push_back(parse_3val(tok));
            tok = strtok_r(NULL, ",", &saveptr);
        }

        generated++;

        for (int i = 0; i < Nnodes; i++) {
            Node[i].gate_output = 2; // X
            Node[i].fault_list.clear();
        }

        for (int i = 0; i < Npi; i++) {
            NSTRUC *pi = Pinput[i];
            int v = pi_vals[i];     // 0 or 1
            pi->gate_output = v;
            int sa = (v == 0) ? 1 : 0;
            pi->fault_list.push_back({(int)pi->num, sa});
        }

        for (int l = 0; l <= max_level; l++) {
            for (int i = 0; i < Nnodes; i++) {
                NSTRUC *np = &Node[i];
                if (np->level != l) continue;

                if (np->ntype == PI) continue;

                int sa_val = 0;

                if (np->ntype == FB) {
                    np->gate_output = (np->fin > 0) ? np->unodes[0]->gate_output : 2;

                    np->fault_list = np->unodes[0]->fault_list;
                    if (np->gate_output == 0) sa_val = 1;
                    else if (np->gate_output == 1) sa_val = 0;
                    else sa_val = 0; // if X, just pick 0 (won't be meaningful for FC anyway)

                    np->fault_list.push_back({(int)np->num, sa_val});
                    continue;
                }

                np->gate_output = eval_gate_3val(np);

                switch (np->type) {
                    case BRCH:
                    case NOT:
                    case BUF:
                        np->fault_list = np->unodes[0]->fault_list;
                        break;

                    case AND:
                    case NAND:
                        np->fault_list = controlling_helper(0, np);
                        break;

                    case OR:
                    case NOR:
                        np->fault_list = controlling_helper(1, np);
                        break;

                    case XOR:
                        // no XOR
                        np->fault_list.clear();
                        break;

                    default:
                        np->fault_list.clear();
                        break;
                }

                if (np->gate_output == 0) sa_val = 1;
                else if (np->gate_output == 1) sa_val = 0;
                else sa_val = 0;

                np->fault_list.push_back({(int)np->num, sa_val});
            }
        }

        for (int k = 0; k < Npo; k++) {
            add_fault_vec(detected, Poutput[k]->fault_list);
        }

        if (generated % freq == 0) {
            double cov = 100.0 * (double)detected.size() / (double)total_faults;
            fprintf(fd_rep, "%.2f\n", cov);

            // saturation detect: no new faults in this interval
            int now = (int)detected.size();
            // if (now == last_interval_detected) {
            //     break;
            // }
            last_interval_detected = now;
        }
        line_acc.clear();
    }

    if (generated % freq != 0) {
        double cov = 100.0 * (double)detected.size() / (double)total_faults;
        fprintf(fd_rep, "%.2f\n", cov);
    }

    fclose(fd_tp);
    fclose(fd_rep);

    printf("==> OK");
}
