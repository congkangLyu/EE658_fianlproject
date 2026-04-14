cmake_minimum_required(VERSION 2.8)
project(Phase4)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(simulator
    main.cpp
    globals.cpp
    readckt.cpp
    help.cpp
    quit.cpp
    lev.cpp
    logicsim.cpp
    rtpg.cpp
    rfl.cpp
    dfs.cpp
    pfs.cpp
    tpfc.cpp
    scoap.cpp
    dalg.cpp
    podem.cpp
    tpg.cpp
    utils.cpp
)