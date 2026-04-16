#include "globals.h"

/* Global state variables - defined here, declared extern in globals.h */
enum e_state Gstate = EXEC;     /* global execution sequence */
NSTRUC *Node;                   /* dynamic array of nodes */
NSTRUC **Pinput;                /* pointer to array of primary inputs */
NSTRUC **Poutput;               /* pointer to array of primary outputs */
int Nnodes;                     /* number of nodes */
int Npi;                        /* number of primary inputs */
int Npo;                        /* number of primary outputs */
int Ngt;                        /* number of gates */
int Done = 0;                   /* status bit to terminate program */
char *cp;                       /* current pointer in command line */
char inFile[MAXLINE];           /* input file name */
std::string inp_name = "";      /* input file name (for lev command) */

/* Global maps/vectors used by various commands */
std::unordered_map<int, int> idx_of_num;
std::vector<int> pi_assign;
std::vector<int> good_val;
std::vector<int> bad_val;

/* Command table - defined here after all command declarations */
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
   {"DALG", dalg, CKTLD},
   {"PODEM", podem, CKTLD},
   {"TPG", tpg, CKTLD},
   {"DTPFC", dtpfc, CKTLD},
};
