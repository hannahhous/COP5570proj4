/*
 * OpenMP implementation for the Game of Life
 * Based on the sequential version by Dr. Xin Yuan
 */

#include <stdio.h>
#include <stdlib.h>
// We'll use OpenMP directives without explicitly including omp.h

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

// Keep all the initialization and utility functions the same as sequential version
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
  int x, y;
  int iter = 0;
  int c;
  int init_count;
  int count;

  if (argc == 1) {
    printf("Usage: ./a.out w_X w_Y\n");
    exit(0);
  } else if (argc == 2)
    test_init();
  else /* more than three parameters */
    init(atoi(argv[1]), atoi(argv[2]));

  c = 0;
  for (x=0; x<w_X; x++) {
    for (y=0; y<w_Y; y++) {
      if (w[y][x] == 1) c++;
    }
  }

  init_count = c;
  count = init_count;

  printf("initial world, population count: %d\n", c);
  if (DEBUG_LEVEL > 10) print_world();

  for (iter = 0; (iter < 200) && (count <50*init_count) &&
     (count > init_count / 50); iter ++) {

    // Add OpenMP directive for the first nested loop
    #pragma omp parallel for private(y, c)
    for (x=0; x < w_X; x++) {
      for (y=0; y<w_Y; y++) {
        c = neighborcount(x, y);  /* count neighbors */
        if (c <= 1) neww[y][x] = 0;      /* die of loneliness */
        else if (c >=4) neww[y][x] = 0;  /* die of overpopulation */
        else if (c == 3)  neww[y][x] = 1;             /* becomes alive */
        else neww[y][x] = w[y][x];   /* c == 2, no change */
      }
    }

    /* copy the world, and count the current lives */
    count = 0;
    // Add OpenMP directive for the second nested loop with reduction
    #pragma omp parallel for private(y) reduction(+:count)
    for (x=0; x<w_X; x++) {
      for (y=0; y<w_Y; y++) {
        w[y][x] = neww[y][x];
        if (w[y][x] == 1) count++;
      }
    }

    printf("iter = %d, population count = %d\n", iter, count);
    if (DEBUG_LEVEL > 10) print_world();
  }

  if (NOOUTPUTFILE != 1)
  {
    FILE *fd;
    if ((fd = fopen("final_world000.txt", "w")) != NULL) {
      for (x=0; x<w_X; x++) {
        for (y=0; y<w_Y; y++) {
          fprintf(fd, "%d", (int)w[y][x]);
        }
        fprintf(fd, "\n");
      }
    } else {
      printf("Can't open file final_world000.txt\n");
      exit(1);
    }
  }
  return 0;
}