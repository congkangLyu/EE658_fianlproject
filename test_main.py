# Do NOT change this file.

import os
from utils import print_test_title, pars_args, prepare_env

WEB_ADDR = "https://sportlab.usc.edu/~msabrishami/files"
WEB_DIR = "ee658"
TEST_DIR = "auto-tests-phase3"
 
if __name__ == '__main__':
   
    # Parsing the arguments 
    args = pars_args()
  
    # Downloading the auto-test files and replace the existings files
    if not args.dnD:
        prepare_env(args)
    
    # Building the simulator (i.e. a new build directory and compile)
    if not args.dnC:
        os.system("rm -rf build; mkdir build && cd build; cmake ..; make")

    if not os.path.exists('./build/simulator'):
        print('No simulator could be found')
        exit(1)
    
    
    # Running the tests for each module
    exit_vals = [] 
    for t in args.func:
        if isinstance(t, tuple):
            func, alg = t
            print_test_title(alg.upper())
        else:
            alg = args.alg
            func = t
            print_test_title(func.upper())
        run_cmd = f'python3 test_{func}.py -tlim {args.tlim}'
        if func =="atpg": run_cmd += f" -alg {alg}"
        
        try: 
            exit_vals.append(os.system(run_cmd))
        except: 
            print("oopps!")
            exit_vals.append(1)
    
    # Removing the auto-tests directory 
    if not args.dnR:
        os.system(f'rm -rf {TEST_DIR}')
    
    exit(0) if sum(exit_vals) == 0 else exit(1) 
    
