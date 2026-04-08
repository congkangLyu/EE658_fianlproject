# Do NOT change this file.

import os
import glob
import subprocess
import time
import pandas as pd
from utils import *
import utils_atpg 


def load_cmds(ckts, alg):
    """
    cmd file name format:
    <cname>_<alg>_<fault_node>_<fault_value>.cmd
    """
    cmds = glob.glob(f"{utils_atpg.ATPG_CMD_DIR[alg]}/*")
    cmds = [cmd.split("/")[-1] for cmd in cmds]
    return cmds


def test_cmd_minickt(ckt, alg, tlim=1):
    """ For all faults of the netlist ckt: 
    Generate cmds for all faults 
    Run the simulator for all these faults 
    Check if the result is correct 
    """
    print(f"Testing {alg.upper()} on all faults of {ckt}")
    fdict = utils_atpg.read_fdict(ckt) 
    faults = fdict.columns.tolist()
    ckt_res = {"PASS":0, "FAIL":0, "TLIM":0, "GFAIL":0}
    
    # generate cmds for all faults of ckt 
    cmds = utils_atpg.gen_cmd_atpg_minickt(ckt, alg, faults)

    # run simulator and compare results for each fault 
    for cmd in cmds: 
        try:
            res = test_cmd(cmd, alg, fdict, tlim)
            ckt_res[res] += 1
        except Exception as e:
            print(f"{bcolors.RED}\n[ERROR] {cmd} {bcolors.ENDC}")
            print(e)
            ckt_res["FAIL"] += 1
            print('-'*50)

    print_test_results(f"{alg.upper()} {ckt}", ckt_res) 
    return ckt_res 

def test_cmd(cmd, alg, df, tlim=1):
    cmd = cmd.split("/")[-1].replace(".cmd", "")
    ckt, _, fault_node, fault_val = cmd.split("_")
    cmd_path = utils_atpg.path_cmd(ckt, alg, fault_node, fault_val)
    output_path = utils_atpg.path_output(ckt, alg, fault_node, fault_val)

    msg = f"cd build/; ./simulator < ../{cmd_path} > /dev/null"
    process = subprocess.Popen(msg, shell=True)
    
    current_time=time.time()
    while time.time() < current_time + tlim and process.poll() is None:
        pass
    
    time_exceeded = False
    if process.poll() is None:
        process.kill()
        # process.terminate()
        print(f"{bcolors.ORG}[TLIM] {cmd} exceeded ({tlim} s){bcolors.ENDC}")
        return "TLIM"
    
    ed_time = f"{time.time() - current_time:.2f} s"
    if correct_answer(output_path, df):
        print(f"{bcolors.GRN}[PASS] '{cmd}'\t({ed_time}) {bcolors.ENDC}")
        return "PASS"
    
    print(f"{bcolors.RED}[FAIL]: '{cmd}'\t({ed_time}){bcolors.ENDC}")
    return "FAIL"


def correct_answer(output_fname, golden_df):
    """Using fd_csv"""
    ckt, _, fault_node, fault_val = output_fname.split("/")[-1].strip(".out").split("_")
    fault = f"{fault_node}@{fault_val}"

    answer_result = open(output_fname,'r').readlines()
    
    if len(answer_result)>1:
        answer_result = answer_result[1].strip()
        stu_tp = list(answer_result.split(','))
        all_stu_tps = gen_all_tps(stu_tp)
    else:
        all_stu_tps = []

    if len(all_stu_tps) == 0: 
        if golden_df[fault].isna().all(): return True
        print(f'{bcolors.RED} No TPs generated while {fault} is detectable. {bcolors.ENDC}')
        return False
        
    passed=True
    for st_tp in all_stu_tps:
        tp_int = tp_to_int(",".join([str(s) for s in st_tp]))
        if not golden_df[fault].loc[tp_int] == 1.0:
            passed=False
            print(f'{bcolors.RED}tp {st_tp} does not detect {fault}.{bcolors.ENDC}')

    return passed


if __name__ == '__main__':
    tlim = get_args().tlim
    alg = get_args().alg
    if alg not in ["dalg", "podem"]:
        raise ValueError(f"Algorithm {alg} is not accepted. ")
    ckts = os.listdir(CIRCUIT_DIR)
    cnames = [x.replace(".ckt", "") for x in ckts]

    all_res = {"PASS":0, "FAIL":0, "TLIM":0, "GFAIL":0}
    for ckt in cnames:
        ckt_res = test_cmd_minickt(ckt, alg, tlim)
        all_res = {key: all_res[key] + ckt_res[key] for key in all_res.keys()}
    print('-'*50)
    
    _return = print_test_results(f"ATPG {alg}", all_res) 
    exit(_return) 
