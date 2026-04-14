#include "globals.h"
#include "readckt.h"
#include "help.h"
#include "quit.h"
#include "lev.h"
#include "logicsim.h"
#include "rtpg.h"
#include "rfl.h"
#include "dfs.h"
#include "pfs.h"
#include "tpfc.h"
#include "scoap.h"
#include "dalg.h"
#include "podem.h"
#include "tpg.h"
#include <stdio.h>
#include <cstdlib>
#include <cstring>

int main(int argc, char *argv[])
{
   int com;
   char cline[MAXLINE], wstr[MAXLINE];

   // Phase 4: reproducible random seeding.
   // Usage: ./simulator <optional_seed>
   // If no seed is provided, the default 658 is used.
   unsigned int seed = 658;
   if (argc >= 2) {
      char *endp = nullptr;
      long v = std::strtol(argv[1], &endp, 10);
      if (endp && *endp == '\0') seed = (unsigned int)v;
   }
   std::srand(seed);

   while(!Done) {
      printf("\nCommand>");
      if (fgets(cline, MAXLINE, stdin) == nullptr) break;
      if(sscanf(cline, "%s", wstr) != 1) continue;
      cp = wstr;
      while(*cp){
	*cp= Upcase(*cp);
	cp++;
      }
      cp = cline + strlen(wstr) + 1;
      com = READ;
      while(com < NUMFUNCS && strcmp(wstr, command[com].name)) com++;
      if(com < NUMFUNCS) {
         if(command[com].state <= Gstate) (*command[com].fptr)();
         else printf("Execution out of sequence!\n");
      }
      else system(cline);
   }
}
