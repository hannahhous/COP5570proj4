/*
 * MPI Implementation of Game of Life
 * Based on the sequential version by Dr. Xin Yuan for the COP5570 class
 *
 * To compile:
 *    mpicc -O3 -DNOOUTPUTFILE proj4_mpi.c -o proj4_mpi
 *
 * To compile to test correctness (the program will output file final_world000.txt):
 *    mpicc -O3 proj4_mpi.c -o proj4_mpi
 *
 * To run:
 *    mpirun -np <num_processes> ./proj4_mpi <width> <height>
 */

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>

#ifdef NOOUTPUTFILE
#define NOOUTPUTFILE 1
#else
#define NOOUTPUTFILE 0
#endif

#define MAX_N 8192

#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL 0
#endif

char local_w[MAX_N][MAX_N];  // Local world with ghost rows
char neww[MAX_N][MAX_N];    // Next generation grid

int w_Y;  // Global variable to match sequential version

// Initialize local portion of the world - EXACTLY matching sequential code
void init_local_world(int w_X, int w_Y, int local_w_Y, int start_row)
{
    int i, j;

    // Initialize local portion to 0 - exactly as sequential version does
    for (i = 0; i < w_X; i++) {
        for (j = 0; j < local_w_Y + 2; j++) {  // +2 for ghost rows
            local_w[j][i] = 0;
        }
    }

    // Exactly match sequential initialization pattern
    // First diagonal (i == j)
    for (i = 0; i < w_X && i < w_Y; i++) {
        if (i >= start_row && i < start_row + local_w_Y) {
            local_w[(i - start_row) + 1][i] = 1;  // +1 for ghost row offset
        }
    }

    // Second diagonal (i == w_Y - 1 - j)
    for (i = 0; i < w_Y && i < w_X; i++) {
        int j = w_Y - 1 - i;  // This matches sequential code exactly
        if (j >= start_row && j < start_row + local_w_Y) {
            local_w[(j - start_row) + 1][i] = 1;  // +1 for ghost row offset
        }
    }
}

void test_init_local_world(int local_w_Y, int start_row)
{
    int i, j;
    int w_X = 4;
    int w_Y = 6;

    // Initialize local portion to 0
    for (i = 0; i < w_X; i++) {
        for (j = 0; j < local_w_Y + 2; j++) {
            local_w[j][i] = 0;
        }
    }

    // Set the specific pattern from sequential test_init()
    // w[0][3] = 1;
    if (0 >= start_row && 0 < start_row + local_w_Y) local_w[(0 - start_row) + 1][3] = 1;

    // w[1][3] = 1;
    if (1 >= start_row && 1 < start_row + local_w_Y) local_w[(1 - start_row) + 1][3] = 1;

    // w[2][1] = 1;
    if (2 >= start_row && 2 < start_row + local_w_Y) local_w[(2 - start_row) + 1][1] = 1;

    // w[3][0] = w[3][1] = w[3][2] = 1;
    if (3 >= start_row && 3 < start_row + local_w_Y) {
        local_w[(3 - start_row) + 1][0] = 1;
        local_w[(3 - start_row) + 1][1] = 1;
        local_w[(3 - start_row) + 1][2] = 1;
    }

    // w[4][1] = 1;
    if (4 >= start_row && 4 < start_row + local_w_Y) local_w[(4 - start_row) + 1][1] = 1;

    // w[5][1] = 1;
    if (5 >= start_row && 5 < start_row + local_w_Y) local_w[(5 - start_row) + 1][1] = 1;
}

// Exchange ghost rows with neighboring processes - using working pattern from debug version
void exchange_ghost_rows(int w_X, int local_w_Y, int rank, int size)
{
    MPI_Status status;

    // Simple and safe exchange pattern to avoid deadlocks
    // First handle even ranks sending, odd ranks receiving
    if (rank % 2 == 0) {
        // Even ranks send first
        if (rank < size - 1) {
            MPI_Send(&local_w[local_w_Y][0], w_X, MPI_CHAR, rank + 1, 0, MPI_COMM_WORLD);
        }
    } else {
        // Odd ranks receive first
        if (rank > 0) {
            MPI_Recv(&local_w[0][0], w_X, MPI_CHAR, rank - 1, 0, MPI_COMM_WORLD, &status);
        }
    }

    // Barrier to ensure all even-to-odd communications complete
    MPI_Barrier(MPI_COMM_WORLD);

    // Now handle odd ranks sending, even ranks receiving
    if (rank % 2 == 1) {
        // Odd ranks send
        if (rank < size - 1) {
            MPI_Send(&local_w[local_w_Y][0], w_X, MPI_CHAR, rank + 1, 1, MPI_COMM_WORLD);
        }
    } else {
        // Even ranks receive
        if (rank > 0) {
            MPI_Recv(&local_w[0][0], w_X, MPI_CHAR, rank - 1, 1, MPI_COMM_WORLD, &status);
        }
    }

    // Another barrier
    MPI_Barrier(MPI_COMM_WORLD);

    // Second round: first even-to-odd for second boundary
    if (rank % 2 == 0) {
        if (rank > 0) {
            MPI_Send(&local_w[1][0], w_X, MPI_CHAR, rank - 1, 2, MPI_COMM_WORLD);
        }
    } else {
        if (rank < size - 1) {
            MPI_Recv(&local_w[local_w_Y + 1][0], w_X, MPI_CHAR, rank + 1, 2, MPI_COMM_WORLD, &status);
        }
    }

    // Final barrier
    MPI_Barrier(MPI_COMM_WORLD);

    // Last round: odd-to-even for second boundary
    if (rank % 2 == 1) {
        if (rank > 0) {
            MPI_Send(&local_w[1][0], w_X, MPI_CHAR, rank - 1, 3, MPI_COMM_WORLD);
        }
    } else {
        if (rank < size - 1) {
            MPI_Recv(&local_w[local_w_Y + 1][0], w_X, MPI_CHAR, rank + 1, 3, MPI_COMM_WORLD, &status);
        }
    }
}

// Count neighbors for a cell in the local domain
// CRUCIAL: Must exactly match the logic in sequential version
int local_neighborcount(int x, int y, int w_X, int local_w_Y, int start_row)
{
    int count = 0;
    int global_y = start_row + (y - 1);  // Convert to global coordinates (subtract 1 for ghost row)

    // Out of bounds check - should never happen but match sequential logic
    if ((x < 0) || (x >= w_X)) {
        printf("neighborcount: (%d %d) out of bound (0..%d, 0..%d).\n", x, global_y, w_X, w_Y);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    if ((global_y < 0) || (global_y >= w_Y)) {
        printf("neighborcount: (%d %d) out of bound (0..%d, 0..%d).\n", x, global_y, w_X, w_Y);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // Following EXACT structure of sequential code
    if (x == 0) {
        if (global_y == 0) {  // Top-left corner
            count = local_w[y][x+1] + local_w[y+1][x] + local_w[y+1][x+1];
        } else if (global_y == w_Y-1) {  // Bottom-left corner
            count = local_w[y][x+1] + local_w[y-1][x] + local_w[y-1][x+1];
        } else {  // Left edge (not corner)
            count = local_w[y-1][x] + local_w[y+1][x] + local_w[y-1][x+1] + local_w[y][x+1] + local_w[y+1][x+1];
        }
    } else if (x == w_X-1) {
        if (global_y == 0) {  // Top-right corner
            count = local_w[y][x-1] + local_w[y+1][x-1] + local_w[y+1][x];
        } else if (global_y == w_Y-1) {  // Bottom-right corner
            count = local_w[y][x-1] + local_w[y-1][x] + local_w[y-1][x-1];
        } else {  // Right edge (not corner)
            count = local_w[y-1][x] + local_w[y+1][x] + local_w[y-1][x-1] + local_w[y][x-1] + local_w[y+1][x-1];
        }
    } else {  // Not on left or right edge
        if (global_y == 0) {  // Top edge (not corner)
            count = local_w[y][x-1] + local_w[y][x+1] + local_w[y+1][x-1] + local_w[y+1][x] + local_w[y+1][x+1];
        } else if (global_y == w_Y-1) {  // Bottom edge (not corner)
            count = local_w[y][x-1] + local_w[y][x+1] + local_w[y-1][x-1] + local_w[y-1][x] + local_w[y-1][x+1];
        } else {  // Middle of grid
            count = local_w[y-1][x-1] + local_w[y][x-1] + local_w[y+1][x-1] + local_w[y-1][x] + local_w[y+1][x]
                  + local_w[y-1][x+1] + local_w[y][x+1] + local_w[y+1][x+1];
        }
    }

    return count;
}

// Print local world for debugging
void print_local_world(int w_X, int local_w_Y, int rank)
{
    if (DEBUG_LEVEL <= 10) return;

    int i, j;

    printf("Process %d local world:\n", rank);
    for (j = 1; j <= local_w_Y; j++) {
        for (i = 0; i < w_X; i++) {
            printf("%d", (int)local_w[j][i]);
        }
        printf("\n");
    }
}

int main(int argc, char *argv[])
{
    int rank, size;
    int w_X, w_Y;
    int local_w_Y, start_row;
    int iter = 0;
    int c, local_count, global_count, init_count;
    double start_time, end_time;

    // Initialize MPI
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    // Parse command line arguments
    if (argc == 1) {
        if (rank == 0) {
            printf("Usage: mpirun -np <num_processes> ./proj4_mpi w_X w_Y\n");
        }
        MPI_Finalize();
        return 1;
    } else if (argc == 2) {
        // Test init with small world
        w_X = 4;
        w_Y = 6;

        // Calculate local domain division
        local_w_Y = w_Y / size;
        // Handle remainder rows (assign to last process)
        if (rank == size - 1) {
            local_w_Y += w_Y % size;
        }
        start_row = rank * (w_Y / size);

        // Initialize the test pattern
        if (rank == 0 && DEBUG_LEVEL > 0) {
            printf("Test on a small 4x6 world\n");
        }
        test_init_local_world(local_w_Y, start_row);
    } else {
        // Normal initialization with dimensions from command line
        w_X = atoi(argv[1]);
        w_Y = atoi(argv[2]);

        // Calculate local domain division
        local_w_Y = w_Y / size;
        // Handle remainder rows (assign to last process)
        if (rank == size - 1) {
            local_w_Y += w_Y % size;
        }
        start_row = rank * (w_Y / size);

        // Initialize local world with normal pattern
        init_local_world(w_X, w_Y, local_w_Y, start_row);
    }

    // Count initial population in local domain
    local_count = 0;
    for (int x = 0; x < w_X; x++) {
        for (int y = 1; y <= local_w_Y; y++) {  // Skip ghost rows
            if (local_w[y][x] == 1) local_count++;
        }
    }

    // Get global population count
    MPI_Allreduce(&local_count, &init_count, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    global_count = init_count;

    if (rank == 0) {
        printf("initial world, population count: %d\n", init_count);
    }

    if (DEBUG_LEVEL > 10) {
        // Print initial world for debugging
        for (int proc = 0; proc < size; proc++) {
            if (rank == proc) {
                printf("Process %d initial local world:\n", rank);
                for (int y = 1; y <= local_w_Y; y++) {
                    for (int x = 0; x < w_X; x++) {
                        printf("%d", (int)local_w[y][x]);
                    }
                    printf("\n");
                }
            }
            MPI_Barrier(MPI_COMM_WORLD);
        }
    }

    // Start timer
    start_time = MPI_Wtime();

    // Main simulation loop
    for (iter = 0; (iter < 200) && (global_count < 50 * init_count) &&
         (global_count > init_count / 50); iter++) {

        // Exchange ghost rows with neighbors
        exchange_ghost_rows(w_X, local_w_Y, rank, size);

        // Update local domain
        for (int x = 0; x < w_X; x++) {
            for (int y = 1; y <= local_w_Y; y++) {  // Skip ghost rows
                c = 0; // Initialize count to 0

                // Directly implement neighbor counting to avoid bounds checking
                int global_y = start_row + (y - 1);  // Convert to global coordinates

                if (x == 0) {
                    if (global_y == 0) {  // Top-left corner
                        c = local_w[y][x+1] + local_w[y+1][x] + local_w[y+1][x+1];
                    } else if (global_y == w_Y-1) {  // Bottom-left corner
                        c = local_w[y][x+1] + local_w[y-1][x] + local_w[y-1][x+1];
                    } else {  // Left edge (not corner)
                        c = local_w[y-1][x] + local_w[y+1][x] + local_w[y-1][x+1] + local_w[y][x+1] + local_w[y+1][x+1];
                    }
                } else if (x == w_X-1) {
                    if (global_y == 0) {  // Top-right corner
                        c = local_w[y][x-1] + local_w[y+1][x-1] + local_w[y+1][x];
                    } else if (global_y == w_Y-1) {  // Bottom-right corner
                        c = local_w[y][x-1] + local_w[y-1][x] + local_w[y-1][x-1];
                    } else {  // Right edge (not corner)
                        c = local_w[y-1][x] + local_w[y+1][x] + local_w[y-1][x-1] + local_w[y][x-1] + local_w[y+1][x-1];
                    }
                } else {  // Not on left or right edge
                    if (global_y == 0) {  // Top edge (not corner)
                        c = local_w[y][x-1] + local_w[y][x+1] + local_w[y+1][x-1] + local_w[y+1][x] + local_w[y+1][x+1];
                    } else if (global_y == w_Y-1) {  // Bottom edge (not corner)
                        c = local_w[y][x-1] + local_w[y][x+1] + local_w[y-1][x-1] + local_w[y-1][x] + local_w[y-1][x+1];
                    } else {  // Middle of grid
                        c = local_w[y-1][x-1] + local_w[y][x-1] + local_w[y+1][x-1] + local_w[y-1][x] + local_w[y+1][x]
                              + local_w[y-1][x+1] + local_w[y][x+1] + local_w[y+1][x+1];
                    }
                }

                if (c <= 1) neww[y][x] = 0;      // die of loneliness
                else if (c >= 4) neww[y][x] = 0;  // die of overpopulation
                else if (c == 3) neww[y][x] = 1;  // becomes alive
                else neww[y][x] = local_w[y][x];  // c == 2, no change
            }
        }

        // Copy new world to current world and count population
        local_count = 0;
        for (int x = 0; x < w_X; x++) {
            for (int y = 1; y <= local_w_Y; y++) {  // Skip ghost rows
                local_w[y][x] = neww[y][x];
                if (local_w[y][x] == 1) local_count++;
            }
        }

        // Get global population count
        MPI_Allreduce(&local_count, &global_count, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

        if (rank == 0) {
            printf("iter = %d, population count = %d\n", iter, global_count);
        }

        if (DEBUG_LEVEL > 10) {
            // Print world after each iteration for debugging
            for (int proc = 0; proc < size; proc++) {
                if (rank == proc) {
                    printf("Process %d after iteration %d:\n", rank, iter);
                    for (int y = 1; y <= local_w_Y; y++) {
                        for (int x = 0; x < w_X; x++) {
                            printf("%d", (int)local_w[y][x]);
                        }
                        printf("\n");
                    }
                }
                MPI_Barrier(MPI_COMM_WORLD);
            }
        }
    }

    // Stop timer
    end_time = MPI_Wtime();

    if (rank == 0) {
        printf("MPI Execution time: %f seconds with %d processes\n", end_time - start_time, size);
    }

    // Optional: Write final world to file
    if (NOOUTPUTFILE != 1) {
        // Gather all local domains to rank 0
        char *global_w = NULL;
        int *recvcounts = NULL;
        int *displs = NULL;

        if (rank == 0) {
            global_w = (char *)malloc(w_X * w_Y * sizeof(char));
            if (!global_w) {
                printf("Error: Failed to allocate memory for global world\n");
                MPI_Abort(MPI_COMM_WORLD, 1);
            }

            // Initialize with zeros
            for (int i = 0; i < w_X * w_Y; i++) {
                global_w[i] = 0;
            }

            recvcounts = (int *)malloc(size * sizeof(int));
            displs = (int *)malloc(size * sizeof(int));

            if (!recvcounts || !displs) {
                printf("Error: Failed to allocate memory for gather parameters\n");
                MPI_Abort(MPI_COMM_WORLD, 1);
            }

            // Calculate receive counts and displacements
            int pos = 0;
            for (int i = 0; i < size; i++) {
                int i_local_w_Y = w_Y / size;
                if (i == size - 1) {
                    i_local_w_Y += w_Y % size;
                }
                recvcounts[i] = w_X * i_local_w_Y;
                displs[i] = pos;
                pos += recvcounts[i];
            }
        }

        // Pack local data (without ghost rows) for gathering
        char *local_data = (char *)malloc(w_X * local_w_Y * sizeof(char));
        if (!local_data) {
            printf("Error: Failed to allocate memory for local data on process %d\n", rank);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        // Copy data from 2D array to 1D array for gathering
        for (int y = 0; y < local_w_Y; y++) {
            for (int x = 0; x < w_X; x++) {
                local_data[y * w_X + x] = local_w[y + 1][x];  // +1 to skip ghost row
            }
        }

        // Gather data to rank 0
        MPI_Gatherv(local_data, w_X * local_w_Y, MPI_CHAR,
                   global_w, recvcounts, displs, MPI_CHAR,
                   0, MPI_COMM_WORLD);

        free(local_data);

        // Write to file on rank 0
        if (rank == 0) {
            FILE *fd;
            if ((fd = fopen("final_world000.txt", "w")) != NULL) {
                // Write data in column-major format as expected by the assignment
                for (int x = 0; x < w_X; x++) {
                    for (int y = 0; y < w_Y; y++) {
                        fprintf(fd, "%d", (int)global_w[y * w_X + x]);
                    }
                    fprintf(fd, "\n");
                }
                fclose(fd);
            } else {
                printf("Can't open file final_world000.txt\n");
            }

            free(global_w);
            free(recvcounts);
            free(displs);
        }
    }

    MPI_Finalize();
    return 0;
}