# Makefile for Game of Life parallel implementations
# COP5570 Programming Assignment 4

# Detect OS
UNAME_S := $(shell uname -s)

# Default compiler
CC = gcc

# Check if we're on macOS and set appropriate compiler flags
ifeq ($(UNAME_S),Darwin)
    # On macOS, try to use Homebrew's GCC if available
    ifneq ($(shell which gcc-13 2>/dev/null),)
        CC = gcc-13
    else ifneq ($(shell which gcc-12 2>/dev/null),)
        CC = gcc-12
    else ifneq ($(shell which gcc-11 2>/dev/null),)
        CC = gcc-11
    endif
    # If we can't find GCC, fall back to clang but warn about OpenMP
    ifeq ($(CC),gcc)
        CC = clang
        $(warning WARNING: Using clang without OpenMP support. OpenMP version will be sequential.)
    endif
endif

# Common flags
CFLAGS = -O3 -DNOOUTPUTFILE

# OpenMP flags (only apply to gcc)
ifneq ($(findstring gcc,$(CC)),)
    OMP_FLAGS = -fopenmp
else
    OMP_FLAGS =
    $(warning OpenMP flags not applied - no proper compiler found)
endif

# Pthread flags
PTHREAD_FLAGS = -pthread

# MPI compiler and flags
MPICC = mpicc
MPI_FLAGS =

# Targets
#all: sequential omp pthread mpi
all: sequential omp pthread


sequential: sequential.c
	$(CC) $(CFLAGS) sequential.c -o sequential

omp: omp.c
	$(CC) $(CFLAGS) $(OMP_FLAGS) omp.c -o omp

pthread: pthread.c
	$(CC) $(CFLAGS) $(PTHREAD_FLAGS) pthread.c -o pthread

mpi: mpi.c
	$(MPICC) $(CFLAGS) $(MPI_FLAGS) mpi.c -o mpi

clean:
	rm -f sequential omp pthread mpi *.o

.PHONY: all clean