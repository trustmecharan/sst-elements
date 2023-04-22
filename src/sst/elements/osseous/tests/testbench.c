    // Copyright 2009-2015 Sandia Corporation. Under the terms
// of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2015, Sandia Corporation
// All rights reserved.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#include "tb_header.h"
#include <memory.h>
#include <sint.h>
#include <stdio.h>
#include <stdlib.h>
#include <uint.h>
#include <utility>
#include <vector>
#include <inttypes.h>
int main(int argc, char *argv[]) {

  const int LENGTH = 32768;

  ariel_enable();
/*
  printf("Allocating arrays of size %d elements.\n", LENGTH);
  double *a = (double *)mlm_malloc(sizeof(double) * LENGTH, 0);
  double *b = (double *)mlm_malloc(sizeof(double) * LENGTH, 0);
  double *fast_c = (double *)mlm_malloc(sizeof(double) * LENGTH, 0);
*/
  UInt<1> reset, io_inc;
  UInt<4> io_amt;
  UInt<1> *inp_ptr;
  UInt<1> *ctrl_ptr;

  mlm_set_pool(1);
/*
  printf("Allocation for fast_c is %llu\n", (unsigned long long int)fast_c);
  double *c = (double *)malloc(sizeof(double) * LENGTH);
  printf("Done allocating arrays.\n");

  int i;
  for (i = 0; i < LENGTH; ++i) {
    a[i] = i;
    b[i] = LENGTH - i;
    c[i] = 0;
  }

  // Issue a memory copy
  mlm_memcpy(fast_c, c, sizeof(double) * LENGTH);

  printf("Perfoming the fast_c compute loop...\n");
#pragma omp parallel for
  for (i = 0; i < LENGTH; ++i) {
    // printf("issuing a write to: %llu (fast_c)\n", ((unsigned long long int)
    // &fast_c[i]));
    fast_c[i] = 2.0 * a[i] + 1.5 * b[i];
  }

  // Now copy results back
  mlm_Tag copy_tag = mlm_memcpy(c, fast_c, sizeof(double) * LENGTH);
  mlm_waitComplete(copy_tag);
*/
  reset = UInt<1>(1);
  io_inc = UInt<1>(0);
  io_amt= UInt<4>(1);


  size_t inp_size = sizeof(UInt<1>) + sizeof(UInt<1>) + sizeof(UInt<4>) ;
  size_t ctrl_size = sizeof(UInt<1>);
  RTL_shmem_info *shmem = new RTL_shmem_info(inp_size, ctrl_size);

  inp_ptr = (UInt<1>*)shmem->get_inp_ptr();
  ctrl_ptr = (UInt<1>*)shmem->get_ctrl_ptr();

  inp_ptr[0] = reset;
  inp_ptr[1] = io_inc;
  UInt<4> *tmp_ptr = (UInt<4>*)&inp_ptr[2];
  tmp_ptr[0] = io_amt;

  Update_RTL_Params *params = new Update_RTL_Params();
  params->storetomem(shmem);
  inp_ptr[0] = reset;
  inp_ptr[1] = io_inc;
  tmp_ptr[0] = io_amt;
  params->storetomem(shmem);

  start_RTL_sim(shmem);
  bool *check = (bool *)shmem->get_inp_ptr();
  printf("\nSimulation started\n");

  reset = UInt<1>(1);
  inp_ptr[0] = reset;

  params->perform_update(true, true, true, true, true, false, false, 2);
  params->storetomem(shmem);
  inp_ptr[0] = reset;
  params->storetomem(shmem);
  update_RTL_sig(shmem);

  reset = UInt<1>(0);
  io_inc = UInt<1>(1);
  io_amt= UInt<4>(1);
  inp_ptr[0] = reset;
  inp_ptr[1] = io_inc;
  tmp_ptr[0] = io_amt;

  params->perform_update(true, true, true, true, true, true, false, 4);
  params->storetomem(shmem);
  inp_ptr[0] = reset;
  inp_ptr[1] = io_inc;
  tmp_ptr[0] = io_amt;
  params->storetomem(shmem);
  update_RTL_sig(shmem);

  io_amt = UInt<4>(2);
  tmp_ptr[0] = io_amt;

  params->perform_update(true, true, true, true, true, true, true, 4);
  params->storetomem(shmem);
  tmp_ptr[0] = io_amt;
  params->storetomem(shmem);
  update_RTL_sig(shmem);

/*
  double sum = 0;
  for (i = 0; i < LENGTH; ++i) {
    sum += c[i];
  }

  printf("Sum of arrays is: %f\n", sum);
  printf("Freeing arrays...\n");

  mlm_free(a);
  mlm_free(b);
  mlm_free(fast_c);
  free(c);
*/
  delete shmem;
  delete params;

  printf("Done.\n");

  return 0;
}
