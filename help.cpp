#include "help.h"
#include <cstdio>

/*-----------------------------------------------------------------------
input: nothing
output: nothing
called by: main
description:
    The routine prints out help information for each command.
-----------------------------------------------------------------------*/
void help(){
    printf("READ filename - ");
    printf("read in circuit file and creat all data structures\n");
    printf("PC - ");
    printf("print circuit information\n");
    printf("HELP - ");
    printf("print this help information\n");
    printf("QUIT - ");
    printf("stop and exit\n");
    printf("LEV - ");
    printf("levelize circuit\n");
    printf("LOGICSIM - ");
    printf("read in input data and write out logical output\n");
    printf("RTPG - ");
    printf("generate random test patterns and store them in a file\n");
    printf("RFL - ");
    printf("lists faults that are reduced by applying the check point theorem \n");
    printf("DFS - ");
    printf("test patterns as input; reports all detectable faults using these patterns \n");
    printf("PFS - ");
    printf("list of faults and test patterns as input; reports the faults can be detected with these test patterns \n");
    printf("TPFC - ");
    printf(" detect saturation and run deterministic approaches for the remaining faults \n");
    printf("SCOAP - ");
    printf("Lists controllability and observability values for all possible lines/nodes in a netlist \n");
    printf("dalG - ");
    printf("Generate a test pattern for fault using the D-Algorithm \n");
    printf("PODEM - ");
    printf("Generate a test pattern for fault using the PODEM Algorithm \n");
    printf("TPG - ");
    printf("Generate test patterns for a circuit given constraints (see README)\n");
    printf("DTPFC - ");
    printf("Same as TPFC but reads the test patterns from a file instead \n");
}
