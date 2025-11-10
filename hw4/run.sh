gcc main.c -o main
gcc -c -fPIC multilevelBF.c -o multilevelBF.o
gcc multilevelBF.o -shared -o multilevelBF.so
LD_PRELOAD=./multilevelBF.so ./main