#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#ifdef NOOUTPUTFILE
#define NOOUTPUTFILE 1
#else
#define NOOUTPUTFILE 0
#endif

#define MAX_N 8192
#define MAX_THREADS 64
#define MAX_TASKS 10000

#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL 0
#endif

char w[MAX_N][MAX_N];
char neww[MAX_N][MAX_N];

int w_X, w_Y;

/* Dynamic task queue */
typedef struct {
    int start_row;
    int end_row;
    int chunk_size;
} Task;

/* Thread data structure */
typedef struct {
    int id;
    int nthreads;
    pthread_t thread;
} ThreadInfo;

ThreadInfo thread_info[MAX_THREADS];

/* Task queue */
Task task_queue[MAX_TASKS];
int num_tasks = 0;
int next_task = 0;
int active_threads = 0;

/* Synchronization */
pthread_mutex_t task_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t task_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t done_cond = PTHREAD_COND_INITIALIZER;

/* Flags */
int current_iteration = 0;
int program_done = 0;

/* Same initialization and utility functions as in the sequential code */
void init(int X, int Y) {
    int i, j;
    w_X = X, w_Y = Y;
    for (i = 0; i < w_X; i++)
        for (j = 0; j < w_Y; j++)
            w[j][i] = 0;

    for (i = 0; i < w_X && i < w_Y; i++) w[i][i] = 1;
    for (i = 0; i < w_Y && i < w_X; i++) w[w_Y - 1 - i][i] = 1;
}

void test_init() {
    printf("Test on a small 4x6 world\n");
    int i, j;
    w_X = 4;
    w_Y = 6;

    for (i = 0; i < w_X; i++)
        for (j = 0; j < w_Y; j++)
            w[j][i] = 0;
    w[0][3] = 1;
    w[1][3] = 1;
    w[2][1] = 1;
    w[3][0] = w[3][1] = w[3][2] = w[4][1] = w[5][1] = 1;
}

void print_world() {
    int i, j;

    for (i = 0; i < w_Y; i++) {
        for (j = 0; j < w_X; j++) {
            printf("%d", (int)w[i][j]);
        }
        printf("\n");
    }
}

int neighborcount(int x, int y) {
    int count = 0;

    if ((x < 0) || (x >= w_X)) {
        printf("neighborcount: (%d %d) out of bound (0..%d, 0..%d).\n", x, y, w_X, w_Y);
        exit(0);
    }
    if ((y < 0) || (y >= w_Y)) {
        printf("neighborcount: (%d %d) out of bound (0..%d, 0..%d).\n", x, y, w_X, w_Y);
        exit(0);
    }

    if (x == 0) {
        if (y == 0) {
            count = w[y][x+1] + w[y+1][x] + w[y+1][x+1];
        } else if (y == w_Y-1) {
            count = w[y][x+1] + w[y-1][x] + w[y-1][x+1];
        } else {
            count = w[y-1][x] + w[y+1][x] + w[y-1][x+1] + w[y][x+1] + w[y+1][x+1];
        }
    } else if (x == w_X - 1) {
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
            count = w[y-1][x-1] + w[y][x-1] + w[y+1][x-1] + w[y-1][x] + w[y+1][x]                    + w[y-1][x+1] + w[y][x+1] + w[y+1][x+1];
        }
    }

    return count;
}

/* New functions */
/* Process a single task */
void process_task(Task *task) {
    for (int y = task->start_row; y < task->end_row; y++) {
        for (int x = 0; x < w_X; x++) {
            int neighbors = neighborcount(x, y);    /* count neighbors */
            if (neighbors <= 1) neww[y][x] = 0;       /* die of loneliness */
            else if (neighbors >= 4) neww[y][x] = 0;  /* die of overpopulation */
            else if (neighbors == 3) neww[y][x] = 1;  /* becomes alive */
            else neww[y][x] = w[y][x];                /* c == 2, no change */
        }
    }
}

// Create tasks for the current iteration
void create_tasks(int iteration) {
    num_tasks = 0;
    next_task = 0;

    /* Calculate base chunk size */
    int base_chunk_size = 64;

    /* First chunks are at least 5x larger than last few chunks */
    int num_chunks = w_Y / base_chunk_size;
    if (w_Y % base_chunk_size > 0) num_chunks++;

    if (num_chunks < 2) num_chunks = 2;

    /* First third: largest chunks
       Middle third: medium chunks
       Last third: smallest chunks */
    int first_chunk_size = base_chunk_size * 5;
    int last_chunk_size = base_chunk_size;

    /* Reduce task size based on iteration */
    if (iteration > 10) {
        first_chunk_size = first_chunk_size * 10 / (iteration / 2 + 10);
        if (first_chunk_size < base_chunk_size * 2)
            first_chunk_size = base_chunk_size * 2;
    }

    /* Don't exceed MAX_TASKS */
    if (num_chunks > MAX_TASKS) {
        printf("Warning: Increasing chunk size to avoid exceeding task limit\n");
        base_chunk_size = w_Y / MAX_TASKS + 1;
        num_chunks = w_Y / base_chunk_size;
        if (w_Y % base_chunk_size > 0) num_chunks++;
        first_chunk_size = base_chunk_size * 5;
        last_chunk_size = base_chunk_size;
    }
/*!!!
    if (DEBUG_LEVEL > 0) {
        printf("Iteration %d: Creating %d tasks. First chunk: %d, Last chunk: %d\n",                iteration, num_chunks, first_chunk_size, last_chunk_size);
    }*/

    int row = 0;
    int third = num_chunks / 3;
    if (third < 1) third = 1;

    /* First third */
    for (int i = 0; i < third && row < w_Y; i++) {
        int end_row = row + first_chunk_size;
        if (end_row > w_Y) end_row = w_Y;

        task_queue[num_tasks].start_row = row;
        task_queue[num_tasks].end_row = end_row;
        task_queue[num_tasks].chunk_size = first_chunk_size;
        num_tasks++;

        row = end_row;
    }

    /* Middle third */
    int medium_chunk_size = (first_chunk_size + last_chunk_size) / 2;
    for (int i = 0; i < third && row < w_Y; i++) {
        int end_row = row + medium_chunk_size;
        if (end_row > w_Y) end_row = w_Y;

        task_queue[num_tasks].start_row = row;
        task_queue[num_tasks].end_row = end_row;
        task_queue[num_tasks].chunk_size = medium_chunk_size;
        num_tasks++;

        row = end_row;
    }

    /* Last third */
    while (row < w_Y && num_tasks < MAX_TASKS) {
        int end_row = row + last_chunk_size;
        if (end_row > w_Y) end_row = w_Y;

        task_queue[num_tasks].start_row = row;
        task_queue[num_tasks].end_row = end_row;
        task_queue[num_tasks].chunk_size = last_chunk_size;
        num_tasks++;

        row = end_row;
    }
/*!!!
    if (DEBUG_LEVEL > 0) {
        printf("Created %d tasks for iteration %d\n", num_tasks, iteration);
    }*/
}

/* Worker thread */
void *worker_thread(void *arg) {
    ThreadInfo *info = (ThreadInfo *)arg;
    int thread_id = info->id;
    Task task;
    int got_task;

    while (1) {
        /* Try to get a task */
        pthread_mutex_lock(&task_mutex);

        /* Wait for new tasks or program termination */
        while (next_task >= num_tasks && !program_done && current_iteration > 0) {
            if (active_threads == 0) {
                /* All tasks are done */
                pthread_cond_signal(&done_cond);
            }
            /* Wait for more tasks or next iteration */
            pthread_cond_wait(&task_cond, &task_mutex);
        }

        /* Check for program termination */
        if (program_done) {
            pthread_mutex_unlock(&task_mutex);
            break;
        }

        /* Try to get a task */
        got_task = 0;
        if (next_task < num_tasks) {
            task = task_queue[next_task++];
            active_threads++;
            got_task = 1;
/*!!!
            if (DEBUG_LEVEL > 1) {
                printf("Thread %d got task: rows %d-%d (size %d)\n",
                       thread_id, task.start_row, task.end_row, task.chunk_size);
            }*/
        }

        pthread_mutex_unlock(&task_mutex);

        /* Try to process task */
        if (got_task) {
            process_task(&task);

            /* Task is completed */
            pthread_mutex_lock(&task_mutex);
            active_threads--;

            /* Done if all tasks are done */
            if (next_task >= num_tasks && active_threads == 0) {
                pthread_cond_signal(&done_cond);
            }

            pthread_mutex_unlock(&task_mutex);
        }
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    int x, y;
    int iter = 0;
    int c;
    int init_count;
    int count;
    int nthreads = 4;  /* Default number of threads */


    if (argc == 1) {
        printf("Usage: ./a.out w_X w_Y [num threads]\n");
        exit(0);
    } else if (argc == 2) {
        test_init();
    } else /* more than three parameters */
    {
        init(atoi(argv[1]), atoi(argv[2]));

        // Get number of threads if specified
        if (argc >= 4) {
            nthreads = atoi(argv[3]);
            if (nthreads <= 0 || nthreads > MAX_THREADS) {
                printf("Invalid number of threads (using default: 4)\n");
                nthreads = 4;
            }
        }
    }

    c = 0;
    for (x=0; x<w_X; x++) {
        for (y=0; y<w_Y; y++) {
            if (w[y][x] == 1) c++;
        }
    }

    init_count = c;
    count = init_count;

    printf("Initial world, population count: %d, using %d threads\n", c, nthreads);
    if (DEBUG_LEVEL > 10) print_world();

    /* Create worker threads */
    for (int i = 0; i < nthreads; i++) {
        thread_info[i].id = i;
        thread_info[i].nthreads = nthreads;

        if (pthread_create(&thread_info[i].thread, NULL, worker_thread, &thread_info[i]) != 0) {
            perror("Thread creation failed");
            exit(1);
        }
    }

    for (iter = 0; iter < 200 && count < 50 * init_count && count > init_count / 50; iter++) {
        /* Create tasks for this iteration */
        pthread_mutex_lock(&task_mutex);
        create_tasks(iter + 1);
        current_iteration = iter + 1;
        active_threads = 0;

        /* Signal worker threads that tasks are available */
        pthread_cond_broadcast(&task_cond);

        /* Wait for all tasks to complete */
        /* Timeout added to avoid some cases with deadlocks */
        struct timespec timeout;
        int all_done = 0;

        while (!all_done) {
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_sec += 5;

            /* Wait for done signal or timeout */
            if (next_task >= num_tasks && active_threads == 0) {
                /* All tasks are already done */
                all_done = 1;
            } else {
                int wait_result = pthread_cond_timedwait(&done_cond, &task_mutex, &timeout);

                if (wait_result == ETIMEDOUT) {
                    printf("Warning: Timeout waiting for tasks to complete in iteration %d\n", iter);
                    printf("  %d/%d tasks completed, %d threads active\n",
                           next_task, num_tasks, active_threads);
                    all_done = 1;
                } else if (next_task >= num_tasks && active_threads == 0)
                {
                    all_done = 1;
                }
            }
        }

        pthread_mutex_unlock(&task_mutex);

        /* copy the world, and count the current lives */
        count = 0;
        for (x=0; x<w_X; x++) {
            for (y=0; y<w_Y; y++) {
                w[y][x] = neww[y][x];
                if (w[y][x] == 1) count++;
            }
        }
        printf("iter = %d, population count = %d\n", iter, count);
        if (DEBUG_LEVEL > 10) print_world();
    }

    /* Signal threads to exit */
    pthread_mutex_lock(&task_mutex);
    program_done = 1;
    pthread_cond_broadcast(&task_cond);
    pthread_mutex_unlock(&task_mutex);

    /* Wait for all threads to exit */
    for (int i = 0; i < nthreads; i++) {
        pthread_join(thread_info[i].thread, NULL);
    }

    if (NOOUTPUTFILE != 1) {
        FILE *fd;
        if ((fd = fopen("final_world000.txt", "w")) != NULL) {
            for (int x = 0; x < w_X; x++) {
                for (int y = 0; y < w_Y; y++) {
                    fprintf(fd, "%d", (int)w[y][x]);
                }
                fprintf(fd, "\n");
            }
            fclose(fd);
        } else {
            printf("Can't open file final_world000.txt\n");
            exit(1);
        }
    }

    /* Clean up */
    pthread_mutex_destroy(&task_mutex);
    pthread_cond_destroy(&task_cond);
    pthread_cond_destroy(&done_cond);

    return 0;
}