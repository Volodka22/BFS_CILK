# BFS_CILK
## Результаты
par bfs: 84868


seq bfs: 231184


acceleration: 2.72404x

## Build and Run
clang++ -fopencilk -O3 bfs.cpp -o bfs

CILK_NWORKERS=4 ./bfs
