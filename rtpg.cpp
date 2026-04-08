#include "rtpg.h"
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

void rtpg() {
    // Seed is set globally from main() so results are reproducible.
    int i, j, r;
    int tp_cnt;
    char mode, val;
    char file[MAXLINE];

    cp[strlen(cp)-1] = '\0';
    sscanf(cp, "%d %c %255s", &tp_cnt, &mode, file);
    FILE* fd = fopen(file, "w");
    if (fd == NULL) {
        std::cout << "Failed to open or create file\n";
        return;
    }
    printf("%d %c", tp_cnt, mode);
    for(i = 0; i<Npi; i++) {
        if (i > 0) fprintf(fd, ",");
        fprintf(fd, "%d",Pinput[i]->num);
    }
    fprintf(fd, "\n");

    for(i = 0; i<tp_cnt; i++) {
        for(j = 0; j<Npi; j++) {
            if (mode == 't') {
                r = std::rand() % 3;
                val = (r == 0) ? '0' : (r == 1) ? '1' : 'x';
            }
            else if (mode == 'b') {
                r = std::rand() % 2;
                val = (r == 0) ? '0' : '1';
            }
            else {
                std::cout << "Invalid mode!\n";
                return;
            }
            if (j > 0) fprintf(fd, ",");
            fprintf(fd, "%c",val);
        }
        fprintf(fd, "\n");
    }

    fclose(fd);

    printf("==> OK");
}
