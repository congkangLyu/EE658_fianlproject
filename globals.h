#ifndef GLOBALS_H
#define GLOBALS_H

#include <string>
#include <vector>
#include <unordered_map>
#include <ctype.h>

/*=======================================================================
  Global definitions and forward declarations for the circuit simulator.
=======================================================================*/

/* Constants */
#define MAXLINE 1024               /* Input buffer size */
#define MAXNAME 31                 /* File name size */

#define Upcase(x) ((isalpha(x) && islower(x))? toupper(x) : (x))
#define Lowcase(x) ((isalpha(x) && isupper(x))? tolower(x) : (x))

#define NUMFUNCS 16

/* Three-valued logic constants */
static const int L0 = 0;
static const int L1 = 1;
static const int LX = 2;

/* Enums */
enum e_com {READ, PC, HELP, QUIT, LEV, LOGICSIM, RTPG, RFL, DFS, PFS, TPFC, SCOAP, DALG, PODEM, TPG};
enum e_state {EXEC, CKTLD};         /* Gstate values */
enum e_ntype {GATE, PI, FB, PO};    /* column 1 of circuit format */
enum e_gtype {IPT, BRCH, XOR, OR, NOR, NOT, NAND, AND, XNOR, BUF};  /* gate types */

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
    int gate_output;           /* Output value of the gate */
    std::vector<std::vector<int>> fault_list;    /* List of detectable faults of the gate */
    Scoap scoap;
} NSTRUC;

struct cmdstruc {
   char name[MAXNAME];        /* command syntax */
   void (*fptr)();            /* function pointer of the commands */
   enum e_state state;        /* execution state sequence */
};

/* Global state variables */
extern enum e_state Gstate;     /* global execution sequence */
extern NSTRUC *Node;            /* dynamic array of nodes */
extern NSTRUC **Pinput;         /* pointer to array of primary inputs */
extern NSTRUC **Poutput;        /* pointer to array of primary outputs */
extern int Nnodes;              /* number of nodes */
extern int Npi;                 /* number of primary inputs */
extern int Npo;                 /* number of primary outputs */
extern int Ngt;                 /* number of gates */
extern int Done;                /* status bit to terminate program */
extern char *cp;                /* current pointer in command line */
extern char inFile[MAXLINE];    /* input file name */
extern std::string inp_name;    /* input file name (for lev command) */

/* Global maps/vectors used by various commands */
extern std::unordered_map<int, int> idx_of_num;
extern std::vector<int> pi_assign;
extern std::vector<int> good_val;
extern std::vector<int> bad_val;

/* Command table and declarations */
extern struct cmdstruc command[NUMFUNCS];

/* Command function declarations */
void cread();
void pc();
void help();
void quit();
void lev();
void logicsim();
void rtpg();
void rfl();
void dfs();
void pfs();
void tpfc();
void scoap();
void dalg();
void podem();
void tpg();
void dtpfc();

#endif /* GLOBALS_H */
