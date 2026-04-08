#include "readckt.h"
#include "utils.h"
#include <stdio.h>
#include <cstdlib>
#include <cstring>
#include <string>

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
