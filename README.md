[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/B8JnW68L)
[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/kryfDs92)
# Phase-4: Test Pattern Generation 

## Description

Available on *blackboard*.
In addition to new functions, your previous functions also must work properly.

## Build & Run
```
mkdir build && cd build
cmake ..
make

./simulator
```

## Generic Command Usage
```
READ ../ckts/<ckt>.ckt
TPG <dalg/podem> <rtpg_version> fc_report_cktname.txt
```

## RTP-v1 Command Usage
```
READ ../ckts/c17.ckt
TPG <dalg/podem> 1 fc_report_cktname.txt
```

## RTP-v3 Command Usage
```
TPG <dalg/podem> 3 fc_report_cktname.txt
```

## Testing
You can test your code with simple test cases provided for you. 
```
python3 test_main.py --func atpg -alg dalg 
python3 test_main.py --func atpg -alg podem 
python3 test_main.py --func scoap

python3 test_main.py --func rtpg
python3 test_main.py --func rfl
python3 test_main.py --func pfs
python3 test_main.py --func dfs
python3 test_main.py --func logicsim
python3 test_main.py --func all
```

You can modify your test using these arguments: 
```
-tlim : Time-limit set for simulation of each test-case in seconds. 
        Default value is 1 second  
-tb :   Choose the downloadable testbench version. Options are: 
        "EZ" (easy): ckts with <1000 lines, simple simulation setup
        "IM" (intermediate): NA 
        "HD" (hard): NA
-dnC :  If set, the script will neither remove the build directories nor compile/build. 
-dnD :  If set, the script will not download any testbenches 
-dnR :  If set, the script will not remove the results after simulation is concluded. 
```

## Checking Memory Leak Using Valgrind
As you allocate memory dynamically and do not deallocate it correctly, it results in memory leak. This means when the program is finished, there is still some allocated memory in the RAM while it is inaccessible and you cannot free it. To detect this, here's a tool you can use.


First, install [Valgrind](https://valgrind.org/). Here is the script for Ubuntu:
```
sudo apt-get install valgrind
```

To use it, when running program, use this command:
```
valgrind --leak-check=yes ./simulator
```

Here is a quick guide to how to read leak logs:

* if you see something like below in the log, it means that no memory leak has occurred:

    ```
    ==96617== HEAP SUMMARY:
    ==96617==     in use at exit: 0 bytes in 0 blocks
    ==96617==   total heap usage: 552 allocs, 552 frees, 123,266 bytes allocated
    ==96617== 
    ==96617== All heap blocks were freed -- no leaks are possible
    ```
* Otherwise, you will see a summary like this at the end of the log:
    ```
    ==92649== HEAP SUMMARY:
    ==92649==     in use at exit: 2,820 bytes in 70 blocks
    ==92649==   total heap usage: 181 allocs, 111 frees, 108,768 bytes allocated
    ==92649== 
    ==92649== 92 bytes in 1 blocks are definitely lost in loss record 4 of 8
    ==92649==    at 0x4841888: malloc (vg_replace_malloc.c:393)
    ==92649==    by 0x12F042: cread() (readckt.cpp:350)
    ==92649==    by 0x136501: main (main.cpp:21)
    ==92649== 
    ==92649== LEAK SUMMARY:
    ==92649==    definitely lost: 92 bytes in 1 blocks
    ==92649==    indirectly lost: 0 bytes in 0 blocks
    ==92649==      possibly lost: 0 bytes in 0 blocks
    ==92649==    still reachable: 2,728 bytes in 69 blocks
    ==92649==         suppressed: 0 bytes in 0 blocks
    ```
**SUMMARY**: 


`definitely lost`: The allocated memory has no longer a pointer.

`indirectly lost`: The pointer to the place where the variable's pointer was saved, is lost.

`possible lost`: The memory is not freed and Valgrind cannot be sure whether there is a pointer or not.

`still reachable`: Possibly a global variable is not freed but still has a pointer.

`suppressed lost`: You can safely ignore this one.

<br>

[Read more here.](https://web.stanford.edu/class/archive/cs/cs107/cs107.1174/guide_valgrind.html#:~:text=That%20block%20was%20allocated%20by,since%20lost%20track%20of%20it.)
