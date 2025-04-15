#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <string.h>

#ifdef NOOUTPUTFILE
#define NOOUTPUTFILE 1
#else
#define NOOUTPUTFILE 0
#endif

#define MAX_N 8192

#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL 0
#endif

char w[MAX_N][MAX_N];
char neww[MAX_N][MAX_N];

int w_X, w_Y;

void init(int X, int Y)
{
    int i, j;
    w_X = X,  w_Y = Y;
    for (i=0; i<w_X;i++)
        for (j=0; j<w_Y; j++)
            w[j][i] = 0;

    for (i=0; i<w_X && i < w_Y; i++) w[i][i] = 1;
    for (i=0; i<w_Y && i < w_X; i++) w[w_Y - 1 - i][i] = 1;
}

void test_init()
{
    printf("Test on a small 4x6 world\n");
    int i, j;
    w_X = 4;
    w_Y = 6;

    for (i=0; i<w_X;i++)
        for (j=0; j<w_Y; j++)
            w[j][i] = 0;
    w[0][3] = 1;
    w[1][3] = 1;
    w[2][1] = 1;
    w[3][0] = w[3][1] = w[3][2] = w[4][1] = w[5][1] = 1;
}

void print_world()
{
    int i, j;

    for (i=0; i<w_Y; i++) {
        for (j=0; j<w_X; j++) {
            printf("%d", (int)w[i][j]);
        }
        printf("\n");
    }
}

int neighborcount(int x, int y)
{
    int count = 0;

    if ((x<0) || (x >=w_X)) {
        printf("neighborcount: (%d %d) out of bound (0..%d, 0..%d).\n", x,y,
           w_X, w_Y);
        exit(0);
    }
    if ((y<0) || (y >=w_Y)) {
        printf("neighborcount: (%d %d) out of bound (0..%d, 0..%d).\n", x,y,
           w_X, w_Y);
        exit(0);
    }

    if (x==0) {
        if (y == 0) {
            count = w[y][x+1] + w[y+1][x] + w[y+1][x+1];
        } else if (y == w_Y-1) {
            count = w[y][x+1] + w[y-1][x] + w[y-1][x+1];
        } else {
            count = w[y-1][x] + w[y+1][x] + w[y-1][x+1] + w[y][x+1] + w[y+1][x+1];
        }
    } else if (x == w_X -1) {
        if (y == 0) {
            count = w[y][x-1] + w[y+1][x-1] + w[y+1][x];
        } else if (y == w_Y-1) {
            count = w[y][x-1] + w[y-1][x] + w[y-1][x-1];
        } else {
            count = w[y-1][x] + w[y+1][x] + w[y-1][x-1] + w[y][x-1] + w[y+1][x-1];
        }
    } else { /* x is in the middle */
        if (y == 0) {
            count = w[y][x-1] + w[y][x+1] + w[y+1][x-1] + w[y+1][x] + w[y+1][x+1];
        } else if (y == w_Y-1) {
            count = w[y][x-1] + w[y][x+1] + w[y-1][x-1] + w[y-1][x] + w[y-1][x+1];
        } else {
            count = w[y-1][x-1] + w[y][x-1] + w[y+1][x-1] + w[y-1][x] + w[y+1][x]
                    + w[y-1][x+1] + w[y][x+1] + w[y+1][x+1];
        }
    }

    return count;
}

int main(int argc, char *argv[])
{
    int rank, size;
    int local_w_Y, start_row;
    int iter = 0;
    int c, local_count, global_count, init_count;
    MPI_Request requests[4];
    MPI_Status statuses[4];

    /* Initialize MPI */
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (argc == 1) {
        if (rank == 0) {
            printf("Usage: mpirun -np <num_processes> ./mpi <w_X> <w_Y>\n");
        }
        MPI_Finalize();
        exit(0);
    } else if (argc == 2) {
          test_init();
    } else /* more than three parameters */
        init(atoi(argv[1]), atoi(argv[2]));


    /* Calculate local grid division */
    local_w_Y = w_Y / size;
    /* Assign remainder rows */
    if (rank < (w_Y % size))
        local_w_Y++;

    start_row = 0;
    for (int i = 0; i < rank; i++) {
        start_row += w_Y / size;
        if (i < (w_Y % size))
            start_row++;
    }

    /* Initialize local grid */
    for (int i = 0; i < w_X; i++) {
        for (int j = 0; j < local_w_Y + 2; j++) {
            w[j][i] = 0;
        }
    }

    /* Initialize the grid with pattern */
    if (argc == 2) {
        /* Test initialization */
        if (0 >= start_row && 0 < start_row + local_w_Y) w[(0 - start_row) + 1][3] = 1;
        if (1 >= start_row && 1 < start_row + local_w_Y) w[(1 - start_row) + 1][3] = 1;
        if (2 >= start_row && 2 < start_row + local_w_Y) w[(2 - start_row) + 1][1] = 1;
        if (3 >= start_row && 3 < start_row + local_w_Y) {
            w[(3 - start_row) + 1][0] = 1;
            w[(3 - start_row) + 1][1] = 1;
            w[(3 - start_row) + 1][2] = 1;
        }
        if (4 >= start_row && 4 < start_row + local_w_Y) w[(4 - start_row) + 1][1] = 1;
        if (5 >= start_row && 5 < start_row + local_w_Y) w[(5 - start_row) + 1][1] = 1;
    } else {
        for (int i = 0; i < w_X && i < w_Y; i++) {
            if (i >= start_row && i < start_row + local_w_Y) {
                w[(i - start_row) + 1][i] = 1;
            }
        }
        for (int i = 0; i < w_Y && i < w_X; i++) {
            int j = w_Y - 1 - i;
            if (j >= start_row && j < start_row + local_w_Y) {
                w[(j - start_row) + 1][i] = 1;
            }
        }
    }

    local_count = 0;
    for (int x = 0; x < w_X; x++) {
        for (int y = 1; y <= local_w_Y; y++) {
            if (w[y][x] == 1) local_count++;
        }
    }

    /* Sum up the global count */
    MPI_Allreduce(&local_count, &init_count, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    global_count = init_count;

    if (rank == 0) {
        printf("initial world, population count: %d\n", init_count);
    }
    if (DEBUG_LEVEL > 10) print_world();

    for (iter = 0; (iter < 200) && (global_count < 50 * init_count) &&
         (global_count > init_count / 50); iter++) {

        int req_count = 0;

        /* Exchange rows */
        if (rank > 0) {
            /* Receive top row from previous process */
            MPI_Irecv(&w[0][0], w_X, MPI_CHAR, rank - 1, 0,
                     MPI_COMM_WORLD, &requests[req_count++]);
        }

        if (rank < size - 1) {
            /* Receive bottom row from next process */
            MPI_Irecv(&w[local_w_Y + 1][0], w_X, MPI_CHAR, rank + 1, 1,
                     MPI_COMM_WORLD, &requests[req_count++]);
        }

        if (rank > 0) {
            /* Send top row to previous process */
            MPI_Isend(&w[1][0], w_X, MPI_CHAR, rank - 1, 1,
                     MPI_COMM_WORLD, &requests[req_count++]);
        }

        if (rank < size - 1) {
            /* Send bottom row to next process */
            MPI_Isend(&w[local_w_Y][0], w_X, MPI_CHAR, rank + 1, 0,
                     MPI_COMM_WORLD, &requests[req_count++]);
        }

        /* Wait for row exchanges to complete */
        MPI_Waitall(req_count, requests, statuses);

        /* Update local grid */
        for (int x = 0; x < w_X; x++) {
            for (int y = 1; y <= local_w_Y; y++) {
                int global_y = start_row + (y - 1);
                c = neighborcount(x, y);  /* count neighbors */
                if (c <= 1) neww[y][x] = 0;      /* die of loneliness */
                else if (c >=4) neww[y][x] = 0;  /* die of overpopulation */
                else if (c == 3)  neww[y][x] = 1;             /* becomes alive */
                else neww[y][x] = w[y][x];   /* c == 2, no change */
            }
        }

        /* copy the world, and count the current lives */
        local_count = 0;
        for (int x = 0; x < w_X; x++) {
            for (int y = 1; y <= local_w_Y; y++) {
                w[y][x] = neww[y][x];
                if (w[y][x] == 1) local_count++;
            }
        }

        /* Get global population count */
        MPI_Allreduce(&local_count, &global_count, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

        if (rank == 0) {
            printf("iter = %d, population count = %d\n", iter, global_count);
        }
    }


    if (NOOUTPUTFILE != 1) {
        /* Gather all local grids to rank 0 */
        char *global_w = NULL;
        int *recvcounts = NULL;
        int *displs = NULL;

        if (rank == 0) {
            global_w = (char *)malloc(w_X * w_Y * sizeof(char));
            if (!global_w) {
                printf("Error: Failed to allocate memory for global world\n");
                MPI_Abort(MPI_COMM_WORLD, 1);
            }

            /* Initialize with zeros */
            memset(global_w, 0, w_X * w_Y * sizeof(char));

            recvcounts = (int *)malloc(size * sizeof(int));
            displs = (int *)malloc(size * sizeof(int));

            if (!recvcounts || !displs) {
                printf("Error: Failed to allocate memory for gather parameters\n");
                MPI_Abort(MPI_COMM_WORLD, 1);
            }

            int pos = 0;
            for (int i = 0; i < size; i++) {
                int i_local_w_Y = w_Y / size;
                if (i < (w_Y % size))
                    i_local_w_Y++;

                recvcounts[i] = w_X * i_local_w_Y;
                displs[i] = pos;
                pos += recvcounts[i];
            }
        }


        /* Transpose data from 2D array to 1D array for gathering */
        char *local_data = (char *)malloc(w_X * local_w_Y * sizeof(char));
        if (!local_data) {
            printf("Error: Failed to allocate memory for local data on process %d\n", rank);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        for (int y = 0; y < local_w_Y; y++) {
            for (int x = 0; x < w_X; x++) {
                local_data[y * w_X + x] = w[y + 1][x];
            }
        }

        /* Gather data to rank 0 */
        MPI_Gatherv(local_data, w_X * local_w_Y, MPI_CHAR,
                   global_w, recvcounts, displs, MPI_CHAR,
                   0, MPI_COMM_WORLD);

        free(local_data);

        if (rank == 0) {
            FILE *fd;
            if ((fd = fopen("final_world000.txt", "w")) != NULL) {
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

            /* Clean Up */
            free(global_w);
            free(recvcounts);
            free(displs);
        }
    }

    MPI_Finalize();
    return 0;
}