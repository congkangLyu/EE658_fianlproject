#include "rfl.h"
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


void rfl(){
    FILE *fd_out = NULL;
    char file2[MAXLINE];

    if (sscanf(cp, "%1023s", file2) != 1) {
        printf("Invalid Input!\n");
        return;
    }

    if ((fd_out = fopen(file2, "w")) == NULL) {
        printf("Cannot open output file!\n");
        return;
    }

    for (int i = 0; i < Nnodes; i++) {
        if (Node[i].ntype == PI || Node[i].ntype == FB) {
            fprintf(fd_out, "%d@0\n", Node[i].num);
            fprintf(fd_out, "%d@1\n", Node[i].num);
        };
    }
    fclose(fd_out);

    printf("==> OK");
}
