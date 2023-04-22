// Copyright 2009-2022 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2022, NTESS
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#include <sst_config.h>

#include "uint.h"
#include "sint.h"
#include "rtl_header.h"
#include "rtlevent.h"

using namespace SST;
using namespace SST::RtlComponent;

void RTLEvent::UpdateRtlSignals(void *update_data, Rtlheader* cmodel, uint64_t& cycles) {
    bool* update_rtl_params = (bool*)update_data; 
    update_inp = update_rtl_params[0];
    update_ctrl = update_rtl_params[1];
    update_eval_args = update_rtl_params[2];
    update_registers = update_rtl_params[3];
    verbose = update_rtl_params[4];
    done_reset = update_rtl_params[5];
    sim_done = update_rtl_params[6];
    uint64_t* cycles_ptr = (uint64_t*)(&update_rtl_params[7]);
    sim_cycles = *cycles_ptr;
    cycles = sim_cycles;
    cycles_ptr++;

    output.verbose(CALL_INFO, 1, 0, "sim_cycles: %" PRIu64 "\n", sim_cycles);
    output.verbose(CALL_INFO, 1, 0, "update_inp: %d\n", update_inp);
    output.verbose(CALL_INFO, 1, 0, "update_ctrl: %d\n", update_ctrl);
    if(update_inp) {
        inp_ptr =  (void*)cycles_ptr; 
        input_sigs(cmodel);
    }

    if(update_ctrl) {
        /*
        UInt<4>* rtl_inp_ptr = (UInt<4>*)inp_ptr;
        ctrl_ptr = (void*)(&rtl_inp_ptr[5]);
        control_sigs(cmodel);
        */
    }
}

void RTLEvent::input_sigs(Rtlheader* cmodel) {

    //Cast all the variables to 4 byte UInt types for uniform storage for now. Later, we either will remove UInt and SInt and use native types. Even then we would need to cast the every variables based on type, width and order while storing in shmem and accordingly access it at runtime from shmem.   
    UInt<1>* rtl_inp_ptr = (UInt<1>*)inp_ptr;
    cmodel->reset = rtl_inp_ptr[0];
    UInt<1>* rtl_inp_ptr1 = (UInt<1>*)&rtl_inp_ptr[1];
    cmodel->io_inc = rtl_inp_ptr1[0];
    UInt<4>* rtl_inp_ptr2 = (UInt<4>*)&rtl_inp_ptr1[1];
    cmodel->io_amt = rtl_inp_ptr2[0];
    stringstream reset, io_inc, io_amt;
    reset << cmodel->reset;
    io_inc << cmodel->io_inc;
    io_amt << cmodel->io_amt;
    output.verbose(CALL_INFO, 1, 0, "input_sigs: %s", reset.str().c_str());
    output.verbose(CALL_INFO, 1, 0, "input_sigs: %s", io_inc.str().c_str());
    output.verbose(CALL_INFO, 1, 0, "input_sigs: %s", io_amt.str().c_str());
    return;
}

void RTLEvent::control_sigs(Rtlheader* cmodel) {
/*
    output.verbose(CALL_INFO, 1, 0, "\nctrl_sigs called"); 
    UInt<1>* rtl_ctrl_ptr = (UInt<1>*)ctrl_ptr;
    cmodel->reset = rtl_ctrl_ptr[0];
    stringstream reset;
    output.verbose(CALL_INFO, 1, 0, "ctrl_sigs: %s", reset.str().c_str());
    return;
    */
}

