# Do NOT change this file.

import os
import glob
import subprocess
import time
from utils import *

LOGICSIM_INPUT_DIR = f"{TEST_DIR}/inputs/logicsim"
LOGICSIM_CMD_TESTS_DIR  = f"{CMD_DIR}/logicsim" 
LOGICSIM_OUTPUT_DIR = f"{OUTPUT_DIR}/logicsim"
LOGICSIM_GOLDEN_DIR = f"{GOLDEN_DIR}/logicsim"

def load_cmds(ckts):
    """
    cmd file name format:
    <cname>_lsim_b|t_tp<tp-count>.cmd
    TODO: Maybe check existence of ll related files? 
    """
    cmds = glob.glob(f"{LOGICSIM_CMD_TESTS_DIR}/*")
    cmds = [cmd.split("/")[-1] for cmd in cmds if "_lsim_tp" in cmd]
    return cmds


def test_cmd(cmd_test, tlim=1):

    ckt, _, tp = cmd_test.split('_')
    tp = int(tp[2:-4])

    msg = f"cd build/; ./simulator < ../{LOGICSIM_CMD_TESTS_DIR}/{cmd_test} > /dev/null "
    process = subprocess.Popen(msg, shell=True)
    
    current_time=time.time()
    while time.time() < current_time + tlim and process.poll() is None:
        pass
    
    time_exceeded = False
    if process.poll() is None:
        process.kill()
        # process.terminate()
        print(f"{bcolors.ORG}[TLIM] {cmd_test} exceeded ({tlim}s){bcolors.ENDC}")
        # print(f"test '{ckt.replace('.txt','')}'","exceeded time limit")
        return "TLIM"
        # raise Exception('Time limit exceeded (probably stuck in loop).')
    
    ed_time=time.time()
    # golden result format: gold_lsim_<cname>_<mode>_tp<tp-count>.txt
    golden_result_b = f"{LOGICSIM_GOLDEN_DIR}/gold_lsim_{ckt}_b_tp{tp}.txt"
    golden_result_t = f"{LOGICSIM_GOLDEN_DIR}/gold_lsim_{ckt}_t_tp{tp}.txt"
 
    if not os.path.exists(golden_result_b):
        print(f'{bcolors.ORG}[NA] Golden result does not exists.{bcolors.ENDC}')
        print(f"\tNot found: {golden_result_b}")
        return "GFAIL"
    if not os.path.exists(golden_result_t):
        print(f'{bcolors.ORG}[NA] Golden result does not exists.{bcolors.ENDC}')
        print(f"\tNot found: {golden_result_t}")
        return "GFAIL"
    
    # output format: <cname>_<mode>_tp<tp-count>.txt
    student_answer_b = f"{LOGICSIM_OUTPUT_DIR}/{ckt}_b_tp{tp}.txt"
    student_answer_t = f"{LOGICSIM_OUTPUT_DIR}/{ckt}_t_tp{tp}.txt"
    res_b = correct_answer(student_answer_b, golden_result_b)
    res_t = correct_answer(student_answer_t, golden_result_t) 
    if res_b and res_t:
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
            # print('Testing logicsim on ' + cmd_test)
            res = test_cmd(cmd_test, tlim)
            all_res[res] += 1

        except Exception as e:
            print(f"{bcolors.RED}\n[ERROR] {cmd_test} {bcolors.ENDC}")
            print(e)
            all_res["FAIL"] += 1
            print('_'*50)

    _return = print_test_results("LOGICSIM", all_res) 
    exit(_return)
