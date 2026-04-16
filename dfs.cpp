#include "dfs.h"
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


void dfs(){
    char file1[MAXLINE];
    char file2[MAXLINE];
    FILE *fd = NULL;
    FILE *fd_out = NULL;
    char buf[MAXLINE];
    std::vector<std::vector<int>> total_coverage;

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

    std::string line_acc;
    line_acc.reserve(4096);
    int sa_val = -1;

    while (fgets(buf, MAXLINE, fd)) {
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
        for (int i = 0; i < Nnodes; i++) {
            Node[i].gate_output = 2;
            Node[i].fault_list.clear();
        }

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

        // assign PI values according to header order (match by node num) and set stuck ats
        for (size_t i = 0; i < pi_ids.size(); i++) {
            int id = pi_ids[i];
            int v  = vals[i];

            for (int n = 0; n < Nnodes; n++) {
                if ((int)Node[n].num == id) {
                    Node[n].gate_output = v;
                    if (v == 0) {
                        sa_val = 1;
                    } else if (v == 1) {
                        sa_val = 0;
                    } else {
                        sa_val = -1;
                        printf("Invalid Stuck-At Value being added!\n");
                    }
                    Node[n].fault_list.push_back({int(Node[n].num), sa_val});
                    // printf("%d@%d \n", Node[n].num, sa_val);
                    break;
                }
            }
        }

        // simulate level by level, set SAs
        for (int l = 0; l <= max_level; l++) {
            for (int i = 0; i < Nnodes; i++) {
                NSTRUC *np = &Node[i];
                if (np->level != l) continue;

                if (np->ntype == PI) continue;

                if (np->ntype == FB) { // buffer node
                    np->gate_output = (np->fin > 0) ? np->unodes[0]->gate_output : 2;

                    np->fault_list = np->unodes[0]->fault_list;
                    if (np->gate_output == 0) {
                        sa_val = 1;
                    } else if (np->gate_output == 1) {
                        sa_val = 0;
                    } else {
                        sa_val = -1;
                        printf("Invalid Stuck-At Value being added!\n");
                    }
                    np->fault_list.push_back({int(np->num), sa_val});
                    continue;
                }

                np->gate_output = eval_gate_3val(np);
                if (np->gate_output == 0) {
                    sa_val = 1;
                } else if (np->gate_output == 1) {
                    sa_val = 0;
                } else {
                    sa_val = -1;
                    printf("Invalid Stuck-At Value being added!\n");
                }
                switch (np->type) {
                    case BRCH: {
                        np->fault_list = np->unodes[0]->fault_list;
                        break;
                    }
                    case NOT: {
                        np->fault_list = np->unodes[0]->fault_list;
                        break;
                    }
                    case AND: {
                        np->fault_list = controlling_helper(0, np);
                        break;
                    }
                    case NAND: {
                        np->fault_list = controlling_helper(0, np);
                        break;
                    }
                    case OR: {
                        np->fault_list = controlling_helper(1, np);
                        break;
                    }
                    case NOR: {
                        np->fault_list = controlling_helper(1, np);
                        break;
                    }
                    case XOR: {
                        // F0(Y) = (F0(A) ∩ F0(B)) U (F1(A) ∩ F1(B))
                        // F1(Y) = (F0(A) ∩ F1(B)) U (F1(A) ∩ F0(B))
                        break;
                    }
                }
                np->fault_list.push_back({int(np->num), sa_val});
            }
        }
        // gather new coverage and union into previous coverage
        for (int k = 0; k < Npo; k++) {
            std::vector<std::vector<int>> swapped;
            std::sort(total_coverage.begin(), total_coverage.end());
            std::sort(Poutput[k]->fault_list.begin(), Poutput[k]->fault_list.end());
            std::set_union(total_coverage.begin(), 
                           total_coverage.end(),
                           Poutput[k]->fault_list.begin(), Poutput[k]->fault_list.end(),
                           std::back_inserter(swapped));
            total_coverage.swap(swapped);
        }
        line_acc.clear();
    }

    // at the end, write the PO vector to the output file
    for (int k = 0; k < total_coverage.size(); k++) {
        fprintf(fd_out, "%d@%d\n", total_coverage[k][0], total_coverage[k][1]);
    }
    fclose(fd);
    fclose(fd_out);

    printf("==> OK");
}
