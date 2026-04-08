# Do NOT change this file.

import os
import glob
import subprocess
import time
from utils import *

SCOAP_CMD_TESTS_DIR  = f"{CMD_DIR}/scoap" 
SCOAP_OUTPUT_DIR  = f"{OUTPUT_DIR}/scoap" 
SCOAP_GOLDEN_DIR  = f"{GOLDEN_DIR}/scoap" 

def load_cmds(ckts):
    """
    cmd file name format:
    <cname>_scoap.cmd
    """
    cmds = glob.glob(f"{SCOAP_CMD_TESTS_DIR}/*")
    cmds = [cmd.split("/")[-1] for cmd in cmds]
    return cmds



def check_scoap(answer, golden):
    passed=True
    golden_result = open(golden,'r').readlines()
    answer_result = open(answer,'r').readlines()

    for line in answer_result:
        if line and line not in golden_result:
            print(f"{bcolors.FAIL}\n")
            print(f"{bcolors.FAIL}\nWrong line in output:")
            print(f"Error on\n\t{bcolors.FAIL}"+line)
            passed = False

    for line in golden_result:
        if line and line not in answer_result:
            print(f"{bcolors.FAIL}\nMissing line in output:")
            print(f"{bcolors.FAIL}\t"+line)
            passed = False

    return passed


def test_cmd(cmd, tlim=1):
    ckt = cmd.split("_")[0] 
    msg = f"cd build/; ./simulator < ../{SCOAP_CMD_TESTS_DIR}/{cmd_test} > /dev/null "
    process = subprocess.Popen(msg, shell=True)
    
    current_time=time.time()
    while time.time() < current_time + tlim and process.poll() is None:
        pass
    
    time_exceeded = False
    if process.poll() is None:
        process.kill()
        # process.terminate()
        print(f"{bcolors.ORG}[TLIM] {cmd} exceeded ({tlim}s){bcolors.ENDC}")
        return "TLIM"
    
    ed_time=time.time()
    golden_output = f"{SCOAP_GOLDEN_DIR}/gold_scoap_{ckt}.txt"
    if (not os.path.exists(golden_output)):
        print(f'{bcolors.ORG}[NA] Golden result does not exists.{bcolors.ENDC}')
        print(f"\tNot found: {golden_output}")
        return "GFAIL"
    
    output = f"{SCOAP_OUTPUT_DIR}/{ckt}_scoap.out"
    if correct_answer(output, golden_output):
        print(f"{bcolors.GRN}[PASS] '{cmd.replace('.cmd','')}' {bcolors.ENDC}")
        return "PASS"
    
    print(f"{bcolors.RED}[FAIL]: '{cmd.replace('.cmd','')}' {bcolors.ENDC}")
    return "FAIL"


if __name__ == '__main__':
    tlim = get_args().tlim
    ckts = os.listdir(CIRCUIT_DIR)
    cmd_test_files = load_cmds(ckts)
    all_res = {"PASS":0, "FAIL":0, "TLIM":0, "GFAIL":0}
 
    for cmd_test in cmd_test_files:
        try:
            res = test_cmd(cmd_test, tlim) 
            all_res[res] += 1
        
        except Exception as e:
            print(f"{bcolors.RED}\n[ERROR] {cmd_test} {bcolors.ENDC}")
            print(e)
            all_res["FAIL"] += 1
            print('_'*50)
 
    _return = print_test_results("SCOAP", all_res) 
    exit(_return) 
