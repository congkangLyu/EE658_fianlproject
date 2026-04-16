#include "utils.h"
#include <string>
#include <cstring>

/*-----------------------------------------------------------------------
input: gate type
output: string of the gate type
called by: pc
description:
    The routine receive an integer gate type and return the gate type in
    character string.
-----------------------------------------------------------------------*/
std::string gname(int tp){
    switch(tp) {
        case 0: return("PI");
        case 1: return("BRANCH");
        case 2: return("XOR");
        case 3: return("OR");
        case 4: return("NOR");
        case 5: return("NOT");
        case 6: return("NAND");
        case 7: return("AND");
    }
    return "";
}

// String trimming utility
void rstrip(char *s) {
    if (!s) return;
    size_t n = std::strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' ' || s[n-1] == '\t'))
        s[--n] = '\0';
}

// Note: Custom strlen was originally in the backup to override system strlen.
// Since this causes conflicts with system headers, we preserve the original
// intent by using std::strlen. The original override only affected readckt.cpp's TU.
// To maintain exact output compatibility, we note this change here.

int eval_gate_from_inputs(NSTRUC *np, const std::vector<int> &val) {
    auto in = [&](int k) -> int {
        int idx = np->unodes[k]->indx;
        return val[idx];
    };

    switch (np->type) {
        case BRCH:
        case BUF:
            return in(0);

        case NOT:
            if (in(0) == LX) return LX;
            return 1 - in(0);

        case AND: {
            bool anyX = false;
            for (unsigned i = 0; i < np->fin; i++) {
                int v = in(i);
                if (v == 0) return 0;
                if (v == LX) anyX = true;
            }
            return anyX ? LX : 1;
        }

        case NAND: {
            bool anyX = false;
            for (unsigned i = 0; i < np->fin; i++) {
                int v = in(i);
                if (v == 0) return 1;
                if (v == LX) anyX = true;
            }
            return anyX ? LX : 0;
        }

        case OR: {
            bool anyX = false;
            for (unsigned i = 0; i < np->fin; i++) {
                int v = in(i);
                if (v == 1) return 1;
                if (v == LX) anyX = true;
            }
            return anyX ? LX : 0;
        }

        case NOR: {
            bool anyX = false;
            for (unsigned i = 0; i < np->fin; i++) {
                int v = in(i);
                if (v == 1) return 0;
                if (v == LX) anyX = true;
            }
            return anyX ? LX : 1;
        }

        case XOR: {
            for (unsigned i = 0; i < np->fin; i++) {
                if (in(i) == LX) return LX;
            }
            int ones = 0;
            for (unsigned i = 0; i < np->fin; i++) {
                ones += in(i);
            }
            return (ones & 1);
        }

        default:
            return LX;
    }
}
