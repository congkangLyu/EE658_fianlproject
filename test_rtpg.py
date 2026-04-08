# Do NOT change this file.

import os
import glob
import subprocess
import time
from utils import *

OUTPUT_DIR = f"{TEST_DIR}/outputs"
RTPG_CMD_TESTS_DIR  = f"{CMD_DIR}/rtpg" 
RTPG_OUTPUT_DIR  = f"{OUTPUT_DIR}/rtpg" 


def check_rtpg(answer_fname):
    # Does not check tp-count and primary inputs
    answer_result = open(answer_fname,'r').readlines()
    written_nodes = answer_result[0].split(',')

    circuit_nodes = written_nodes.copy()

    written_nodes.sort()
    circuit_nodes.sort()
    
    if written_nodes!=circuit_nodes:
        return False

    for line in answer_result[1:]:
        tp = line.split(',')

        if len(tp) != len(circuit_nodes):
            return False
        
        if '_t_' in answer_fname:
            for t in tp:
                if t.strip() not in ['0','1','x']:
                    return False
        
        elif '_b_' in answer_fname:
            for t in tp:
                if t.strip() not in ['0','1']:
                    return False
    
    return True


def load_cmds(ckts):
    """
    cmd file name format:
    <cname>_rtpg.cmd
    """
    cmds = glob.glob(f"{RTPG_CMD_TESTS_DIR}/*")
    cmds = [cmd.split("/")[-1] for cmd in cmds]  
    return cmds


def test_cmd(cmd_test, tlim=1):
    ckt, _, tp = cmd_test.split("_")
    tp = int(tp[2:-4])

    msg = f"cd build/; ./simulator < ../{RTPG_CMD_TESTS_DIR}/{cmd_test} > /dev/null "
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
    
    ed_time=time.time()

    res_b = check_rtpg(f'{RTPG_OUTPUT_DIR}/{ckt}_b_tp{tp}.tp')
    res_t = check_rtpg(f'{RTPG_OUTPUT_DIR}/{ckt}_t_tp{tp}.tp')

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
            # print('Testing on ' + cmd_test)
            res = test_cmd(cmd_test, tlim)
            all_res[res] += 1

        except Exception as e:
            print(f"{bcolors.RED}\n[ERROR] {cmd_test} {bcolors.ENDC}")
            print(e)
            all_res["FAIL"] += 1
            print('-'*50)

    _return = print_test_results("RTPG", all_res) 
    exit(_return)
