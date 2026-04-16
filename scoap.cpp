#include "scoap.h"
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

void scoap() {
    int i, j, done, gate_tp;
    NSTRUC *np;
    FILE *fd_out = NULL;
    int big_number = std::numeric_limits<int>::max()/4;

    cp[strlen(cp)-1] = '\0';

    if ((fd_out = fopen(cp, "w")) == NULL) {
        printf("Cannot open output file!\n");
        return;
    }

    // initialize scoap vals
    for(i = 0; i<Nnodes; i++) {
        Node[i].scoap.CC0 = -1;
        Node[i].scoap.CC1 = -1;
        Node[i].scoap.CO  = big_number;
    }
    for(i = 0; i<Npi; i++) {
        Pinput[i]->scoap.CC0 = 1;
        Pinput[i]->scoap.CC1 = 1;
    }
    for(i = 0; i<Npo; i++) {
        Poutput[i]->scoap.CO = 0;
    }

    // go forward thru the circuit to calculate CC0/CC1
    done = 0;
    while (!done) {
        done = 1;    
        for(i = 0; i < Nnodes; i++) {
            std::vector<int> v0, v1;
            np = &Node[i];

            if (np->ntype == PI) continue;

            gate_tp = np->type;
            bool ready = true;

            switch (gate_tp) {
                case BRCH:
                case BUF:
                    if (!np->unodes[0]) {
                        printf("Null fanin at node %d\n", np->num);
                        exit(1);
                    }
                    if (np->unodes[0]->scoap.CC0 == -1 || np->unodes[0]->scoap.CC1 == -1) {
                            done = 0;
                            break;
                    }
                    np->scoap.CC0 = np->unodes[0]->scoap.CC0;
                    np->scoap.CC1 = np->unodes[0]->scoap.CC1;
                    break;
                case XOR:
                    // NOT IN NETLISTS
                    break;
                case OR:
                    for (j = 0; j < np->fin; j++) {
                        if (np->unodes[j]->scoap.CC0 == -1 || np->unodes[j]->scoap.CC1 == -1) {
                            done = 0;
                            ready = false;
                            break;
                        }
                        v0.push_back(np->unodes[j]->scoap.CC0);
                        v1.push_back(np->unodes[j]->scoap.CC1);
                    }
                    if ( ready ) {
                        np->scoap.CC0 = 1 +  std::accumulate(v0.begin(), v0.end(), 0);
                        np->scoap.CC1 = 1 + *std::min_element(v1.begin(), v1.end());
                    }
                    break; 
                case NOR:
                    for (j = 0; j < np->fin; j++) {
                        if (np->unodes[j]->scoap.CC0 == -1 || np->unodes[j]->scoap.CC1 == -1) {
                            done = 0;
                            ready = false;
                            break;
                        }
                        v0.push_back(np->unodes[j]->scoap.CC0);
                        v1.push_back(np->unodes[j]->scoap.CC1);
                    }
                    if ( ready ) {
                        np->scoap.CC1 = 1 +  std::accumulate(v0.begin(), v0.end(), 0);
                        np->scoap.CC0 = 1 + *std::min_element(v1.begin(), v1.end());
                    }
                    break;
                case NOT:
                    np->scoap.CC0 = np->unodes[0]->scoap.CC1 + 1;
                    np->scoap.CC1 = np->unodes[0]->scoap.CC0 + 1;
                    break;
                case NAND:
                    for (j = 0; j < np->fin; j++) {
                        if (np->unodes[j]->scoap.CC0 == -1 || np->unodes[j]->scoap.CC1 == -1) {
                            done = 0;
                            ready = false;
                            break;
                        }
                        v0.push_back(np->unodes[j]->scoap.CC0);
                        v1.push_back(np->unodes[j]->scoap.CC1);
                    }
                    if ( ready ) {
                        np->scoap.CC1 = 1 + *std::min_element(v0.begin(), v0.end());
                        np->scoap.CC0 = 1 + std::accumulate(v1.begin(), v1.end(), 0);
                    }
                    break;
                case AND:
                    for (j = 0; j < np->fin; j++) {
                        if (np->unodes[j]->scoap.CC0 == -1 || np->unodes[j]->scoap.CC1 == -1) {
                            done = 0;
                            ready = false;
                            break;
                        }
                        v0.push_back(np->unodes[j]->scoap.CC0);
                        v1.push_back(np->unodes[j]->scoap.CC1);
                    }
                    if ( ready ) {
                        np->scoap.CC0 = 1 + *std::min_element(v0.begin(), v0.end());
                        np->scoap.CC1 = 1 + std::accumulate(v1.begin(), v1.end(), 0);
                    }
                    break;
                default:
                    printf("Unknown node type!\n");
                    exit(-1);
            }   
        }
    }

    // go backwards thru the circuit to calculate CO
    done = 0;
    while (!done) {
        done = 1;
        for (i = 0; i < Nnodes; ++i) {
            np = &Node[i];

            if (np->scoap.CO == big_number) {
                done = 0;   
                continue;
            }

            /* propagate observability from gate np to each of its fan-in nodes */
            for (j = 0; j < np->fin; ++j) {
                NSTRUC *in = np->unodes[j];   
                
                int other_cost = cost_other_non_controlling(np, j);  
                int contrib = np->scoap.CO + other_cost;        
                if (np->type != BRCH) contrib += 1;

                if (contrib < in->scoap.CO) {
                    in->scoap.CO = contrib;
                    done = 0;   
                }
            }
        }
    }

    for(i = 0; i < Nnodes; i++) {
        fprintf(fd_out, "%d,%d,%d,%d\n", Node[i].num, Node[i].scoap.CC0, Node[i].scoap.CC1, Node[i].scoap.CO);
    }

    fclose(fd_out);
    printf("==> OK");
    return;
} 
