/*
 * Based on sequential code created by Dr. Xin Yuan for the COP5570 class
 *
 * To compile to test performance:
 *    gcc -O3 -pthread -DNOOUTPUTFILE proj4_pthread.c -o proj4_pthread
 *
 * To compile to test correctness (the program will output
 *    file final_world000.txt for comparison:
 *    gcc -pthread proj4_pthread.c -o proj4_pthread
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#ifdef NOOUTPUTFILE
#define NOOUTPUTFILE 1
#else
#define NOOUTPUTFILE 0
#endif

#define MAX_N 8192
#define MAX_THREADS 64

#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL 0
#endif

char w[MAX_N][MAX_N];
char neww[MAX_N][MAX_N];

int w_X, w_Y;
int init_count, count;

// Thread structure
typedef struct {
    int id;
    int nthreads;
    pthread_t thread;
    int active;  // Flag to indicate if thread is currently active
} ThreadInfo;

ThreadInfo thread_info[MAX_THREADS];

// Task structure
typedef struct {
    int start_y;
    int end_y;
    int start_x;
    int end_x;
    int task_id;
} Task;

#define MAX_TASKS 1024
Task task_queue[MAX_TASKS];
int task_count = 0;
int next_task = 0;
int active_workers = 0;
int total_tasks_created = 0;

// Flags to control thread execution
int threads_running = 1;  // Set to 0 to terminate threads
int iteration_complete = 0; // Set to 1 when all tasks for an iteration are done

// Synchronization primitives
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t task_available = PTHREAD_COND_INITIALIZER;
pthread_cond_t all_tasks_done = PTHREAD_COND_INITIALIZER;

// Initialize the world
void init(int X, int Y)
{
    int i, j;
    w_X = X, w_Y = Y;
    for (i=0; i<w_X; i++)
        for (j=0; j<w_Y; j++)
            w[j][i] = 0;

    for (i=0; i<w_X && i < w_Y; i++) w[i][i] = 1;
    for (i=0; i<w_Y && i < w_X; i++) w[w_Y - 1 - i][i] = 1;
}

// Test initialization
void test_init()
{
    printf("Test on a small 4x6 world\n");
    int i, j;
    w_X = 4;
    w_Y = 6;

    for (i=0; i<w_X; i++)
        for (j=0; j<w_Y; j++)
            w[j][i] = 0;
    w[0][3] = 1;
    w[1][3] = 1;
    w[2][1] = 1;
    w[3][0] = w[3][1] = w[3][2] = w[4][1] = w[5][1] = 1;
}

// Print the world state
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

// Count neighbors for a cell
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

// Create tasks for the current iteration
void create_tasks(int iteration) {
    // Reset task queue
    task_count = 0;
    next_task = 0;

    // Calculate chunk size with progressive reduction
    // Start with larger tasks in early iterations and reduce size over time
    int base_divisor = 4; // Start dividing the grid into 4x4 chunks
    int divisor = base_divisor + (iteration / 5); // Reduce chunk size every 5 iterations
    if (divisor > 20) divisor = 20; // Cap at 20x20 grid division

    int chunk_size_y = (w_Y + divisor - 1) / divisor; // Ceiling division
    int chunk_size_x = (w_X + divisor - 1) / divisor;

    // Ensure minimum chunk size
    if (chunk_size_y < 2) chunk_size_y = 2;
    if (chunk_size_x < 2) chunk_size_x = 2;

    if (DEBUG_LEVEL > 0) {
        printf("Iteration %d: Chunk size %dx%d (grid divided into %dx%d chunks)\n",
               iteration, chunk_size_x, chunk_size_y, divisor, divisor);
    }

    // Create tasks by dividing the grid into chunks
    for (int y = 0; y < w_Y; y += chunk_size_y) {
        int end_y = y + chunk_size_y;
        if (end_y > w_Y) end_y = w_Y;

        for (int x = 0; x < w_X; x += chunk_size_x) {
            int end_x = x + chunk_size_x;
            if (end_x > w_X) end_x = w_X;

            if (task_count < MAX_TASKS) {
                task_queue[task_count].start_y = y;
                task_queue[task_count].end_y = end_y;
                task_queue[task_count].start_x = x;
                task_queue[task_count].end_x = end_x;
                task_queue[task_count].task_id = total_tasks_created++;
                task_count++;
            } else {
                printf("Warning: Task queue full, some parts of the grid may not be processed\n");
                return;
            }
        }
    }

    if (DEBUG_LEVEL > 0) {
        printf("Created %d tasks for iteration %d\n", task_count, iteration);
    }
}

// Process a task
void process_task(Task *task) {
    int x, y, c;

    for (x = task->start_x; x < task->end_x; x++) {
        for (y = task->start_y; y < task->end_y; y++) {
            c = neighborcount(x, y);  // count neighbors
            if (c <= 1) neww[y][x] = 0;       // die of loneliness
            else if (c >= 4) neww[y][x] = 0;  // die of overpopulation
            else if (c == 3) neww[y][x] = 1;  // becomes alive
            else neww[y][x] = w[y][x];        // c == 2, no change
        }
    }
}

// Update the world after an iteration
void update_world() {
    int x, y;

    // Copy new world to current world and count population
    count = 0;
    for (x = 0; x < w_X; x++) {
        for (y = 0; y < w_Y; y++) {
            w[y][x] = neww[y][x];
            if (w[y][x] == 1) count++;
        }
    }
}

// Function to fetch a task from the queue (returns 1 if task available, 0 otherwise)
int get_task(Task *task) {
    if (next_task >= task_count) {
        return 0;  // No more tasks
    }

    *task = task_queue[next_task++];
    return 1;
}

// Worker thread function
void *worker_thread(void *arg) {
    ThreadInfo *info = (ThreadInfo *)arg;
    Task task;

    while (threads_running) {
        // Wait for tasks to be available
        pthread_mutex_lock(&queue_mutex);

        // Mark thread as inactive while waiting
        info->active = 0;

        // Wait until there's a task or we're told to terminate
        while (next_task >= task_count && threads_running) {
            pthread_cond_wait(&task_available, &queue_mutex);
        }

        // If we're terminating, exit the thread
        if (!threads_running) {
            pthread_mutex_unlock(&queue_mutex);
            pthread_exit(NULL);
        }

        // Get a task if available
        int got_task = get_task(&task);

        if (got_task) {
            info->active = 1;
            active_workers++;
        }

        pthread_mutex_unlock(&queue_mutex);

        // Process the task if we got one
        if (got_task) {
            process_task(&task);

            // Mark task as completed
            pthread_mutex_lock(&queue_mutex);
            active_workers--;

            // If all tasks are done, signal the main thread
            if (active_workers == 0 && next_task >= task_count) {
                pthread_cond_signal(&all_tasks_done);
            }

            pthread_mutex_unlock(&queue_mutex);
        }
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    int nthreads = 4;  // Default number of threads
    int iter = 0;
    int c;

    // Process command line arguments
    if (argc == 1) {
        printf("Usage: ./proj4_pthread w_X w_Y [num_threads]\n");
        exit(0);
    } else if (argc == 2) {
        test_init();
    } else {
        // Regular initialization
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

    // Count initial population
    c = 0;
    for (int x = 0; x < w_X; x++) {
        for (int y = 0; y < w_Y; y++) {
            if (w[y][x] == 1) c++;
        }
    }

    init_count = c;
    count = init_count;

    printf("Initial world, population count: %d, using %d threads\n", c, nthreads);
    if (DEBUG_LEVEL > 10) print_world();

    // Create worker threads
    for (int i = 0; i < nthreads; i++) {
        thread_info[i].id = i;
        thread_info[i].nthreads = nthreads;
        thread_info[i].active = 0;

        if (pthread_create(&thread_info[i].thread, NULL, worker_thread, &thread_info[i]) != 0) {
            perror("Thread creation failed");
            exit(1);
        }
    }

    // Main simulation loop
    for (iter = 0; (iter < 200) && (count < 50 * init_count) &&
        (count > init_count / 50); iter++) {

        // Create tasks for this iteration
        pthread_mutex_lock(&queue_mutex);
        create_tasks(iter);

        // Reset active workers counter
        active_workers = 0;

        // Signal worker threads that tasks are available
        pthread_cond_broadcast(&task_available);
        pthread_mutex_unlock(&queue_mutex);

        // Wait for all tasks to complete
        pthread_mutex_lock(&queue_mutex);
        while (next_task < task_count || active_workers > 0) {
            pthread_cond_wait(&all_tasks_done, &queue_mutex);
        }
        pthread_mutex_unlock(&queue_mutex);

        // Update the world and count population
        update_world();

        printf("iter = %d, population count = %d\n", iter, count);
        if (DEBUG_LEVEL > 10) print_world();
    }

    // Signal threads to exit
    pthread_mutex_lock(&queue_mutex);
    threads_running = 0;
    pthread_cond_broadcast(&task_available);
    pthread_mutex_unlock(&queue_mutex);

    // Join worker threads
    for (int i = 0; i < nthreads; i++) {
        pthread_join(thread_info[i].thread, NULL);
    }

    // Write the final world state to a file if needed
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

    // Clean up
    pthread_mutex_destroy(&queue_mutex);
    pthread_cond_destroy(&task_available);
    pthread_cond_destroy(&all_tasks_done);

    return 0;
}