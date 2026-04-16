#ifndef STATIC_HELPERS_H
#define STATIC_HELPERS_H

#include "globals.h"
#include <cstring>
#include <cstdlib>

/*=======================================================================
  Static helper functions used by multiple command modules.
  These are file-scope helpers defined inline or as static functions
  to maintain the original behavior while enabling modularization.
=======================================================================*/

// Lambda helpers converted to static functions
static bool read_full_line(FILE *f, std::string &out) {
out.clear();
char tmp[MAXLINE];
while (fgets(tmp, MAXLINE, f)) {
    out += tmp;
    if (!out.empty() && out.back() == '\n') break;
}
if (out.empty()) return false;
while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
return true;
}

static void trim_inplace(char *s) {
    if (!s) return;
    int n = (int)strlen(s);
    while (n > 0 && (s[n-1] == '\r' || s[n-1] == '\n' || s[n-1] == ' ' || s[n-1] == '\t'))
        s[--n] = '\0';
    int p = 0;
    while (s[p] == ' ' || s[p] == '\t') p++;
    if (p > 0) memmove(s, s + p, strlen(s + p) + 1);
}

static int parse_3val(const char *tok) {
    if (!tok || !tok[0]) return 2;
    char c = tok[0];
    if (c == '0') return 0;
    if (c == '1') return 1;
    if (c == 'x' || c == 'X') return 2;
    return 2;
}

static char print_3val(int v) {
    if (v == 0) return '0';
    if (v == 1) return '1';
    return 'x';
}

static int eval_gate_3val(NSTRUC *np) {
    const int VX = 2;
    int fin = (int)np->fin;
    auto in = [&](int i)->int{
        int v = np->unodes[i]->gate_output;
        if (v != 0 && v != 1 && v != 2) v = VX;
        return v;
    };
    switch (np->type) {
        case BRCH:
        case BUF:
            return (fin > 0) ? in(0) : VX;
        case NOT:
            if (fin <= 0) return VX;
            if (in(0) == 0) return 1;
            if (in(0) == 1) return 0;
            return VX;
        case AND: {
            int anyX = 0;
            for (int i = 0; i < fin; i++) {
                int v = in(i);
                if (v == 0) return 0;
                if (v == VX) anyX = 1;
            }
            return anyX ? VX : 1;
        }
        case NAND: {
            int a;
            int anyX = 0;
            for (int i = 0; i < fin; i++) {
                int v = in(i);
                if (v == 0) { a = 0; goto nand_done; }
                if (v == VX) anyX = 1;
            }
            a = anyX ? VX : 1;
            nand_done:
            if (a == 0) return 1;
            if (a == 1) return 0;
            return VX;
        }
        case OR: {
            int anyX = 0;
            for (int i = 0; i < fin; i++) {
                int v = in(i);
                if (v == 1) return 1;
                if (v == VX) anyX = 1;
            }
            return anyX ? VX : 0;
        }
        case NOR: {
            int o;
            int anyX = 0;
            for (int i = 0; i < fin; i++) {
                int v = in(i);
                if (v == 1) { o = 1; goto nor_done; }
                if (v == VX) anyX = 1;
            }
            o = anyX ? VX : 0;
            nor_done:
            if (o == 0) return 1;
            if (o == 1) return 0;
            return VX;
        }
        case XOR: {
            for (int i = 0; i < fin; i++) if (in(i) == VX) return VX;
            int ones = 0;
            for (int i = 0; i < fin; i++) ones += (in(i) == 1);
            return (ones & 1) ? 1 : 0;
        }
        default:
            return VX;
    }
}

#endif /* STATIC_HELPERS_H */
