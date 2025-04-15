# Common Flags
CFLAGS = -O3

# Targets
# !!! real line
# all: sequential omp pthread mpi
all: sequential pthread mpi

sequential: sequential.c
	gcc $(CFLAGS) sequential.c -o sequential

omp: omp.c
	gcc $(CFLAGS) -fopenmp omp.c -o omp

pthread: pthread.c
	gcc $(CFLAGS) -pthread pthread.c -o pthread

mpi: mpi.c
	mpicc $(CFLAGS) mpi.c -o mpi

clean:
	rm -f sequential omp pthread mpi *.o

.PHONY: all clean