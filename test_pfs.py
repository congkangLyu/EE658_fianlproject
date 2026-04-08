# Do NOT change this file.

import os
import glob
import subprocess
import time
from utils import *

PFS_INPUT_TP_DIR = f"{TEST_DIR}/inputs/pfs/tps"
PFS_INPUT_F_DIR = f"{TEST_DIR}/inputs/pfs/faults"
PFS_CMD_TESTS_DIR  = f"{CMD_DIR}/pfs" 
PFS_OUTPUT_DIR = f"{OUTPUT_DIR}/pfs"
PFS_GOLDEN_DIR = f"{GOLDEN_DIR}/pfs"


def load_cmds(ckts):
    """
    cmd file name format:
    <cname>_pfs_tp<tp-count>_f<fault-count>.cmd
    TODO: Maybe check existence of ll related files? 
    """
    cmds = glob.glob(f"{PFS_CMD_TESTS_DIR}/*")
    cmds = [cmd.split("/")[-1] for cmd in cmds if "_pfs_tp" in cmd]
    # print(cmds)
    return cmds


def test_cmd(cmd_test, tlim=1):

    ckt, _, tp, fault = cmd_test.split('_')
    tp = int(tp[2:])
    fault = int(fault[1:-4])

    msg = f"cd build/; ./simulator < ../{PFS_CMD_TESTS_DIR}/{cmd_test} > /dev/null"
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
    
    # gold_<cname>_tp<tp-count>_f<fault-count>.txt
    ed_time = f"{time.time() - current_time:.2f} s"
    golden_result_b = f"{PFS_GOLDEN_DIR}/gold_pfs_{ckt}_tp{tp}_f{fault}.txt"
    if (not os.path.exists(golden_result_b)):
        print(f'{bcolors.ORG}[NA] Golden result does not exists.{bcolors.ENDC}')
        print(f"\tNot found: {golden_result_b}")
        return "GFAIL"
    
    student_answer_b = f"{PFS_OUTPUT_DIR}/{ckt}_tp{tp}_f{fault}.txt"
    if correct_answer(student_answer_b, golden_result_b):
        print(f"{bcolors.GRN}[PASS] '{cmd_test}'\t({ed_time}) {bcolors.ENDC}")
        return "PASS"
    
    print(f"{bcolors.RED}[FAIL]: '{cmd_test}'\t({ed_time}){bcolors.ENDC}")
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
            print('-'*50)
            
    _return = print_test_results("PFS", all_res) 
    exit(_return)
