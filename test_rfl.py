# Do NOT change this file.

import os
import glob
import subprocess
import time
from utils import *

RFL_CMD_TESTS_DIR  = f"{CMD_DIR}/rfl/" 
RFL_OUTPUT_DIR = f"{OUTPUT_DIR}/rfl/"
RFL_GOLDEN_DIR = f"{GOLDEN_DIR}/rfl/"


def load_cmds(ckts):
    """
    cmd file name format:
    <cname>_rfl.cmd
    TODO: Maybe check existence of ll related files? 
    """
    cmds = glob.glob(f"{RFL_CMD_TESTS_DIR}/*")
    cmds = [cmd.split("/")[-1] for cmd in cmds if "_rfl.cmd" in cmd]
    return cmds


def test_cmd(cmd_test, tlim=1):

    ckt = cmd_test.split('_')[0]
    msg = f"cd build/; ./simulator < ../{RFL_CMD_TESTS_DIR}{cmd_test} > /dev/null "
    process = subprocess.Popen(msg, shell=True)
    
    current_time=time.time()
    while time.time() < current_time + tlim and process.poll() is None:
        pass
    
    time_exceeded = False
    if process.poll() is None:
        process.kill()
        # process.terminate()
        print(f"{bcolors.ORG}[TLIM] {cmd_test} exceeded ({tlim}s){bcolors.ENDC}")
        return "TLIM"
    
    # gold_rfl_<cname>.txt
    ed_time=time.time()
    golden_result_b = f"{RFL_GOLDEN_DIR}/gold_rfl_{ckt}.txt"
    if (not os.path.exists(golden_result_b)):
        print(f'{bcolors.ORG}[NA] Golden result does not exists.{bcolors.ENDC}')
        print(f"\tNot found: {golden_result_b}")
        return "GFAIL"
    
    student_answer_b = f"{RFL_OUTPUT_DIR}/{ckt}_rfl.txt"
    if correct_answer(student_answer_b, golden_result_b):
        print(f"{bcolors.GRN}[PASS] '{cmd_test.replace('.txt','')}' {bcolors.ENDC}")
        return "PASS"
    
    print(f"{bcolors.RED}[FAIL]: '{cmd_test.replace('.txt','')}' {bcolors.ENDC}")
    return "FAIL"




if __name__ == '__main__':
    tlim = get_args().tlim
    ckts = os.listdir(CIRCUIT_DIR)
    cmd_test_files = load_cmds(ckts)
    all_res = {"PASS":0, "FAIL":0, "TLIM":0, "GFAIL":0}
    
    for cmd_test in cmd_test_files:
        try:
            # print('Testing on ' + cmd_test)
            res = test_cmd(cmd_test, tlim)
            all_res[res] += 1
    
        except Exception as e:
            print(f"{bcolors.RED}\n[ERROR] {cmd_test} {bcolors.ENDC}")
            print(e)
            all_res["FAIL"] += 1
            print('_'*50)
 
    _return = print_test_results("RFL", all_res) 
    exit(_return)
