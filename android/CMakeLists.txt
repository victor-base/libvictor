cmake_minimum_required(VERSION 3.6)
project(libvictor_android C)

add_library(victor SHARED
    ../src/index.c
    ../src/index_flat.c
    ../src/vmath.c
    ../src/mem.c
    ../src/method.c
    ../src/vector.c
    ../src/iflat_utils.c
    ../src/heap.c
    ../src/vtime.c
    ../src/map.c
    ../src/index_nsw.c
    ../src/store.c
    ../src/file.c
    ../src/asort.c
    ../src/graph.c
    ../src/index_hnsw.c
    ../src/error.c
)

target_include_directories(victor PRIVATE
    ../src
)

target_link_libraries(victor m)