import argparse
import os

class bcolors:
    GRN = '\033[92m'
    RED = '\033[91m'
    ORG = '\033[93m'
    ENDC = '\033[0m'
    BGD  = '\033[44m'
    BOLD = '\033[1m'

X_VALUE = 'x'

TEST_DIR = "auto-tests-phase3"
TPI_DIR = f"{TEST_DIR}/tpi"
CIRCUIT_DIR = f"{TEST_DIR}/ckts"
CMD_DIR = f"{TEST_DIR}/cmds"

OUTPUT_DIR = f"{TEST_DIR}/outputs"
GOLDEN_DIR = f"{TEST_DIR}/golden_results"
FDICT_CSV_DIR = f"{GOLDEN_DIR}/fd_csv"

WEB_ADDR = "https://sportlab.usc.edu/~msabrishami/files"
WEB_DIR = "ee658"
 
def get_args():
    parser = argparse.ArgumentParser(description ='Which function to test')
    parser.add_argument("-tlim", type=float, default=5,
            help="Time limit in seconds")
    parser.add_argument("-alg", type=str, 
            help="ATPG algorithm (DALG/PODEM)")

    return parser.parse_args()


def pars_args(): 

    parser = argparse.ArgumentParser(description ='Which function to test')
    parser.add_argument('-f', '--func', metavar='function', 
            choices={'rtpg', 'rfl', 'logicsim', 'dfs', 'pfs', 
                'atpg', 'scoap', 'all'}, required=True) 
    parser.add_argument('-tb', '--tb', metavar='testbench', 
            choices={'EZ', 'IM', 'HD'}, default="EZ") 
    parser.add_argument("-dnC", action="store_true", 
            help="do NOT compile")
    parser.add_argument("-dnR", action="store_true", 
            help="do NOT remove the files that are downloaded or generated")
    parser.add_argument("-dnD", action="store_true", 
            help="do NOT download the auto-test files")
    parser.add_argument("-tlim", type=float, default=1,
            help="Time limit in seconds")
    parser.add_argument("-alg", type=str, default="dalg",
            help="ATPG algorithm (DALG/PODEM)")
    
    args = parser.parse_args()
    
    if args.func == "all": 
        args.func = ['rtpg', 'rfl', 'logicsim', 'dfs', 'pfs', 
                ("atpg","dalg"), ("atpg","podem"), "scoap"] 
    elif args.func == "atpg": 
        args.func = [("atpg", args.alg)]
    else: args.func = [args.func]

    return args


def prepare_env(args):
    """download tests/create folders"""
    if args.tb == "EZ": ZIP_NAME = "auto-tests-phase3-v2401-EZ.zip"
    # elif args.tb == "IM": ZIP_NAME = "auto-tests-phase3-v2401-IM.zip"
    # elif args.tb == "HD": ZIP_NAME = "auto-tests-phase3-v2401-HD.zip"
 
    os.system(f"rm -rf {TEST_DIR}")
    os.system(f"mkdir {TEST_DIR}")
    # os.system(f"rm -rf {ZIP_NAME}")

    # if os.path.exists("/etc/hostname") and \
    #         ("viterbi-scf" in open("/etc/hostname").readline().strip()):
    #     os.system(f"cp /home/{WEB_DIR}/{ZIP_NAME} ./")
    # else:
    #     os.system(f"wget {WEB_ADDR}/{WEB_DIR}/{ZIP_NAME}")
    
    os.system(f"unzip {ZIP_NAME} -d ./{TEST_DIR} > /dev/null") 
    # os.system(f"rm -rf {ZIP_NAME}")


def print_test_title(tname, count=35):
    tname = f"Testing {tname}"
    print("\n"+ "="*count)
    sz1 = int((count - (len(tname)+4))/2)
    sz2 = count - (sz1 + len(tname) + 4)
    print("="*sz1 + f"  {tname}  " + "="*sz2)
    print("="*count)


def print_bg(msg, col, szL, szT, bg=bcolors.BGD):
    szR = szT - (len(msg) + szL)
    print(f'{bg}{" "*szL}{col}{msg}{" "*szR}{bcolors.ENDC}')


def print_test_results(tname, res, maxL=35):
    tot_cmd = res["PASS"] + res["FAIL"] + res["TLIM"] 
    if res["TLIM"] == 0 and res["FAIL"] == 0:
        # print(f"{bcolors.BG_YELLOW}{bcolors.GRN}{bcolors.ENDC}", end="")
        msg = f" ALL-PASSED >> {tname} "
        if len(msg)%2==0: maxL += 1
        hL = int((maxL - len(msg))/2) - 1 
        mL = len(msg)
        print(f'{bcolors.BGD}{bcolors.GRN}{" "*maxL}{bcolors.ENDC}')
        print(f'{bcolors.BGD}{bcolors.GRN}{" "*hL}|\33[53m{msg}{bcolors.ENDC}', end="")
        print(f'{bcolors.BGD}{bcolors.GRN}|{" "*hL}{bcolors.ENDC}')
        print(f'{bcolors.BGD}{bcolors.GRN}{" "*hL} \33[53m{" "*mL}{bcolors.ENDC}',end="")
        print(f'{bcolors.BGD}{bcolors.GRN}{" "*(hL+1)}{bcolors.ENDC}')
        return 0
    else:
        print_bg("", bcolors.GRN, 10, maxL)
        print_bg(tname, bcolors.BOLD, 10, maxL)
        print_bg(f'PASS: {res["PASS"]}/{tot_cmd}', bcolors.GRN, 10, maxL)
        if res["TLIM"] > 0:
            print_bg(f'TLIM: {res["TLIM"]}/{tot_cmd}', bcolors.ORG, 10, maxL) 
        if res["FAIL"] > 0:
            print_bg(f'FAIL: {res["FAIL"]}/{tot_cmd}', bcolors.RED, 10, maxL) 
        print_bg("", bcolors.GRN, 10, maxL)
        return 1
    return 1


def correct_answer(answer, golden):
    passed=True
    golden_result = open(golden,'r').readlines()
    answer_result = open(answer,'r').readlines()
    
    for idx, line in enumerate(answer_result):
        if line and line not in golden_result:
            if passed:
                print(f"{bcolors.RED}\n[Oops!] Missmatch report:")
                print(f"\tOutput:\t{answer}\n\tGolden:\t{golden}{bcolors.ENDC}")

            print(f"{bcolors.RED}\t-- Wrong line {idx} >>\t{line.strip()}{bcolors.ENDC}")
            passed = False

    for line in golden_result:
        if line and line not in answer_result:
            if passed:
                print(f"{bcolors.RED}\n[Oops!] Missmatch report:")
                print(f"\tOutput:\t{answer}\n\tGolden:\t{golden}{bcolors.ENDC}")

            print(f"{bcolors.RED}\t-- Missing line >>\t{line.strip()}{bcolors.ENDC}")
            passed = False

    return passed


def gen_all_tps(tp):
    from collections import deque

    all_tps = deque()
    all_tps.append(tp)

    while True:
        front_tp = all_tps.popleft()
        if not X_VALUE in front_tp:
            all_tps.append(front_tp)
            break
        
        first_x = None
        for t in range(len(front_tp)):
            if front_tp[t] == X_VALUE:
                first_x = t
                break
        
        #substitute zero and one
        if first_x is not None:
            tp_copy = front_tp.copy()
            tp_copy[first_x] = 1
            all_tps.append(tp_copy)

            front_tp[first_x] = 0
            all_tps.append(front_tp)
    
    return list(all_tps)

def tp_to_int(tp):
    return int(tp.replace(',','').replace('\n',''), 2)
