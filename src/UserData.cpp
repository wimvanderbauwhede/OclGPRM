/* As a user you are required to implement the populateData(cl_uint *data) function
   which is used to populate the data store with input data and allocate memory for results. */
#include "UserData.h"
#include "SharedMacros.h"
#include <ctime>
#include <iostream>

/* Generate a random number between 0 and max. */
int randomNumber(int max) {
  return (rand() % (max + 1));
}
#if EX == 4
/* Populates the data store with the data needed for execute example 4. */
void populateData(cl_uint *data) {
  /* Initialise seed for random number generation. */
  srand(1);

  int dim = 2; // N rows of a square matrix.

  /* Total number of memory sections allocated. */
  // WV I think of this as number of I/O registers used
  unsigned int n_io_regs = 6

  data[0] = n_io_regs;
  /* Pointers to allocated memory. */
  // WV start of memory area allocated for I/O buffers
  data[1] = BUFFER_FILE_SZ + REGISTER_FILE_SZ; // reg 0
  data[2] = data[1] + (dim * dim); // reg 1
  data[3] = data[2] + (dim * dim); // reg 2
  data[4] = data[3] + (dim * dim); // reg 4
  data[5] = dim; // const 
  data[6] = data[4] + (dim * dim); // reg 6

  /* Pointer to scratch memory. */
  data[n_io_regs + 1] = data[6] + (dim * dim); // Pointer to scratch free/scratch memory.

  /* Populate input matrices. */
  /*
    for (uint i = data[1]; i < data[2]; i++) {
    data[i] = randomNumber(10);
    }

    for (uint i = data[2]; i < data[3]; i++) {
    data[i] = randomNumber(10);
    }
  */

  data[data[1]] = 1;
  data[data[1] + 1] = 2;
  data[data[1] + 2] = -3;
  data[data[1] + 3] = 11;

  data[data[2]] = -2;
  data[data[2] + 1] = 4;
  data[data[2] + 2] = 7;
  data[data[2] + 3] = 1;
}
#elif EX == 1
/* Replace populateData with this one to run example 1. */
void populateData(cl_uint *data) {
  /* Initialise seed for random number generation. */
  srand(1);
  
  int dim = 1024; // N rows of a square matrix.
  
  /* Total number of memory sections allocated. */
   unsigned int n_io_regs = 3
  data[0] = n_io_regs;
  
  /* Pointers to allocated memory. */
  data[1] =  BUFFER_FILE_SZ + REGISTER_FILE_SZ;
  data[2] = data[1] + (dim * dim);
  data[3] = data[2] + (dim * dim);
  
  /* Pointer to scratch memory. */
  data[n_io_regs + 1] = data[3] + (dim * dim); // Pointer to scratch free/scratch memory.
  
  /* Populate input matrices. */
  
  for (uint i = data[1]; i < data[2]; i++) {
    data[i] = randomNumber(10);
  }
  
  for (uint i = data[2]; i < data[3]; i++) {
    data[i] = randomNumber(10);
  }
}
#endif
