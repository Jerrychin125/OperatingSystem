gcc main.c -Wall -Wno-unused-result -o main
gcc -c -fPIC multilevelBF.c -o multilevelBF.o
gcc multilevelBF.o -shared -o multilevelBF.so
LD_PRELOAD=./multilevelBF.so ./main

# Clean feature
rm -f multilevelBF.o multilevelBF.so main *.log
