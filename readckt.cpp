/*=======================================================================
  A simple parser for "self" format

  The circuit format (called "self" format) is based on outputs of
  a ISCAS 85 format translator written by Dr. Sandeep Gupta.
  The format uses only integers to represent circuit information.
  The format is as follows:

1        2        3        4           5           6 ...
------   -------  -------  ---------   --------    --------
0 GATE   outline  0 IPT    #_of_fout   #_of_fin    inlines
                  1 BRCH
                  2 XOR(currently not implemented)
                  3 OR
                  4 NOR
                  5 NOT
                  6 NAND
                  7 AND

1 PI     outline  0        #_of_fout   0

2 FB     outline  1 BRCH   inline

3 PO     outline  2 - 7    0           #_of_fin    inlines

    The code was initially implemented by Chihang Chen in 1994 in C, 
    and was later changed to C++ in 2022 for the course porject of 
    EE658: Diagnosis and Design of Reliable Digital Systems at USC. 

=======================================================================*/

/*=======================================================================
    Guide for students: 
        Write your program as a subroutine under main().
        The following is an example to add another command 'lev' under main()

enum e_com {READ, PC, HELP, QUIT, LEV};
#define NUMFUNCS 5
void cread(), pc(), quit(), lev();
struct cmdstruc command[NUMFUNCS] = {
    {"READ", cread, EXEC},
    {"PC", pc, CKTLD},
    {"HELP", help, EXEC},
    {"QUIT", quit, EXEC},
    {"LEV", lev, CKTLD},
};

lev()
{
   ...
}
=======================================================================*/

#include <stdio.h>
#include <iostream>
#include <string>
#include <string.h>
#include <ctype.h>
#include <cstring>
#include <stdlib.h>
#include <ctime>
#include <fstream>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <unordered_set> 
#include <functional>
#include <numeric>
#include <limits>
#define MAXLINE 1024               /* Input buffer size */
#define MAXNAME 31               /* File name size */

#define Upcase(x) ((isalpha(x) && islower(x))? toupper(x) : (x))
#define Lowcase(x) ((isalpha(x) && isupper(x))? tolower(x) : (x))

enum e_com {READ, PC, HELP, QUIT};
enum e_state {EXEC, CKTLD};         /* Gstate values */
enum e_ntype {GATE, PI, FB, PO};    /* column 1 of circuit format */
enum e_gtype {IPT, BRCH, XOR, OR, NOR, NOT, NAND, AND};  /* gate types */

struct cmdstruc {
   char name[MAXNAME];        /* command syntax */
   void (*fptr)();            /* function pointer of the commands */
   enum e_state state;        /* execution state sequence */
};

struct Scoap {
    int CC0;
    int CC1;
    int CO;
};

typedef struct n_struc {
    unsigned indx;             /* node index(from 0 to NumOfLine - 1 */
    unsigned num;              /* line number(May be different from indx */
    enum e_ntype ntype;
    enum e_gtype type;         /* gate type */
    unsigned fin;              /* number of fanins */
    unsigned fout;             /* number of fanouts */
    struct n_struc **unodes;   /* pointer to array of up nodes */
    struct n_struc **dnodes;   /* pointer to array of down nodes */
    int level;                 /* level of the gate output */
    int gate_output;          /* Output value of the gate */
    std::vector<std::vector<int>> fault_list;    /* List of detectable faults of the gate */
    Scoap scoap;
} NSTRUC;                     

static const int L0 = 0;
static const int L1 = 1;
static const int LX = 2;

static std::unordered_map<int, int> idx_of_num;
static std::vector<int> pi_assign;
static std::vector<int> good_val;
static std::vector<int> bad_val;

static void simulate_circuit(int fault_node_num, int sa_val);
static bool fault_at_po();
static bool fault_activated(int fault_idx, int sa_val);
static std::vector<NSTRUC*> get_d_frontier();
static int non_controlling_value(NSTRUC *g);
static bool podem_rec(int fault_idx, int sa_val);
static bool get_objective(int fault_idx, int sa_val, NSTRUC* &obj_node, int &obj_val);
static void backtrace(NSTRUC *obj_node, int obj_val, NSTRUC* &pi_node, int &pi_val);
static int pi_index_from_node(NSTRUC *pi);

/*----------------- Command definitions ----------------------------------*/
#define NUMFUNCS 15
void cread(), pc(), help(), quit(), lev(), logicsim(), rtpg(), rfl(), dfs(), pfs(), tpfc(), scoap(), dlag(), podem(), tpg();
struct cmdstruc command[NUMFUNCS] = {
   {"READ", cread, EXEC},
   {"PC", pc, CKTLD},
   {"HELP", help, EXEC},
   {"QUIT", quit, EXEC},
   {"LEV", lev, CKTLD},
   {"LOGICSIM", logicsim, CKTLD},
   {"RTPG", rtpg, CKTLD},
   {"RFL", rfl, CKTLD},
   {"DFS", dfs, CKTLD},
   {"PFS", pfs, CKTLD},
   {"TPFC", tpfc, CKTLD},
   {"SCOAP", scoap, CKTLD},
   {"DALG", dlag, CKTLD},
   {"PODEM", podem, CKTLD},
   {"TPG", tpg, CKTLD}
};

/*------------------------------------------------------------------------*/
enum e_state Gstate = EXEC;     /* global exectution sequence */
NSTRUC *Node;                   /* dynamic array of nodes */
NSTRUC **Pinput;                /* pointer to array of primary inputs */
NSTRUC **Poutput;               /* pointer to array of primary outputs */
int Nnodes;                     /* number of nodes */
int Npi;                        /* number of primary inputs */
int Npo;                        /* number of primary outputs */
int Ngt;                        /* number of gates */
int Done = 0;                   /* status bit to terminate program */
char *cp;              
char inFile[MAXLINE];

/*----------------------------------vi--------------------------------------*/

/*-----------------------------------------------------------------------
input: nothing
output: nothing
called by: shell
description:
    This is the main program of the simulator. It displays the prompt, reads
    and parses the user command, and calls the corresponding routines.
    Commands not reconized by the parser are passed along to the shell.
    The command is executed according to some pre-determined sequence.
    For example, we have to read in the circuit description file before any
    action commands.  The code uses "Gstate" to check the execution
    sequence.
    Pointers to functions are used to make function calls which makes the
    code short and clean.
-----------------------------------------------------------------------*/

std::size_t strlen(const char* start) {
   const char* end = start;
   for( ; *end != '\0'; ++end)
      ;
   return end - start;
}


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


/*-----------------------------------------------------------------------
input: nothing
output: nothing
called by: main 
description:
    The routine prints ot help inormation for each command.
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
    printf("DLAG - ");
    printf("Generate a test pattern for fault using the D-Algorithm \n");
    printf("PODEM - ");
    printf("Generate a test pattern for fault using the PODEM Algorithm \n");
}

// ---------- helpers ----------
static inline void rstrip(char *s) {
    if (!s) return;
    size_t n = ::strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' ' || s[n-1] == '\t'))
        s[--n] = '\0';
}

static int eval_gate_from_inputs(NSTRUC *np, const std::vector<int> &val) {
    auto in = [&](int k) -> int {
        int idx = np->unodes[k]->indx;
        return val[idx];
    };

    switch (np->type) {
        case BRCH:
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

static void simulate_circuit(int fault_node_num, int sa_val) {
    good_val.assign(Nnodes, LX);
    bad_val.assign(Nnodes, LX);

    for (int i = 0; i < Npi; i++) {
      int idx = Pinput[i]->indx;
      good_val[idx] = pi_assign[i];
      bad_val[idx] = pi_assign[i];

      // inject stuck-at fault even when the fault site is a PI
      if ((int)Pinput[i]->num == fault_node_num) {
        bad_val[idx] = sa_val;
      }
    }

    int max_level = 0;
    for (int i = 0; i < Nnodes; i++) {
        if (Node[i].level > max_level) max_level = Node[i].level;
    }

    for (int l = 0; l <= max_level; l++) {
        for (int i = 0; i < Nnodes; i++) {
            NSTRUC *np = &Node[i];
            if (np->level != l) continue;
            if (np->ntype == PI) continue;

            good_val[i] = eval_gate_from_inputs(np, good_val);
            bad_val[i] = eval_gate_from_inputs(np, bad_val);

            if ((int)np->num == fault_node_num) {
                bad_val[i] = sa_val;
            }
        }
    }
}

static bool fault_at_po() {
    for (int i = 0; i < Npo; i++) {
        int idx = Poutput[i]->indx;
        if (good_val[idx] != LX && bad_val[idx] != LX && good_val[idx] != bad_val[idx]) {
            return true;
        }
    }
    return false;
}

static bool fault_activated(int fault_idx, int sa_val) {
    return good_val[fault_idx] != LX &&
           good_val[fault_idx] == (1 - sa_val) &&
           bad_val[fault_idx] == sa_val;
}

static bool node_has_fault_effect(int idx) {
    return good_val[idx] != LX &&
           bad_val[idx] != LX &&
           good_val[idx] != bad_val[idx];
}

static bool gate_output_unresolved(int idx) {
    if (good_val[idx] == LX || bad_val[idx] == LX) return true;
    return good_val[idx] == bad_val[idx];
}

static std::vector<NSTRUC*> get_d_frontier() {
    std::vector<NSTRUC*> df;

    for (int i = 0; i < Nnodes; i++) {
        NSTRUC *np = &Node[i];
        if (np->ntype == PI) continue;

        bool hasD = false;
        bool hasX = false;

        for (unsigned k = 0; k < np->fin; k++) {
            int u = np->unodes[k]->indx;
            if (node_has_fault_effect(u)) hasD = true;
            if (good_val[u] == LX && bad_val[u] == LX) hasX = true;
        }

        if (hasD && hasX && gate_output_unresolved(i)) {
            df.push_back(np);
        }
    }

    return df;
}

static int non_controlling_value(NSTRUC *g) {
    switch (g->type) {
        case AND:
        case NAND:
            return 1;
        case OR:
        case NOR:
            return 0;
        default:
            return 1;
    }
}

static bool get_objective(int fault_idx, int sa_val, NSTRUC* &obj_node, int &obj_val) {
    if (!fault_activated(fault_idx, sa_val)) {
        obj_node = &Node[fault_idx];
        obj_val = 1 - sa_val;
        return true;
    }

    std::vector<NSTRUC*> df = get_d_frontier();
    if (df.empty()) return false;

    std::sort(df.begin(), df.end(), [](NSTRUC *a, NSTRUC *b) {
        return a->scoap.CO < b->scoap.CO;
    });

    NSTRUC *g = df[0];
    int want = non_controlling_value(g);

    NSTRUC *best_in = nullptr;
    int best_cost = std::numeric_limits<int>::max();

    for (unsigned k = 0; k < g->fin; k++) {
        NSTRUC *u = g->unodes[k];
        int idx = u->indx;

        if (good_val[idx] == LX && bad_val[idx] == LX) {
            int c = (want == 0) ? u->scoap.CC0 : u->scoap.CC1;
            if (c < best_cost) {
                best_cost = c;
                best_in = u;
            }
        }
    }

    if (!best_in) return false;

    obj_node = best_in;
    obj_val = want;
    return true;
}

static bool is_inverting_gate(NSTRUC *g) {
    return (g->type == NOT || g->type == NAND || g->type == NOR);
}

static NSTRUC* choose_backtrace_input(NSTRUC *np, int val) {
    NSTRUC *best = nullptr;
    int best_cost = std::numeric_limits<int>::max();

    for (unsigned i = 0; i < np->fin; i++) {
        NSTRUC *u = np->unodes[i];
        int idx = u->indx;

        if (!(good_val[idx] == LX && bad_val[idx] == LX)) continue;

        int c = (val == 0) ? u->scoap.CC0 : u->scoap.CC1;
        if (c < best_cost) {
            best_cost = c;
            best = u;
        }
    }

    return best; // no fallback
}

static void backtrace(NSTRUC *obj_node, int obj_val, NSTRUC* &pi_node, int &pi_val) {
    NSTRUC *cur = obj_node;
    int val = obj_val;

    while (cur->ntype != PI) {
        if (is_inverting_gate(cur)) {
            val = 1 - val;
        }
        cur = choose_backtrace_input(cur, val);
        if (cur == nullptr) break;
    }

    pi_node = cur;
    pi_val = val;
}

static int pi_index_from_node(NSTRUC *pi) {
    for (int i = 0; i < Npi; i++) {
        if (Pinput[i] == pi) return i;
    }
    return -1;
}

static bool x_path_exists_from_dfrontier() {
    return !get_d_frontier().empty();
}

static bool podem_rec(int fault_idx, int sa_val) {
    simulate_circuit((int)Node[fault_idx].num, sa_val);

    if (fault_at_po()) {
        return true;
    }

    if (fault_activated(fault_idx, sa_val) && !x_path_exists_from_dfrontier()) {
        return false;
    }

    NSTRUC *obj_node = nullptr;
    int obj_val = LX;
    if (!get_objective(fault_idx, sa_val, obj_node, obj_val)) {
        return false;
    }

    NSTRUC *pi_node = nullptr;
    int pi_val = LX;
    backtrace(obj_node, obj_val, pi_node, pi_val);

    if (pi_node == nullptr) return false;

    int pidx = pi_index_from_node(pi_node);
    if (pidx < 0) return false;

    int cur = pi_assign[pidx];
    if (cur != LX) return false;

    // Normal trial only if the PI is X:
    pi_assign[pidx] = pi_val;
    if (podem_rec(fault_idx, sa_val)) return true;

    pi_assign[pidx] = 1 - pi_val;
    if (podem_rec(fault_idx, sa_val)) return true;

    pi_assign[pidx] = LX;
    return false;
}

static int all_fanins_ready(NSTRUC *np) {
    for (unsigned i = 0; i < np->fin; i++) {
        if (np->unodes[i]->gate_output != 0 && np->unodes[i]->gate_output != 1)
            return 0;
    }
    return 1;
}

auto read_full_line = [](FILE *f, std::string &out) -> bool {
out.clear();
char tmp[MAXLINE];
while (fgets(tmp, MAXLINE, f)) {
    out += tmp;
    if (!out.empty() && out.back() == '\n') break; // got full line
}
if (out.empty()) return false;
// strip \r\n
while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
return true;
};
auto trim_inplace = [](char *s) {
    // remove trailing \r \n space \t
    int n = (int)strlen(s);
    while (n > 0 && (s[n-1] == '\r' || s[n-1] == '\n' || s[n-1] == ' ' || s[n-1] == '\t'))
        s[--n] = '\0';
    // remove leading spaces/tabs by shifting
    int p = 0;
    while (s[p] == ' ' || s[p] == '\t') p++;
    if (p > 0) memmove(s, s + p, strlen(s + p) + 1);
};

auto parse_3val = [](const char *tok) -> int {
    // 0 -> 0, 1 -> 1, x/X -> 2
    if (!tok || !tok[0]) return 2;
    char c = tok[0];
    if (c == '0') return 0;
    if (c == '1') return 1;
    if (c == 'x' || c == 'X') return 2;
    return 2;
};

auto print_3val = [](int v) -> char {
    if (v == 0) return '0';
    if (v == 1) return '1';
    return 'x';
};

// conservative 3-valued gate eval on node (inputs taken from np->unodes[])
auto eval_gate_3val = [](NSTRUC *np) -> int {
    const int VX = 2;
    int fin = (int)np->fin;

    // gather inputs
    // (treat any non {0,1,2} as X)
    auto in = [&](int i)->int{
        int v = np->unodes[i]->gate_output;
        if (v != 0 && v != 1 && v != 2) v = VX;
        return v;
    };

    switch (np->type) {
        case BRCH: // branch: pass-through
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
            // reuse AND rule quickly
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
            // conservative: if any X, output X
            for (int i = 0; i < fin; i++) if (in(i) == VX) return VX;
            int ones = 0;
            for (int i = 0; i < fin; i++) ones += (in(i) == 1);
            return (ones & 1) ? 1 : 0;
        }

        // If your template has XNOR, you can add it here similarly.
        default:
            return VX;
    }
};

 std::vector<std::vector<int>> controlling_helper (int controlling_value, NSTRUC *np){
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

 int cost_other_non_controlling (NSTRUC *np, int index) {
    int cost = 0;
    switch (np->type) {
        case NOT:
        case BRCH:
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
// ---------- end helpers ----------

/*-----------------------------------------------------------------------
input: nothing
output: nothing
called by: main 
description:
    Set Done to 1 which will terminates the program.
-----------------------------------------------------------------------*/
void quit(){
    Done = 1;
}

/*======================================================================*/

/*-----------------------------------------------------------------------
input: nothing
output: nothing
called by: cread
description:
    This routine clears the memory space occupied by the previous circuit
    before reading in new one. It frees up the dynamic arrays Node.unodes,
    Node.dnodes, Node.flist, Node, Pinput, Poutput, and Tap.
-----------------------------------------------------------------------*/
void clear(){
    int i;
    for(i = 0; i<Nnodes; i++) {
        free(Node[i].unodes);
        free(Node[i].dnodes);
    }
    // free(Node);
    delete[] Node;
    free(Pinput);
    free(Poutput);
    Gstate = EXEC;
}

/*-----------------------------------------------------------------------
input: nothing
output: nothing
called by: cread
description:
    This routine allocatess the memory space required by the circuit
    description data structure. It allocates the dynamic arrays Node,
    Node.flist, Node, Pinput, Poutput, and Tap. It also set the default
    tap selection and the fanin and fanout to 0.
-----------------------------------------------------------------------*/
void allocate(){
    int i;
    // Node = (NSTRUC *) malloc(Nnodes * sizeof(NSTRUC));
    /*NSTRUC contains a std::vector member (fault_list) so 
    allocating an array of NSTRUC with malloc()
    will produce "Uninitialised  value was created by a heap allocation." 
    Use new and delete to fix this*/
    Node = new NSTRUC[Nnodes];
    Pinput = (NSTRUC **) malloc(Npi * sizeof(NSTRUC *));
    Poutput = (NSTRUC **) malloc(Npo * sizeof(NSTRUC *));
    for(i = 0; i<Nnodes; i++) {
        Node[i].indx = i;
        Node[i].fin = Node[i].fout = 0;
    }
}


/*-----------------------------------------------------------------------
input: circuit description file name
output: nothing
called by: main
description:
    This routine reads in the circuit description file and set up all the
    required data structure. It first checks if the file exists, then it
    sets up a mapping table, determines the number of nodes, PI's and PO's,
    allocates dynamic data arrays, and fills in the structural information
    of the circuit. In the ISCAS circuit description format, only upstream
    nodes are specified. Downstream nodes are implied. However, to facilitate
    forward implication, they are also built up in the data structure.
    To have the maximal flexibility, three passes through the circuit file
    are required: the first pass to determine the size of the mapping table
    , the second to fill in the mapping table, and the third to actually
    set up the circuit information. These procedures may be simplified in
    the future.
-----------------------------------------------------------------------*/
std::string inp_name = "";
void cread(){
    char buf[MAXLINE];
    int ntbl, *tbl, i, j, k, nd, tp, fo, fi, ni = 0, no = 0;
    FILE *fd;
    NSTRUC *np;
    cp[strlen(cp)-1] = '\0';
    if((fd = fopen(cp,"r")) == NULL){
        printf("File does not exist!\n");
        return;
    }
    inp_name = cp;
    
    if(Gstate >= CKTLD) clear();
    Ngt = Nnodes = Npi = Npo = ntbl = 0;
    while(fgets(buf, MAXLINE, fd) != NULL) {
        if(sscanf(buf,"%d %d", &tp, &nd) == 2) {
            if(ntbl < nd) ntbl = nd;
            Nnodes ++;
            if(tp == PI) {
                Npi++;
            }
            else if(tp == PO) {
                Npo++;
                Ngt++;
            }
            else if(tp == GATE) {
                Ngt++;
            }
        }
    }
    tbl = (int *) malloc(++ntbl * sizeof(int));
    
    fseek(fd, 0L, 0);
    i = 0;
    while(fgets(buf, MAXLINE, fd) != NULL) {
        if(sscanf(buf,"%d %d", &tp, &nd) == 2) tbl[nd] = i++;
    }
    allocate();

    fseek(fd, 0L, 0);
    while(fscanf(fd, "%d %d", &tp, &nd) != EOF) {
        np = &Node[tbl[nd]];
        np->num = nd;
        np->level = -1;
        np->gate_output = -1;
        
        if(tp == PI) {
            Pinput[ni++] = np;
            np->ntype = PI;
        }
        else if(tp == PO) {
            Poutput[no++] = np;
            np->ntype = PO;
        }
        else if(tp == GATE) {
            np->ntype = GATE;
        }
        else if(tp == FB) {
            np->ntype = FB;
        }
        
        switch(tp) {
            case PI:
            case PO:
            case GATE: 
                {
                  int tmp_type;
                  fscanf(fd, "%d %d %d", &tmp_type, &np->fout, &np->fin);
                  np->type = (e_gtype)tmp_type;
                  break;
                }
            case FB:
                np->fout = np->fin = 1;
                {
                  int tmp_type;
                  fscanf(fd, "%d", &tmp_type);
                  np->type = (e_gtype)tmp_type;
                }
                break;
            default:
                printf("Unknown node type!\n");
                exit(-1);
        }
        np->unodes = (NSTRUC **) malloc(np->fin * sizeof(NSTRUC *));
        np->dnodes = (NSTRUC **) malloc(np->fout * sizeof(NSTRUC *));
        for(i = 0; i < np->fin; i++) {
            fscanf(fd, "%d", &nd);
            np->unodes[i] = &Node[tbl[nd]];
        }
        for(i = 0; i < np->fout; np->dnodes[i++] = NULL);
    }
    for(i = 0; i < Nnodes; i++) {
        for(j = 0; j < Node[i].fin; j++) {
            np = Node[i].unodes[j];
            k = 0;
            while(np->dnodes[k] != NULL) k++;
            np->dnodes[k] = &Node[i];
        }
    }
    
    int done = 0;
    while (!done) {
        done = 1;
        for(i = 0; i < Nnodes; i++) {
            np = &Node[i];

            if (np->level != -1) continue;

            tp = np->ntype;
            int max_fan = 0;
            int node_lvl = 0;
            int ready = 1;

            if (tp == PI) {
                np->level = 0;
            }
            else if (tp == PO || tp == GATE) {
                max_fan = -1;
                for(j = 0; j<np->fin; j++) {
                    node_lvl = np->unodes[j]->level;
                    if (node_lvl == -1) {
                        ready = 0;
                        done = 0;
                        break;
                    }
                    max_fan = (max_fan > node_lvl) ? max_fan : node_lvl;
                }
                if (ready)
                    np->level = max_fan + 1; 
            }
            else if (tp == FB) {
                node_lvl = np->unodes[0]->level;
                if (node_lvl == -1) {
                        ready = 0;
                        done = 0;
                        break;
                }
                if (ready)
                    np->level = node_lvl + 1; 
            }
        }
    }

    free(tbl);
    tbl = NULL;
    
    fclose(fd);
    Gstate = CKTLD;
    printf("==> OK");
}

/*-----------------------------------------------------------------------
input: nothing
output: nothing
called by: main
description:
    The routine prints out the circuit description from previous READ command.
-----------------------------------------------------------------------*/
void pc(){
    int i, j;
    NSTRUC *np;
    std::string gname(int);
   
    printf(" Node   Type \tIn     \t\t\tOut    \n");
    printf("------ ------\t-------\t\t\t-------\n");
    for(i = 0; i<Nnodes; i++) {
        np = &Node[i];
        printf("\t\t\t\t\t");
        for(j = 0; j<np->fout; j++) printf("%d ",np->dnodes[j]->num);
        printf("\r%5d  %s\t", np->num, gname(np->type).c_str());
        for(j = 0; j<np->fin; j++) printf("%d ",np->unodes[j]->num);
        printf("\n");
    }
    printf("Primary inputs:  ");
    for(i = 0; i<Npi; i++) printf("%d ",Pinput[i]->num);
    printf("\n");
    printf("Primary outputs: ");
    for(i = 0; i<Npo; i++) printf("%d ",Poutput[i]->num);
    printf("\n\n");
    printf("Number of nodes = %d\n", Nnodes);
    printf("Number of primary inputs = %d\n", Npi);
    printf("Number of primary outputs = %d\n", Npo);
}

std::string filterCkt(const std::string& s) {
    size_t slashPos = s.find_last_of('/');
    size_t cktPos   = s.find(".ckt");

    if (slashPos == std::string::npos || cktPos == std::string::npos || cktPos <= slashPos)
        return ""; // not found / invalid

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

void pfs() {
    // [cmd$] PFS <input-tp-file> <input-fl-file> <output-fl-file>
    char tpfile[MAXLINE], flfile[MAXLINE], outfile[MAXLINE];
    rstrip(cp);

    if (sscanf(cp, "%1023s %1023s %1023s", tpfile, flfile, outfile) != 3) {
        printf("Invalid Input!\n");
        return;
    }

    FILE *fd_tp = fopen(tpfile, "r");
    if (!fd_tp) {
        printf("Input file does not exist!\n");
        return;
    }
    FILE *fd_fl = fopen(flfile, "r");
    if (!fd_fl) {
        printf("Input file does not exist!\n");
        fclose(fd_tp);
        return;
    }
    FILE *fd_out = fopen(outfile, "w");
    if (!fd_out) {
        printf("Cannot open output file!\n");
        fclose(fd_tp);
        fclose(fd_fl);
        return;
    }

    // Build node_num -> node_index map for O(1) lookup
    std::unordered_map<int, int> idx_of_num;
    idx_of_num.reserve((size_t)Nnodes * 2);
    for (int i = 0; i < Nnodes; i++) idx_of_num[(int)Node[i].num] = i;

    // max level (assume already levelized elsewhere in your flow)
    int max_level = 0;
    for (int i = 0; i < Nnodes; i++) max_level = std::max(max_level, (int)Node[i].level);

    // -------- read fault list (fl-file-format: "n@sa") ----------
    std::vector<std::pair<int,int>> faults; // (node_num, sa_val)
    {
        char buf[MAXLINE];
        while (fgets(buf, MAXLINE, fd_fl)) {
            trim_inplace(buf);
            if (buf[0] == '\0') continue;

            int n = 0, sa = 0;
            // tolerate spaces: " 12@1 "
            if (sscanf(buf, "%d@%d", &n, &sa) != 2) continue;
            if (sa != 0 && sa != 1) continue;
            if (idx_of_num.find(n) == idx_of_num.end()) continue; // skip unknown nodes

            faults.push_back({n, sa});
        }
    }

    // Nothing to do
    if (faults.empty()) {
        fclose(fd_tp);
        fclose(fd_fl);
        fclose(fd_out);
        printf("==> OK");
        return;
    }

    // We'll store detected faults uniquely
    auto enc_fault = [](int node_num, int sa_val) -> unsigned long long {
        return ((unsigned long long)(unsigned int)node_num << 1) | (unsigned long long)(sa_val & 1);
    };
    std::unordered_set<unsigned long long> detected;
    detected.reserve(faults.size() * 2);

    // -------- read TP header (PI ids line) ----------
    std::string header;
    if (!read_full_line(fd_tp, header)) {
        printf("Empty input file!\n");
        fclose(fd_tp);
        fclose(fd_fl);
        fclose(fd_out);
        return;
    }

    std::vector<int> pi_ids;
    {
        std::stringstream ss(header);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            tok.erase(0, tok.find_first_not_of(" \t"));
            tok.erase(tok.find_last_not_of(" \t") + 1);
            if (!tok.empty()) pi_ids.push_back(std::atoi(tok.c_str()));
        }
    }

    // -------- PFS core ----------
    using Word = unsigned long;
    const int W = (int)(sizeof(Word) * 8);        // machine word bits
    const int KMAX = W - 1;                       // faults per pass
    const int F = (int)faults.size();

    auto make_mask = [&](int bits)->Word {
        // bits in [1..W]
        if (bits >= W) return (Word)(~(Word)0);
        return ((Word)1 << bits) - (Word)1;
    };

    // Temp storage per pass
    std::vector<Word> val((size_t)Nnodes, 0);
    std::vector<Word> force0((size_t)Nnodes, 0);
    std::vector<Word> force1((size_t)Nnodes, 0);

    // Read TP lines (may be long; your dfs uses an accumulator, keep that style)
    char buf[MAXLINE];
    std::string line_acc;
    line_acc.reserve(4096);

    while (fgets(buf, MAXLINE, fd_tp)) {
        line_acc += buf;
        if (strchr(buf, '\n') == NULL && !feof(fd_tp)) continue; // not a full line yet

        // normalize line
        std::vector<char> tmp(line_acc.begin(), line_acc.end());
        tmp.push_back('\0');
        trim_inplace(tmp.data());
        if (tmp[0] == '\0') {
            line_acc.clear();
            continue;
        }

        // parse PI values for this pattern
        std::vector<int> pi_vals;
        pi_vals.reserve(pi_ids.size());
        {
            char *saveptr = NULL;
            char *tok = strtok_r(tmp.data(), ",", &saveptr);
            while (tok) {
                trim_inplace(tok);
                int v = parse_3val(tok);
                // PFS here is binary-parallel; if 'x', treat as 0 (common/simple convention)
                if (v == 2) v = 0;
                pi_vals.push_back(v);
                tok = strtok_r(NULL, ",", &saveptr);
            }
        }
        while (pi_vals.size() < pi_ids.size()) pi_vals.push_back(0);

        // run PFS passes for this ONE test pattern
        for (int base = 0; base < F; base += KMAX) {
            const int K = std::min(KMAX, F - base);   // faults in this pass
            const int bits = K + 1;                   // include good bit
            const Word MASK = make_mask(bits);

            // reset pass data
            std::fill(val.begin(), val.end(), (Word)0);
            std::fill(force0.begin(), force0.end(), (Word)0);
            std::fill(force1.begin(), force1.end(), (Word)0);

            // build force masks for faults in this block
            for (int j = 0; j < K; j++) {
                int node_num = faults[base + j].first;
                int sa_val   = faults[base + j].second;
                int idx      = idx_of_num[node_num];
                Word bitmask = ((Word)1 << (j + 1)); // bit0 is good, bit(j+1) is this fault

                if (sa_val == 0) force0[idx] |= bitmask;
                else             force1[idx] |= bitmask;
            }

            // initialize PI words: replicate good value into all bits (then faults override)
            for (size_t p = 0; p < pi_ids.size(); p++) {
                int id = pi_ids[p];
                auto it = idx_of_num.find(id);
                if (it == idx_of_num.end()) continue;
                int idx = it->second;
                int gv  = pi_vals[p]; // 0/1

                Word w = gv ? MASK : (Word)0;  // replicate across all bits
                // apply fault forcing if PI itself is faulty in this block
                w = (w & ~force0[idx]) | force1[idx];
                val[idx] = w & MASK;
            }

            // simulate level-by-level
            for (int l = 0; l <= max_level; l++) {
                for (int i = 0; i < Nnodes; i++) {
                    NSTRUC *np = &Node[i];
                    if ((int)np->level != l) continue;

                    if (np->ntype == PI) continue;

                    Word outv = 0;

                    if (np->ntype == FB) {
                        // buffer/branch-like
                        if (np->fin > 0) outv = val[idx_of_num[(int)np->unodes[0]->num]];
                        else outv = 0;
                    } else {
                        // logic gate types (binary)
                        auto inw = [&](int k)->Word {
                            NSTRUC *u = np->unodes[k];
                            return val[idx_of_num[(int)u->num]];
                        };

                        switch (np->type) {
                            case BRCH:
                                outv = (np->fin > 0) ? inw(0) : 0;
                                break;
                            case NOT:
                                outv = (np->fin > 0) ? (~inw(0) & MASK) : 0;
                                break;
                            case AND: {
                                outv = MASK;
                                for (int k = 0; k < (int)np->fin; k++) outv &= inw(k);
                                outv &= MASK;
                                break;
                            }
                            case NAND: {
                                Word t = MASK;
                                for (int k = 0; k < (int)np->fin; k++) t &= inw(k);
                                outv = (~t) & MASK;
                                break;
                            }
                            case OR: {
                                outv = 0;
                                for (int k = 0; k < (int)np->fin; k++) outv |= inw(k);
                                outv &= MASK;
                                break;
                            }
                            case NOR: {
                                Word t = 0;
                                for (int k = 0; k < (int)np->fin; k++) t |= inw(k);
                                outv = (~t) & MASK;
                                break;
                            }
                            case XOR: {
                                Word t = 0;
                                if (np->fin > 0) {
                                    t = inw(0);
                                    for (int k = 1; k < (int)np->fin; k++) t ^= inw(k);
                                }
                                outv = t & MASK;
                                break;
                            }
                            default:
                                outv = 0;
                                break;
                        }
                    }

                    // apply stuck-at forcing for this node in this pass
                    outv = (outv & ~force0[i]) | force1[i];
                    val[i] = outv & MASK;
                }
            }

            // detect faults: if ANY PO differs between good(bit0) and fault bit
            Word detected_bits = 0; // bits 0..K (we’ll ignore bit0 later)
            for (int k = 0; k < Npo; k++) {
                int po_idx = idx_of_num[(int)Poutput[k]->num];
                Word y = val[po_idx] & MASK;

                Word good = (y & 1) ? MASK : (Word)0;     // replicate good across bits
                Word diff = (y ^ good) & MASK;            // bits that differ from good
                detected_bits |= (diff >> 1);             // align fault bits to [0..K-1]
            }

            // record detected faults from this pass
            for (int j = 0; j < K; j++) {
                if (detected_bits & ((Word)1 << j)) {
                    detected.insert(enc_fault(faults[base + j].first, faults[base + j].second));
                }
            }
        }

        line_acc.clear();
    }

    // -------- write output (unique, sorted by node then sa) ----------
    std::vector<std::pair<int,int>> out;
    out.reserve(detected.size());
    for (auto code : detected) {
        int node = (int)(code >> 1);
        int sa   = (int)(code & 1ULL);
        out.push_back({node, sa});
    }
    std::sort(out.begin(), out.end());

    for (auto &p : out) {
        fprintf(fd_out, "%d@%d\n", p.first, p.second);
    }

    fclose(fd_tp);
    fclose(fd_fl);
    fclose(fd_out);

    printf("==> OK");
}

void tpfc(){
    // TPFC <tp-count> <freq> <output-tp-file> <report-file>
    rstrip(cp);

    int ntot = 0;
    int freq = 0;
    char tpfile[MAXLINE];
    char repfile[MAXLINE];

    if (sscanf(cp, "%d %d %1023s %1023s", &ntot, &freq, tpfile, repfile) != 4) {
        printf("Invalid Input!\n");
        return;
    }
    if (ntot <= 0 || freq <= 0) {
        printf("Invalid Input!\n");
        return;
    }

    FILE *fd_tp = fopen(tpfile, "w");
    if (!fd_tp) {
        printf("Cannot open output tp file!\n");
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

    std::vector<int> pi_nums;
    pi_nums.reserve(Npi);
    std::unordered_map<int, NSTRUC*> pi_map;  // num -> PI node*
    pi_map.reserve(Npi * 2);

    for (int i = 0; i < Npi; i++) {
        int id = (int)Pinput[i]->num;
        pi_nums.push_back(id);
        pi_map[id] = Pinput[i];
    }
    std::sort(pi_nums.begin(), pi_nums.end());

    for (int i = 0; i < (int)pi_nums.size(); i++) {
        if (i) fprintf(fd_tp, ",");
        fprintf(fd_tp, "%d", pi_nums[i]);
    }
    fprintf(fd_tp, "\n");

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

    int generated = 0;
    int last_interval_detected = 0;

    for (int t = 1; t <= ntot; t++) {

        std::vector<int> pi_vals(Npi, 0);
        for (int i = 0; i < Npi; i++) {
            pi_vals[i] = std::rand() & 1; // 0/1
        }

        for (int i = 0; i < Npi; i++) {
            if (i) fprintf(fd_tp, ",");
            fprintf(fd_tp, "%c", pi_vals[i] ? '1' : '0');
        }
        fprintf(fd_tp, "\n");
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

        if (t % freq == 0) {
            double cov = 100.0 * (double)detected.size() / (double)total_faults;
            fprintf(fd_rep, "%.2f\n", cov);

            // saturation detect: no new faults in this interval
            int now = (int)detected.size();
            // if (now == last_interval_detected) {
            //     break;
            // }
            last_interval_detected = now;
        }
    }

    if (generated % freq != 0) {
        double cov = 100.0 * (double)detected.size() / (double)total_faults;
        fprintf(fd_rep, "%.2f\n", cov);
    }

    fclose(fd_tp);
    fclose(fd_rep);

    printf("==> OK");
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

            switch (gate_tp) {
                case BRCH:
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
                            break;
                        }
                        v0.push_back(np->unodes[j]->scoap.CC0);
                        v1.push_back(np->unodes[j]->scoap.CC1);
                    }
                    np->scoap.CC0 = 1 +  std::accumulate(v0.begin(), v0.end(), 0);
                    np->scoap.CC1 = 1 + *std::min_element(v1.begin(), v1.end());
                    break; 
                case NOR:
                    for (j = 0; j < np->fin; j++) {
                        if (np->unodes[j]->scoap.CC0 == -1 || np->unodes[j]->scoap.CC1 == -1) {
                            done = 0;
                            break;
                        }
                        v0.push_back(np->unodes[j]->scoap.CC0);
                        v1.push_back(np->unodes[j]->scoap.CC1);
                    }
                    np->scoap.CC1 = 1 +  std::accumulate(v0.begin(), v0.end(), 0);
                    np->scoap.CC0 = 1 + *std::min_element(v1.begin(), v1.end());
                    break;
                case NOT:
                    np->scoap.CC0 = np->unodes[0]->scoap.CC1 + 1;
                    np->scoap.CC1 = np->unodes[0]->scoap.CC0 + 1;
                    break;
                case NAND:
                    for (j = 0; j < np->fin; j++) {
                        if (np->unodes[j]->scoap.CC0 == -1 || np->unodes[j]->scoap.CC1 == -1) {
                            done = 0;
                            break;
                        }
                        v0.push_back(np->unodes[j]->scoap.CC0);
                        v1.push_back(np->unodes[j]->scoap.CC1);
                    }
                    np->scoap.CC1 = 1 + *std::min_element(v0.begin(), v0.end());
                    np->scoap.CC0 = 1 + std::accumulate(v1.begin(), v1.end(), 0);
                    break;
                case AND:
                    for (j = 0; j < np->fin; j++) {
                        if (np->unodes[j]->scoap.CC0 == -1 || np->unodes[j]->scoap.CC1 == -1) {
                            done = 0;
                            break;
                        }
                        v0.push_back(np->unodes[j]->scoap.CC0);
                        v1.push_back(np->unodes[j]->scoap.CC1);
                    }
                    np->scoap.CC0 = 1 + *std::min_element(v0.begin(), v0.end());
                    np->scoap.CC1 = 1 + std::accumulate(v1.begin(), v1.end(), 0);
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



static int dlag_node_index_by_num(int node_num) {
    auto it = idx_of_num.find(node_num);
    if (it == idx_of_num.end()) return -1;
    return it->second;
}

struct DlagSimResult {
    std::vector<int> good;
    std::vector<int> faulty;
};

static DlagSimResult dlag_simulate_pattern(const std::vector<int> &assign, int fault_node_num, int stuck_at) {
    DlagSimResult res;
    res.good.assign(Nnodes, LX);
    res.faulty.assign(Nnodes, LX);

    for (int i = 0; i < Npi; i++) {
        int v = (i < (int)assign.size()) ? assign[i] : LX;
        int idx = Pinput[i]->indx;
        res.good[idx] = v;
        res.faulty[idx] = ((int)Pinput[i]->num == fault_node_num) ? stuck_at : v;
    }

    int max_level = 0;
    for (int i = 0; i < Nnodes; i++) {
        if (Node[i].level > max_level) max_level = Node[i].level;
    }

    for (int l = 0; l <= max_level; l++) {
        for (int i = 0; i < Nnodes; i++) {
            NSTRUC *np = &Node[i];
            if (np->level != l) continue;
            if (np->ntype == PI) continue;

            std::vector<int> gins, fins;
            gins.reserve(np->fin);
            fins.reserve(np->fin);
            for (unsigned k = 0; k < np->fin; k++) {
                gins.push_back(res.good[np->unodes[k]->indx]);
                fins.push_back(res.faulty[np->unodes[k]->indx]);
            }

            int gv = eval_gate_from_inputs(np, res.good);
            int fv = eval_gate_from_inputs(np, res.faulty);
            if (np->ntype == FB) {
                gv = gins.empty() ? LX : gins[0];
                fv = fins.empty() ? LX : fins[0];
            }

            res.good[np->indx] = gv;
            res.faulty[np->indx] = ((int)np->num == fault_node_num) ? stuck_at : fv;
        }
    }

    return res;
}

static bool dlag_pattern_detects_fault(const std::vector<int> &assign, int fault_node_num, int stuck_at) {
    DlagSimResult sim = dlag_simulate_pattern(assign, fault_node_num, stuck_at);
    for (int i = 0; i < Npo; i++) {
        int idx = Poutput[i]->indx;
        int g = sim.good[idx];
        int f = sim.faulty[idx];
        if (g != LX && f != LX && g != f) return true;
    }
    return false;
}

static bool dlag_can_still_activate(const std::vector<int> &assign, int fault_node_num, int stuck_at) {
    int fault_idx = dlag_node_index_by_num(fault_node_num);
    if (fault_idx < 0) return false;
    DlagSimResult sim = dlag_simulate_pattern(assign, fault_node_num, stuck_at);
    int g = sim.good[fault_idx];
    int f = sim.faulty[fault_idx];
    if (g != LX && f != LX && g != f) return true;
    return (g == LX || f == LX);
}

static bool dlag_has_possible_propagation(const std::vector<int> &assign, int fault_node_num, int stuck_at) {
    DlagSimResult sim = dlag_simulate_pattern(assign, fault_node_num, stuck_at);
    for (int i = 0; i < Npo; i++) {
        int idx = Poutput[i]->indx;
        int g = sim.good[idx];
        int f = sim.faulty[idx];
        if (g != LX && f != LX && g != f) return true;
        if (g == LX || f == LX) return true;
    }
    return false;
}

static bool dlag_backtrack_search(const std::vector<int> &order,
                                  int depth,
                                  std::vector<int> &assign,
                                  int fault_node_num,
                                  int stuck_at,
                                  std::vector<int> &solution) {
    if (!dlag_can_still_activate(assign, fault_node_num, stuck_at)) return false;
    if (!dlag_has_possible_propagation(assign, fault_node_num, stuck_at)) return false;

    if (dlag_pattern_detects_fault(assign, fault_node_num, stuck_at)) {
        solution = assign;
        return true;
    }

    if (depth >= (int)order.size()) return false;

    int pi_idx = order[depth];
    int first = 0, second = 1;

    if ((int)Pinput[pi_idx]->num == fault_node_num) {
        first = 1 - stuck_at;
        second = stuck_at;
    } else {
        int cc0 = Pinput[pi_idx]->scoap.CC0;
        int cc1 = Pinput[pi_idx]->scoap.CC1;
        if (cc0 >= 0 && cc1 >= 0 && cc1 < cc0) {
            first = 1;
            second = 0;
        }
    }

    assign[pi_idx] = first;
    if (dlag_backtrack_search(order, depth + 1, assign, fault_node_num, stuck_at, solution)) return true;

    assign[pi_idx] = second;
    if (dlag_backtrack_search(order, depth + 1, assign, fault_node_num, stuck_at, solution)) return true;

    assign[pi_idx] = LX;
    return false;
}

static std::vector<int> dlag_compress_to_ternary(const std::vector<int> &binary_assign,
                                                 int fault_node_num,
                                                 int stuck_at) {
    std::vector<int> out = binary_assign;
    for (int i = 0; i < Npi; i++) {
        int oldv = out[i];
        out[i] = LX;
        if (!dlag_pattern_detects_fault(out, fault_node_num, stuck_at)) out[i] = oldv;
    }
    return out;
}


static void dlag_compute_scoap_internal() {
    int big_number = std::numeric_limits<int>::max() / 4;
    for (int i = 0; i < Nnodes; i++) {
        Node[i].scoap.CC0 = -1;
        Node[i].scoap.CC1 = -1;
        Node[i].scoap.CO = big_number;
    }
    for (int i = 0; i < Npi; i++) {
        Pinput[i]->scoap.CC0 = 1;
        Pinput[i]->scoap.CC1 = 1;
    }
    for (int i = 0; i < Npo; i++) {
        Poutput[i]->scoap.CO = 0;
    }

    bool done = false;
    while (!done) {
        done = true;
        for (int i = 0; i < Nnodes; i++) {
            NSTRUC *np = &Node[i];
            if (np->ntype == PI) continue;

            std::vector<int> v0, v1;
            switch (np->type) {
                case BRCH:
                    if (np->unodes[0]->scoap.CC0 == -1 || np->unodes[0]->scoap.CC1 == -1) {
                        done = false;
                        break;
                    }
                    np->scoap.CC0 = np->unodes[0]->scoap.CC0;
                    np->scoap.CC1 = np->unodes[0]->scoap.CC1;
                    break;
                case NOT:
                    if (np->unodes[0]->scoap.CC0 == -1 || np->unodes[0]->scoap.CC1 == -1) {
                        done = false;
                        break;
                    }
                    np->scoap.CC0 = np->unodes[0]->scoap.CC1 + 1;
                    np->scoap.CC1 = np->unodes[0]->scoap.CC0 + 1;
                    break;
                case OR:
                case NOR:
                case NAND:
                case AND:
                    for (int j = 0; j < (int)np->fin; j++) {
                        if (np->unodes[j]->scoap.CC0 == -1 || np->unodes[j]->scoap.CC1 == -1) {
                            done = false;
                            v0.clear();
                            v1.clear();
                            break;
                        }
                        v0.push_back(np->unodes[j]->scoap.CC0);
                        v1.push_back(np->unodes[j]->scoap.CC1);
                    }
                    if (!v0.empty()) {
                        if (np->type == OR) {
                            np->scoap.CC0 = 1 + std::accumulate(v0.begin(), v0.end(), 0);
                            np->scoap.CC1 = 1 + *std::min_element(v1.begin(), v1.end());
                        } else if (np->type == NOR) {
                            np->scoap.CC1 = 1 + std::accumulate(v0.begin(), v0.end(), 0);
                            np->scoap.CC0 = 1 + *std::min_element(v1.begin(), v1.end());
                        } else if (np->type == NAND) {
                            np->scoap.CC1 = 1 + *std::min_element(v0.begin(), v0.end());
                            np->scoap.CC0 = 1 + std::accumulate(v1.begin(), v1.end(), 0);
                        } else if (np->type == AND) {
                            np->scoap.CC0 = 1 + *std::min_element(v0.begin(), v0.end());
                            np->scoap.CC1 = 1 + std::accumulate(v1.begin(), v1.end(), 0);
                        }
                    }
                    break;
                default:
                    break;
            }
        }
    }

    done = false;
    while (!done) {
        done = true;
        for (int i = 0; i < Nnodes; i++) {
            NSTRUC *np = &Node[i];
            if (np->scoap.CO == big_number) {
                done = false;
                continue;
            }
            for (int j = 0; j < (int)np->fin; j++) {
                NSTRUC *in = np->unodes[j];
                int contrib = np->scoap.CO + cost_other_non_controlling(np, j);
                if (np->type != BRCH) contrib += 1;
                if (contrib < in->scoap.CO) {
                    in->scoap.CO = contrib;
                    done = false;
                }
            }
        }
    }
}

static bool dlag_write_tp_output_file(const char *outfile, const std::vector<int> &assign) {
    FILE *fd = fopen(outfile, "w");
    if (!fd) return false;

    std::vector<std::pair<int,int>> ordered;
    ordered.reserve(Npi);
    for (int i = 0; i < Npi; i++) ordered.push_back({(int)Pinput[i]->num, i});
    std::sort(ordered.begin(), ordered.end());

    for (int i = 0; i < (int)ordered.size(); i++) {
        if (i) fprintf(fd, ",");
        fprintf(fd, "%d", ordered[i].first);
    }
    fprintf(fd, "\n");

    for (int i = 0; i < (int)ordered.size(); i++) {
        if (i) fprintf(fd, ",");
        fprintf(fd, "%c", print_3val(assign[ordered[i].second]));
    }
    fprintf(fd, "\n");
    fclose(fd);
    return true;
}


void dlag() {
    int fault_num, sa_val;
    char outfile[MAXLINE];
    rstrip(cp);

    if (sscanf(cp, "%d %d %1023s", &fault_num, &sa_val, outfile) != 3) {
        printf("Invalid Input!\n");
        return;
    }
    if (sa_val != 0 && sa_val != 1) {
        printf("Invalid Input!\n");
        return;
    }

    idx_of_num.clear();
    for (int i = 0; i < Nnodes; i++) idx_of_num[(int)Node[i].num] = i;

    if (idx_of_num.find(fault_num) == idx_of_num.end()) {
        FILE *fd = fopen(outfile, "w");
        if (fd) fclose(fd);
        printf("Invalid fault node!\n");
        return;
    }

    bool need_scoap = false;
    for (int i = 0; i < Nnodes; i++) {
        if (Node[i].scoap.CC0 <= 0 || Node[i].scoap.CC1 <= 0 || Node[i].scoap.CO < 0) {
            need_scoap = true;
            break;
        }
    }
    if (need_scoap) {
        dlag_compute_scoap_internal();
    }

    std::vector<int> order(Npi);
    for (int i = 0; i < Npi; i++) order[i] = i;
    std::sort(order.begin(), order.end(), [](int a, int b) {
        NSTRUC *pa = Pinput[a];
        NSTRUC *pb = Pinput[b];
        int ca = std::min(pa->scoap.CC0, pa->scoap.CC1) + pa->scoap.CO;
        int cb = std::min(pb->scoap.CC0, pb->scoap.CC1) + pb->scoap.CO;
        if (ca != cb) return ca < cb;
        return pa->num < pb->num;
    });

    std::vector<int> assign(Npi, LX);
    std::vector<int> solution;
    bool ok = dlag_backtrack_search(order, 0, assign, fault_num, sa_val, solution);

    if (!ok) {
        FILE *fd = fopen(outfile, "w");
        if (!fd) {
            printf("Cannot open output file!\n");
            return;
        }
        fclose(fd);
        printf("==> OK");
        return;
    }

    std::vector<int> ternary = dlag_compress_to_ternary(solution, fault_num, sa_val);
    if (!dlag_write_tp_output_file(outfile, ternary)) {
        printf("Cannot open output file!\n");
        return;
    }

    printf("==> OK");
}

void podem() {
    int fault_num, sa_val;
    char outfile[MAXLINE];
    rstrip(cp);

    if (sscanf(cp, "%d %d %1023s", &fault_num, &sa_val, outfile) != 3) {
        printf("Invalid Input!\n");
        return;
    }
    if (sa_val != 0 && sa_val != 1) {
        printf("Invalid Input!\n");
        return;
    }

    FILE *fd = fopen(outfile, "w");
    if (!fd) {
        printf("Cannot open output file!\n");
        return;
    }

    idx_of_num.clear();
    for (int i = 0; i < Nnodes; i++) {
        idx_of_num[(int)Node[i].num] = i;
    }

    auto it = idx_of_num.find(fault_num);
    if (it == idx_of_num.end()) {
        fclose(fd);
        printf("Invalid fault node!\n");
        return;
    }

    pi_assign.assign(Npi, LX);

    bool ok = podem_rec(it->second, sa_val);

    if (ok) {
        for (int i = 0; i < Npi; i++) {
            if (i) fprintf(fd, ",");
            fprintf(fd, "%d", (int)Pinput[i]->num);
        }
        fprintf(fd, "\n");

        for (int i = 0; i < Npi; i++) {
            if (i) fprintf(fd, ",");
            char c = (pi_assign[i] == 0) ? '0' : (pi_assign[i] == 1) ? '1' : 'x';
            fprintf(fd, "%c", c);
        }
        fprintf(fd, "\n");
    }

    fclose(fd);
    printf("==> OK");
}
	
/*======================= TPG (Phase 4) ================================*/

// Internal DALG helper: returns true on success and fills tp_out with the
// ternary PI assignment (indexed by Pinput[i]). No file I/O.
static bool run_dalg_internal(int fault_node_num, int sa_val, std::vector<int> &tp_out) {
    tp_out.clear();
    if (sa_val != 0 && sa_val != 1) return false;

    idx_of_num.clear();
    for (int i = 0; i < Nnodes; i++) idx_of_num[(int)Node[i].num] = i;
    if (idx_of_num.find(fault_node_num) == idx_of_num.end()) return false;

    bool need_scoap = false;
    for (int i = 0; i < Nnodes; i++) {
        if (Node[i].scoap.CC0 <= 0 || Node[i].scoap.CC1 <= 0 || Node[i].scoap.CO < 0) {
            need_scoap = true; break;
        }
    }
    if (need_scoap) dlag_compute_scoap_internal();

    std::vector<int> order(Npi);
    for (int i = 0; i < Npi; i++) order[i] = i;
    std::sort(order.begin(), order.end(), [](int a, int b) {
        NSTRUC *pa = Pinput[a];
        NSTRUC *pb = Pinput[b];
        int ca = std::min(pa->scoap.CC0, pa->scoap.CC1) + pa->scoap.CO;
        int cb = std::min(pb->scoap.CC0, pb->scoap.CC1) + pb->scoap.CO;
        if (ca != cb) return ca < cb;
        return pa->num < pb->num;
    });

    std::vector<int> assign(Npi, LX);
    std::vector<int> solution;
    bool ok = dlag_backtrack_search(order, 0, assign, fault_node_num, sa_val, solution);
    if (!ok) return false;

    tp_out = dlag_compress_to_ternary(solution, fault_node_num, sa_val);
    return true;
}

// Internal PODEM helper: returns true on success and fills tp_out.
static bool run_podem_internal(int fault_node_num, int sa_val, std::vector<int> &tp_out) {
    tp_out.clear();
    if (sa_val != 0 && sa_val != 1) return false;

    idx_of_num.clear();
    for (int i = 0; i < Nnodes; i++) idx_of_num[(int)Node[i].num] = i;
    auto it = idx_of_num.find(fault_node_num);
    if (it == idx_of_num.end()) return false;

    pi_assign.assign(Npi, LX);
    bool ok = podem_rec(it->second, sa_val);
    if (!ok) return false;

    tp_out = pi_assign;
    return true;
}

// Ternary/binary fault simulation wrapper: does "assign" detect node@sa?
static inline bool tpg_pattern_detects(const std::vector<int> &assign,
                                       int fault_node_num, int sa_val) {
    return dlag_pattern_detects_fault(assign, fault_node_num, sa_val);
}

// A simple single-fault single-pattern check loop used to drop detected faults
// after a new test pattern is added. For baseline we just reuse the
// good/faulty simulator that already handles ternary X values.
static void tpg_drop_detected_faults(const std::vector<int> &assign,
                                     std::vector<std::pair<int,int>> &flist) {
    std::vector<std::pair<int,int>> kept;
    kept.reserve(flist.size());
    for (auto &f : flist) {
        if (!tpg_pattern_detects(assign, f.first, f.second)) kept.push_back(f);
    }
    flist.swap(kept);
}

static bool tpg_write_tp_file(const char *outfile,
                              const std::vector<std::vector<int>> &tps) {
    FILE *fd = fopen(outfile, "w");
    if (!fd) return false;

    // header: PI node numbers sorted ascending
    std::vector<std::pair<int,int>> ordered;
    ordered.reserve(Npi);
    for (int i = 0; i < Npi; i++) ordered.push_back({(int)Pinput[i]->num, i});
    std::sort(ordered.begin(), ordered.end());

    for (int i = 0; i < (int)ordered.size(); i++) {
        if (i) fprintf(fd, ",");
        fprintf(fd, "%d", ordered[i].first);
    }
    fprintf(fd, "\n");

    for (const auto &tp : tps) {
        for (int i = 0; i < (int)ordered.size(); i++) {
            if (i) fprintf(fd, ",");
            int v = tp[ordered[i].second];
            char c = (v == 0) ? '0' : (v == 1) ? '1' : 'x';
            fprintf(fd, "%c", c);
        }
        fprintf(fd, "\n");
    }
    fclose(fd);
    return true;
}

/*
TPG - Phase 4 baseline TPG main flow.

Usage:
    TPG <alg> <rtpg-count> <output-tp-file>

    <alg>          : DALG | PODEM
    <rtpg-count>   : non-negative integer; number of random binary patterns
                     to generate first (0 to skip RTPG stage)
    <output-tp-file>: path of the final test pattern file

Flow:
    1. Build the full single stuck-at fault list (node@0 and node@1 for all nodes).
    2. RTPG stage: generate <rtpg-count> random binary patterns, add each one
       to the final TP set, and drop any faults detected by them.
    3. Deterministic ATPG stage: for every remaining fault, call the chosen
       ATPG algorithm (DALG or PODEM). On success, add the pattern to the
       final TP set and drop all faults it detects. On failure, continue.
    4. Write the final TP set to <output-tp-file>.
*/
void tpg() {
    char alg_str[MAXLINE];
    int  rtpg_cnt = -1;
    char outfile[MAXLINE];

    rstrip(cp);
    if (sscanf(cp, "%1023s %d %1023s", alg_str, &rtpg_cnt, outfile) != 3) {
        printf("Invalid Input!\n");
        return;
    }
    for (char *q = alg_str; *q; ++q) *q = Upcase(*q);

    bool use_dalg;
    if      (strcmp(alg_str, "DALG")  == 0) use_dalg = true;
    else if (strcmp(alg_str, "PODEM") == 0) use_dalg = false;
    else { printf("Invalid Input!\n"); return; }

    if (rtpg_cnt < 0) { printf("Invalid Input!\n"); return; }

    // Make sure node num -> idx map is ready for ATPG helpers.
    idx_of_num.clear();
    for (int i = 0; i < Nnodes; i++) idx_of_num[(int)Node[i].num] = i;

    // Step A: build full single stuck-at fault list (fault = {node_num, sa}).
    std::vector<std::pair<int,int>> flist;
    flist.reserve((size_t)Nnodes * 2);
    for (int i = 0; i < Nnodes; i++) {
        flist.push_back({(int)Node[i].num, 0});
        flist.push_back({(int)Node[i].num, 1});
    }
    int total_faults = (int)flist.size();

    std::vector<std::vector<int>> final_tps;
    final_tps.reserve((size_t)rtpg_cnt + 64);

    // Step B: RTPG stage (binary patterns).
    for (int t = 0; t < rtpg_cnt && !flist.empty(); t++) {
        std::vector<int> pat(Npi, 0);
        for (int i = 0; i < Npi; i++) pat[i] = std::rand() & 1;
        final_tps.push_back(pat);
        tpg_drop_detected_faults(pat, flist);
    }

    // Step C: Deterministic ATPG stage on remaining faults.
    // Iterate over a snapshot so dropping during the loop is safe.
    std::vector<std::pair<int,int>> pending = flist;
    for (const auto &f : pending) {
        if (flist.empty()) break;
        // skip already dropped faults
        bool still_there = false;
        for (const auto &g : flist) {
            if (g.first == f.first && g.second == f.second) { still_there = true; break; }
        }
        if (!still_there) continue;

        std::vector<int> tp;
        bool ok = use_dalg ? run_dalg_internal(f.first, f.second, tp)
                           : run_podem_internal(f.first, f.second, tp);
        if (!ok || (int)tp.size() != Npi) continue;

        final_tps.push_back(tp);
        tpg_drop_detected_faults(tp, flist);
    }

    if (!tpg_write_tp_file(outfile, final_tps)) {
        printf("Cannot open output file!\n");
        return;
    }

    int covered = total_faults - (int)flist.size();
    double fc = total_faults ? (100.0 * covered / total_faults) : 0.0;
    printf("TPG: alg=%s rtpg=%d tps=%d FC=%.2f%% (%d/%d)\n",
           use_dalg ? "DALG" : "PODEM",
           rtpg_cnt, (int)final_tps.size(), fc, covered, total_faults);
    printf("==> OK");
}

/*========================= End of program ============================*/