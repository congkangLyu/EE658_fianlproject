
import pandas as pd
import utils

DALG_CMD_TESTS_DIR  = f"{utils.CMD_DIR}/dalg" 
DALG_OUTPUT_DIR  = f"{utils.OUTPUT_DIR}/dalg" 
DALG_GOLDEN_DIR  = f"{utils.GOLDEN_DIR}/dalg" 
PODEM_CMD_TESTS_DIR  = f"{utils.CMD_DIR}/podem" 
PODEM_OUTPUT_DIR  = f"{utils.OUTPUT_DIR}/podem" 
PODEM_GOLDEN_DIR  = f"{utils.GOLDEN_DIR}/podem" 


ATPG_CMD_DIR = {"dalg": DALG_CMD_TESTS_DIR, "podem": PODEM_CMD_TESTS_DIR}
ATPG_OUTPUT_DIR = {"dalg": DALG_OUTPUT_DIR, "podem": PODEM_OUTPUT_DIR}

def path_cmd(ckt, alg, fault_node, fault_val):
    cmd_dir = ATPG_CMD_DIR[alg]
    cmd_fname = f"{cmd_dir}/{ckt}_{alg}_{fault_node}_{fault_val}.cmd"
    return cmd_fname

def path_output(ckt, alg, fault_node, fault_val):
    output_dir = ATPG_OUTPUT_DIR[alg]
    output_fname = f"{output_dir}/{ckt}_{alg}_{fault_node}_{fault_val}.out"
    return output_fname 

def gen_cmd_atpg_single(ckt, alg, fault_node, fault_val):
    cmd_dir = ATPG_CMD_DIR[alg]
    cmd_fname = path_cmd(ckt, alg, fault_node, fault_val) 
    output_fname = path_output(ckt, alg, fault_node, fault_val) 
    with open(cmd_fname, 'w') as outfile:
        outfile.write(f"READ ../{utils.CIRCUIT_DIR}/{ckt}.ckt\n")
        outfile.write(f"{alg.upper()} {fault_node} {fault_val} ../{output_fname}\n")
        outfile.write("QUIT")
    return cmd_fname


def gen_cmd_atpg_minickt(ckt, alg, faults=None):
    if not faults:
        faults = read_fdict(ckt) 
    cmds = []
    for fault in faults: 
        fault_node, fault_val = fault.split("@")
        cmds.append(gen_cmd_atpg_single(ckt, alg, fault_node, fault_val))
    return cmds

def read_fdict(ckt):
    """ Bug handling: removing possible spaces in csv file """ 
    df = pd.read_csv(f'{utils.FDICT_CSV_DIR}/{ckt}_fd.csv', index_col=0)
    df.columns = [col.strip().replace(" ", "") for col in df.columns]
    for col in df.columns:
        df[col] = df[col].apply(lambda x: x.strip() if isinstance(x, str) else x)
        df[col] = df[col].apply(lambda x: int(x) if x=="1" else x)

    return df
