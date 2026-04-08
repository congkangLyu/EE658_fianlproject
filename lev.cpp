#include "lev.h"
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


static std::string filterCkt(const std::string& s) {
    size_t slashPos = s.find_last_of('/');
    size_t cktPos   = s.find(".ckt");
    if (slashPos == std::string::npos || cktPos == std::string::npos || cktPos <= slashPos)
        return "";
    return s.substr(slashPos + 1, cktPos - slashPos - 1);
}

void lev() {
    // TODO: implement here
    int i, j;
    cp[strlen(cp)-1] = '\0';
    FILE* fd = fopen(cp, "w"); 

    if (fd == NULL) {
        std::cout << "Failed to open or create file\n";
        return;
    }

    NSTRUC *np;

    fprintf(fd, "%s", filterCkt(inp_name.c_str()).c_str());
    fprintf(fd, "\n#PI: ");
    fprintf(fd, "%d", Npi);
    fprintf(fd, "\n#PO: ");
    fprintf(fd, "%d", Npo);
    fprintf(fd, "\n#Nodes: ");
    fprintf(fd, "%d", Nnodes);
    fprintf(fd, "\n#Gates: ");
    fprintf(fd, "%d", Ngt);
    fprintf(fd, "\n");

    for(i = 0; i<Nnodes; i++) {
        np = &Node[i];
        fprintf(fd, "%d %d", np->num, np->level);
        fprintf(fd, "\n");
    }

    fclose(fd);
}
