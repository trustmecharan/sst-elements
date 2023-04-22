// Copyright 2009-2020 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2020, NTESS
// All rights reserved.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#include <sst_config.h>
#include <sst/core/event.h>
#include "rtlcomponent.h"
#include "rtlevent.h"
#include "rtlmemmgr.h"
#include <fstream>
#include <string>


#define parse_nibble(c) ((c) >= 'a' ? (c)-'a'+10 : (c)-'0')

using namespace SST;
using namespace std;
using namespace SST::RtlComponent;
using namespace SST::Interfaces;

int m1300 = 0;
Rtlmodel::Rtlmodel(SST::ComponentId_t id, SST::Params& params) :
	SST::Component(id)/*, verbosity(static_cast<uint32_t>(out->getVerboseLevel()))*/ {

    bool found;
    dut = new Rtlheader;
    axiport = new AXITop;
    RtlAckEv = new ArielComponent::ArielRtlEvent();
	output.init("Rtlmodel-" + getName() + "-> ", 1, 0, SST::Output::STDOUT);

	RTLClk  = params.find<std::string>("ExecFreq", "1GHz" , found);
    if (!found) {
        Simulation::getSimulation()->getSimulationOutput().fatal(CALL_INFO, -1,"couldn't find work per cycle\n");
    }

	maxCycles = params.find<Cycle_t>("maxCycles", 0, found);
    if (!found) {
        Simulation::getSimulation()->getSimulationOutput().fatal(CALL_INFO, -1,"couldn't find work per cycle\n");
    }

	/*if(RTLClk == NULL || RTLClk == "0") 
		output.fatal(CALL_INFO, -1, "Error: printFrequency must be greater than zero.\n");*/

 	output.verbose(CALL_INFO, 1, 0, "Config: maxCycles=%" PRIu64 ", RTL Clock Freq=%s\n",
		static_cast<uint64_t>(maxCycles), RTLClk.c_str());

	// Just register a plain clock for this simple example
    output.verbose(CALL_INFO, 1, 0, "Registering RTL clock at %s\n", RTLClk.c_str());
   
   //set our clock 
   clock_handler = new Clock::Handler<Rtlmodel>(this, &Rtlmodel::clockTick); 
   timeConverter = registerClock(RTLClk, clock_handler);
   unregisterClock(timeConverter, clock_handler);
   writePayloads = true;//params.find<int>("writepayloadtrace") == 0 ? false : true;

    //Configure and register Event Handler for ArielRtllink
   ArielRtlLink = configureLink("ArielRtllink", new Event::Handler<Rtlmodel>(this, &Rtlmodel::handleArielEvent)); 

    // Find all the components loaded into the "memory" slot
    // Make sure all cores have a loaded subcomponent in their slot
    cacheLink = loadUserSubComponent<Interfaces::StandardMem>("memory", ComponentInfo::SHARE_NONE, timeConverter, new StandardMem::Handler<Rtlmodel>(this, &Rtlmodel::handleMemEvent));
    if(!cacheLink) {
       std::string interfaceName = params.find<std::string>("memoryinterface", "memHierarchy.standardInterface");
       output.verbose(CALL_INFO, 1, 0, "Memory interface to be loaded is: %s\n", interfaceName.c_str());
       
       Params interfaceParams = params.get_scoped_params("memoryinterfaceparams");
       interfaceParams.insert("port", "RtlCacheLink");
       cacheLink = loadAnonymousSubComponent<Interfaces::StandardMem>(interfaceName, "memory", 0, ComponentInfo::SHARE_PORTS | ComponentInfo::INSERT_STATS,
               interfaceParams, timeConverter, new StandardMem::Handler<Rtlmodel>(this, &Rtlmodel::handleMemEvent));
       
       if (!cacheLink)
           output.fatal(CALL_INFO, -1, "%s, Error loading memory interface\n", getName().c_str());
   }

    std::string memorymanager = params.find<std::string>("memmgr", "rtl.MemoryManagerSimple");
    if (NULL != (memmgr = loadUserSubComponent<RtlMemoryManager>("memmgr"))) {
        output.verbose(CALL_INFO, 1, 0, "Loaded memory manager: %s\n", memmgr->getName().c_str());
    } else {
        // Warn about memory levels and the selected memory manager if needed
        if (memorymanager == "rtl.MemoryManagerSimple" /*&& memLevels > 1*/) {
            output.verbose(CALL_INFO, 1, 0, "Warning - the default 'rtl.MemoryManagerSimple' does not support multiple memory levels. Configuring anyways but memorylevels will be 1.\n");
            params.insert("memmgr.memorylevels", "1", true);
        }

        output.verbose(CALL_INFO, 1, 0, "Loading memory manager: %s\n", memorymanager.c_str());
        Params mmParams = params.get_scoped_params("memmgr");
        memmgr = loadAnonymousSubComponent<RtlMemoryManager>(memorymanager, "memmgr", 0, ComponentInfo::SHARE_NONE | ComponentInfo::INSERT_STATS, mmParams);
        if (NULL == memmgr) output.fatal(CALL_INFO, -1, "Failed to load memory manager: %s\n", memorymanager.c_str());
    }

    output.verbose(CALL_INFO, 1, 0, "RTL Memory manager construction is completed.\n");

   pendingTransactions = new std::unordered_map<StandardMem::Request::id_t, StandardMem::Request*>();
   AXIReadPendingTransactions = new std::unordered_map<StandardMem::Request::id_t, StandardMem::Request*>();
   pending_transaction_count = 0;
   unregisterClock(timeConverter, clock_handler);
   isStalled = true;

    statReadRequests  = registerStatistic<uint64_t>( "read_requests");
    statWriteRequests = registerStatistic<uint64_t>( "write_requests");
    statReadRequestSizes = registerStatistic<uint64_t>( "read_request_sizes");
    statWriteRequestSizes = registerStatistic<uint64_t>( "write_request_sizes");
    statSplitReadRequests = registerStatistic<uint64_t>( "split_read_requests");
    statSplitWriteRequests = registerStatistic<uint64_t>( "split_write_requests");
    statFlushRequests = registerStatistic<uint64_t>( "flush_requests");
    statFenceRequests = registerStatistic<uint64_t>( "fence_requests");


   // Tell SST to wait until we authorize it to exit
   registerAsPrimaryComponent();
   primaryComponentDoNotEndSim();

   sst_assert(ArielRtlLink, CALL_INFO, -1, "ArielRtlLink is null");

    isRead=false;
    isLoaded=false;
    isWritten=false;
    canStartRead=false;
    isRespReceived=true;

    //AXI variables init
    size = (1L << 32);
    //data = new char[size];
    word_size = 8;
    store_inflight = false;
    dummy_data.resize(word_size);

    mCycles=0;

}

Rtlmodel::~Rtlmodel() {
    delete dut;
    delete axiport;

    //AXI variables deinit
    delete data;
}

void Rtlmodel::setup() {
    dut->reset = UInt<1>(1);
    axiport->reset = UInt<1>(1);
	output.verbose(CALL_INFO, 1, 0, "Component is being setup.\n");
    for(int i = 0; i < 512; i++)
        axiport->queue.ram[i] = 0;
    axiport->eval(true,true,true);
    axiport->reset = UInt<1>(0);
    /*
    tempptr = (char *)malloc(sizeof(char) * 11000);
    char *bin_ptr = (char *)malloc(sizeof(char) * 11000);

    std::ifstream fin("qsort.riscv.hex");
    std::string line;
    int start = 0;

    while(std::getline(fin, line))
    {
        for (int i = line.length()-2, j = 0; i >= 0; i -= 2, j++) {
        bin_ptr[start + j]  = (parse_nibble(line[i]) << 4) | parse_nibble(line[i+1]);
        //io_ins[start + j] = (char)temp;//UInt<4>(i);
        //mcount++;
        }
        start += line.length()/2;
    }
    fin.close();

    size_t temp_inp_size = inp_size;
    int temp_count=0;
    while(temp_inp_size > 0)
    {
        size_t temp_size;
        if(temp_inp_size >= cacheLineSize)
        {
            temp_size = cacheLineSize;
        }else
        {
            temp_size = temp_inp_size;
        }
        //char * temp_ptr = (char *)tempptr;
        RtlWriteEvent* rtlrev_inp_ptr = new RtlWriteEvent((uint64_t)&bin_ptr[temp_count],(uint32_t)temp_size,&bin_ptr[temp_count]); 
        VA_VA_map.insert({(uint64_t)&tempptr[temp_count], (uint64_t)(bin_ptr+temp_count)});
       // generateWriteRequest(rtlrev_inp_ptr);
        temp_inp_size -= temp_size;
        temp_count += temp_size;
    }
    */
}

void Rtlmodel::init(unsigned int phase) {
	output.verbose(CALL_INFO, 1, 0, "Component Init Phase Called %d\n", phase);
    cacheLink->init(phase);
    /*
    if(phase == 3)
    {
        tempptr = (char *)malloc(sizeof(char) * 11000);
        bin_ptr = (char *)malloc(sizeof(char) * 11000);

        std::ifstream fin("qsort.riscv.hex");
        std::string line;
        int start = 0;

        while(std::getline(fin, line))
        {
            for (int i = line.length()-2, j = 0; i >= 0; i -= 2, j++) {
            bin_ptr[start + j]  = (parse_nibble(line[i]) << 4) | parse_nibble(line[i+1]);
            //io_ins[start + j] = (char)temp;//UInt<4>(i);
            //mcount++;
            }
            start += line.length()/2;
        }
        fin.close();

        RtlWriteEvent* rtlrev_inp_ptr = new RtlWriteEvent((uint64_t)(&bin_ptr[0]),1024u,&bin_ptr[0]); 
        generateWriteRequest(rtlrev_inp_ptr);
    }
    */
}

//Nothing to add in finish as of now. Need to see what could be added.
void Rtlmodel::finish() {
    output.verbose(CALL_INFO, 1, 0, "mCycles: %d\n", mCycles);
	output.verbose(CALL_INFO, 1, 0, "Component is being finished.\n");
    //free(getBaseDataAddress());
    free(tempptr);
    free(bin_ptr);
}

//clockTick will actually execute the RTL design at every cycle based on the input and control signals updated by CPU CPU or Event Handler.
bool Rtlmodel::clockTick( SST::Cycle_t currentCycle ) {

    /*if(!isStalled) {
        if(tickCount == 4) {
            output.verbose(CALL_INFO, 1, 0, "AXI signals changed"); 
            axi_tvalid_$next = 1;
            axi_tdata_$next = 34;
            output.verbose(CALL_INFO, 1, 0, "\n Sending data at tickCount 4");
        }
    }

    if((axi_tvalid_$old ^ axi_tvalid_$next) || (axi_tdata_$old ^ axi_tdata_$next))  {
        uint8_t ready = 1;
        output.verbose(CALL_INFO, 1, 0, "handleAXISignals called"); 
        if(axiport->queue.maybe_full) 
            ready = 0;
        handleAXISignals(ready); 
        axiport->eval(true, true, true);

        //Initial value of AXI control signals
        fifo_enq_$old = axiport->queue.value_1.as_single_word();
        fifo_enq_$next = axiport->queue.value.as_single_word();
        uint64_t prev_data = axiport->queue.ram[fifo_enq_$old].as_single_word();

        while(!(prev_data ^ axiport->queue.ram[fifo_enq_$next].as_single_word())) {
            prev_data = axiport->queue.ram[fifo_enq_$next].as_single_word();
            axiport->eval(true, true, true);
            fifo_enq_$next = axiport->queue.value.as_single_word();
            if(fifo_enq_$old ^ fifo_enq_$next) {
                output.verbose(CALL_INFO, 1, 0, "\nQueue_value is: % %" PRIu64 PRIu64, axiport->queue.value, fifo_enq_$next); 
                output.verbose(CALL_INFO, 1, 0, "\nData enqueued in the queue: %" PRIu64, axiport->queue.ram[fifo_enq_$next]);
            }
            fifo_enq_$old = fifo_enq_$next;
        }
    }

    axi_tdata_$old = axi_tdata_$next;
    axi_tvalid_$old = axi_tvalid_$next;
    axi_tready_$old = axi_tready_$next;

    uint64_t read_addr = (axiport->queue.ram[fifo_enq_$next].as_single_word());// << 32) | (axiport->queue.ram[fifo_enq_$next+1].as_single_word());
    uint64_t size = (axiport->queue.ram[fifo_enq_$next+2].as_single_word());// << 32) | (axiport->queue.ram[fifo_enq_$next+3].as_single_word());*/

    //output.verbose(CALL_INFO, 1, 0, "\nSim Done is: %d", ev.sim_done);
    
    //static bool isLoaded=false;
    //static bool isWritten=false;
    //char *bin_ptr;
    //static int in_temp_inp_size = 0;
    //static int temp_inp_size = 0;
    m1300=0;
    if(!isLoaded)
    {
        isLoaded = true;
        tempptr = (char *)malloc(sizeof(char) * 11000);
        bin_ptr = (char *)malloc(sizeof(char) * 150000);

        std::ifstream fin("qsort.riscv.hex");
        std::string line;
        int start = 0;
        int num_bytes = 0;

        while(std::getline(fin, line))
        {
            for (int i = line.length()-2, j = 0; i >= 0; i -= 2, j++) {
            bin_ptr[start + j]  = (parse_nibble(line[i]) << 4) | parse_nibble(line[i+1]);
            //io_ins[start + j] = (char)temp;//UInt<4>(i);
            //in_temp_inp_size++;
            //temp_inp_size++;
            num_bytes++;
            }
            start += line.length()/2;
        }
        fin.close();
        printf("Num bytes in bin: %d", num_bytes);
    }

    static int in_temp_inp_size = 11000;
    static int in_temp_count=0;
    //static bool isRead=false;
    if(isWritten /*&& isRespReceived*//*&& canStartRead*/)
    {
        if(!isRead)
        {
            if(in_temp_inp_size > 0)
            {
                int in_temp_size;
                if(in_temp_inp_size >= cacheLineSize)
                {
                    in_temp_size = cacheLineSize;
                }else
                {
                    in_temp_size = in_temp_inp_size;
                }
                //char * temp_ptr = (char *)tempptr;
                RtlReadEvent* rtlrev_inp_ptr = new RtlReadEvent((uint64_t)&bin_ptr[in_temp_count],(uint32_t)in_temp_size);
                isRespReceived = false;
                generateReadRequest(false, rtlrev_inp_ptr);
                in_temp_inp_size -= in_temp_size;
                in_temp_count += in_temp_size;
            }
            if(in_temp_inp_size == 0)
            {
               isRead = true;
            }
        }
    }

    static int temp_inp_size = 11000;
    static int temp_count=0;
    if(!isWritten /*&& isRespReceived*/)
    {
        if(temp_inp_size > 0)
        {
            int temp_size;
            if(temp_inp_size >= cacheLineSize)
            {
                temp_size = cacheLineSize;
            }else
            {
                temp_size = temp_inp_size;
            }
            //char * temp_ptr = (char *)tempptr;
            RtlWriteEvent* rtlrev_inp_ptr = new RtlWriteEvent((uint64_t)(&bin_ptr[temp_count]),(uint32_t)temp_size,&bin_ptr[temp_count]); 
            //VA_VA_map.insert({(uint64_t)(bin_ptr+temp_count), (uint64_t)&tempptr[temp_count]});
            isRespReceived = false;
            generateWriteRequest(rtlrev_inp_ptr);
            temp_inp_size -= temp_size;
            temp_count += temp_size;
        }
        if(temp_inp_size == 0)
        {
            isWritten = true;
        }
    }

    
    //RtlWriteEvent* rtlrev_inp_ptr = new RtlWriteEvent((uint64_t)&bin_ptr[0],(uint32_t)64,&bin_ptr[0]); 
    //VA_VA_map.insert({(uint64_t)(&bin_ptr[0]), (uint64_t)&tempptr[0]});
    //generateWriteRequest(rtlrev_inp_ptr);


    if(isWritten && isRead)
    {
        static int mcnt = 0;
        static int mcnt1 = 0;
        static uint64_t main_time = 0;
        //cout << "Enabling waves..." << endl;
        
        //cout << "Starting simulation!" << endl;
        if(mcnt < 5)
        {
            if(mcnt==0)
            {
                dut->reset = UInt<1>(1);
            }
            cpu_mem_tick(true, false); 
            mcnt++;
            if(mcnt==5)
            {
                dut->reset = UInt<1>(0);
                dut->io_host_fromhost_bits = UInt<32>(0);
                dut->io_host_fromhost_valid = UInt<1>(0);
            }
        }else if(!dut->io_host_tohost.as_single_word() && main_time < 50000000L)
        {
            //cout << "while" <<endl;
            cpu_mem_tick(true,true);
            main_time++;
            mCycles++;
        }else if(mcnt1 < 10)
        {
            cpu_mem_tick(true, true); 
            mcnt1++;
        }else
        {
            fprintf(dut->RiscvMiniOut,"\n\nIPC: %f\n",25381/(float)mCycles);
            primaryComponentOKToEndSim();
            return true;
        }
    }
    /*
    if(!isStalled) {
        dut->eval(ev.update_registers, ev.verbose, ev.done_reset);
        tickCount++;
    }
    
	if( tickCount >= 30000) {
        if(ev.sim_done) {
            output.verbose(CALL_INFO, 1, 0, "OKToEndSim, TickCount %" PRIu64, tickCount);
            RtlAckEv->setEndSim(true);
            ArielRtlLink->send(RtlAckEv);
            primaryComponentOKToEndSim();  //Tell the SST that it can finish the simulation.
            return true;
        }
	} */
    
    return false;
}


/*Event Handle will be called by Ariel CPU once it(Ariel CPU) puts the input and control signals in the shared memory. Now, we need to modify Ariel CPU code for that.
Event handler will update the input and control signal based on the workload/C program to be executed.
Don't know what should be the argument of Event handle as of now. As, I think we don't need any argument. It's just a requst/call by Ariel CPU to update input and control signals.*/
void Rtlmodel::handleArielEvent(SST::Event *event) {
    /*
    * Event will pick information from shared memory. (What will be the use of Event queue.)
    * Need to insert code for it. 
    * Probably void pointers will be used to get the data from the shared memory which will get casted based on the width set by the user at runtime.
    * void pointers will be defined by Ariel CPU and passed as parameters through SST::Event to the C++ model. 
    * As of now, shared memory is like a scratch-pad or heap which is passive without any intelligent performance improving stuff like TLB, Cache hierarchy, accessing mechanisms(VIPT/PIPT) etc.  
    */
    uint8_t* data;
    unregisterClock(timeConverter, clock_handler);
    ArielComponent::ArielRtlEvent* ariel_ev = dynamic_cast<ArielComponent::ArielRtlEvent*>(event);
    RtlAckEv->setEventRecvAck(true);
    ArielRtlLink->send(RtlAckEv);

    output.verbose(CALL_INFO, 1, 0, "\nVecshiftReg RTL Event handle called \n");

    static bool isPageTableReceived = false;
    if(!isPageTableReceived)
    {
        isPageTableReceived = true;
        memmgr->AssignRtlMemoryManagerSimple(*ariel_ev->RtlData.pageTable, ariel_ev->RtlData.freePages, ariel_ev->RtlData.pageSize);
        memmgr->AssignRtlMemoryManagerCache(*ariel_ev->RtlData.translationCache, ariel_ev->RtlData.translationCacheEntries, ariel_ev->RtlData.translationEnabled);
    }
    //Update all the virtual address pointers in RTLEvent class
    updated_rtl_params = ariel_ev->get_updated_rtl_params();
    inp_ptr = tempptr;//ariel_ev->get_rtl_inp_ptr(); 
    inp_size = ariel_ev->RtlData.rtl_inp_size;
    cacheLineSize = ariel_ev->RtlData.cacheLineSize;

    //Creating Read Event from memHierarchy for the above virtual address pointers
    RtlReadEvent* rtlrev_params = new RtlReadEvent((uint64_t)ariel_ev->get_updated_rtl_params(),(uint32_t)ariel_ev->get_updated_rtl_params_size()); 
    //RtlReadEvent* rtlrev_inp_ptr = new RtlReadEvent((uint64_t)ariel_ev->get_rtl_inp_ptr(),(uint32_t)ariel_ev->get_rtl_inp_size()); 
    RtlReadEvent* rtlrev_ctrl_ptr = new RtlReadEvent((uint64_t)ariel_ev->get_rtl_ctrl_ptr(),(uint32_t)ariel_ev->get_rtl_ctrl_size()); 
    output.verbose(CALL_INFO, 1, 0, "\nVirtual address in handleArielEvent is: %" PRIu64, (uint64_t)ariel_ev->get_updated_rtl_params());

    if(!mem_allocated) {
        size_t size = ariel_ev->get_updated_rtl_params_size() + ariel_ev->get_rtl_inp_size() + ariel_ev->get_rtl_ctrl_size();
        data = (uint8_t*)malloc(size);
        VA_VA_map.insert({(uint64_t)ariel_ev->get_updated_rtl_params(), (uint64_t)data});
        uint64_t index = ariel_ev->get_updated_rtl_params_size()/sizeof(uint8_t);
        VA_VA_map.insert({(uint64_t)inp_ptr, (uint64_t)(data+index)});
        index += ariel_ev->get_rtl_inp_size()/sizeof(uint8_t);
        VA_VA_map.insert({(uint64_t)ariel_ev->get_rtl_ctrl_ptr(), (uint64_t)(data+index)});
        setBaseDataAddress(data);
        setDataAddress(getBaseDataAddress());
        mem_allocated = true;
    }

    //Initiating the read request from memHierarchy
    static bool isFirst = true;
    if(isFirst)
    {
        //isFirst = false;
        generateReadRequest(false, rtlrev_params);
    }
    size_t temp_inp_size = inp_size;
    int temp_count=0;
    /*
    while(temp_inp_size > 0)
    {
        size_t temp_size;
        if(temp_inp_size >= cacheLineSize)
        {
            temp_size = cacheLineSize;
        }else
        {
            temp_size = temp_inp_size;
        }
        char * temp_ptr = (char *)inp_ptr;
        RtlReadEvent* rtlrev_inp_ptr = new RtlReadEvent((uint64_t)&temp_ptr[temp_count],(uint32_t)temp_size); 
        VA_VA_map.insert({(uint64_t)&temp_ptr[temp_count], (uint64_t)(data+temp_count)});
        //generateReadRequest(false,rtlrev_inp_ptr);
        temp_inp_size -= temp_size;
        temp_count += temp_size;
    }*/
   // generateReadRequest(false,rtlrev_ctrl_ptr);
    isStalled = true;
    //sendArielEvent();
}

void Rtlmodel::sendArielEvent() {
     
    RtlAckEv = new ArielComponent::ArielRtlEvent();
    RtlAckEv->RtlData.rtl_inp_ptr = inp_ptr;
    RtlAckEv->RtlData.rtl_inp_size = inp_size;
    ArielRtlLink->send(RtlAckEv);
    return;
}


void Rtlmodel::handleAXISignals(uint8_t tready) {
    axiport->readerFrontend.done = 0;
    axiport->readerFrontend.enable = 1;
    axiport->readerFrontend.length = 64;
    axiport->io_read_tdata = axi_tdata_$next; 
    axiport->io_read_tvalid = axi_tvalid_$next; 
    axiport->io_read_tready = tready; 
    //axiport->cmd_queue.push('r');
}

void Rtlmodel::handleMemEvent(StandardMem::Request* event) {
    StandardMem::ReadResp* read = (StandardMem::ReadResp*)event;
    output.verbose(CALL_INFO, 4, 0, " handling a memory event in RtlModel.\n");
    StandardMem::Request::id_t mev_id = read->getID();

    auto find_entry = pendingTransactions->find(mev_id);
    auto AXI_read_find_entry = AXIReadPendingTransactions->find(mev_id);
    if(find_entry != pendingTransactions->end()) {
        output.verbose(CALL_INFO, 4, 0, "Correctly identified event in pending transactions, removing from list, before there are: %" PRIu32 " transactions pending.\n", (uint32_t) pendingTransactions->size());
       
        int i;
        auto DataAddress = VA_VA_map.find(read->vAddr);
        if(DataAddress != VA_VA_map.end())
            setDataAddress((uint8_t*)DataAddress->second);
        else
        {
            uint64_t temp = (uint64_t)read->vAddr - (uint64_t)&bin_ptr[0];
            if((temp >= 0) && (temp < 11000)) 
            {
                setDataAddress((uint8_t *)(&tempptr[temp]));
//VA_VA_map.insert({(uint64_t)(&bin_ptr[0]), (uint64_t)&tempptr[0]});
            }else
            {
                //setDataAddress((uint8_t *)(&tempptr[0]));
            }
        }
           // output.fatal(CALL_INFO, -1, "Error: DataAddress corresponding to VA: %" PRIu64, read->vAddr);

        //Actual reading of data from memEvent and storing it to getDataAddress
        //output.verbose(CALL_INFO, 1, 0, "\nAddress is: %" PRIu64, (uint64_t)getDataAddress());
        if(read->data.size() <= cacheLineSize)
        {
            for(i = 0; i < read->data.size(); i++)
                getDataAddress()[i] = read->data[i]; 
        }else
        {
            output.verbose(CALL_INFO, 1, 0, "Error: Data size is greater than cacheLineSize");
        }
        if(read->vAddr == (uint64_t)updated_rtl_params) {
            bool* ptr = (bool*)getBaseDataAddress();
            output.verbose(CALL_INFO, 1, 0, "Updated Rtl Params is: %d\n",*ptr);
        }

        pendingTransactions->erase(find_entry);
        pending_transaction_count--;

        if(isStalled && pending_transaction_count == 0) {
            //ev.UpdateRtlSignals((void*)getBaseDataAddress(), dut, sim_cycle);
            tickCount = 0;
            reregisterClock(timeConverter, clock_handler);
            setDataAddress(getBaseDataAddress());
            isStalled = false;
            isRespReceived = true;
            if(isWritten)
            {
                canStartRead = true;
            }
            if(isRead && isWritten)
            {
                for(int i=0;i<10000;i++)
                {
                    if(tempptr[i] != bin_ptr[i])
                    {
                        output.verbose(CALL_INFO, 1, 0, "Error: tempptr[%d] = %d, bin_ptr[%d] = %d\n",i,tempptr[i],i,bin_ptr[i]);
                    }
                }
            }
        }
    }else if(AXI_read_find_entry != AXIReadPendingTransactions->end()) 
    {
        AXIReadPendingTransactions->erase(AXI_read_find_entry);
        if(read->data.size() <= cacheLineSize)
        {
            std::vector<char> temp;
            //char *mptr = (char *)read->vAddr;
            uint64_t mBase = read->vAddr - (uint64_t)bin_ptr;
            for(int i = 0; i < read->data.size(); i++)
                temp.push_back(read->data[i]);//(bin_ptr[mBase + i]);
            rresp.push(mm_rresp_t(curr_ar_id, temp, AXIReadPendingTransactions->size() == 0));
            temp.clear();
        }else
        {
            output.verbose(CALL_INFO, 1, 0, "AXI-Error: Data size is greater than cacheLineSize");
        }   
    } 
    
    else 
        output.fatal(CALL_INFO, -4, "Memory event response to VecShiftReg was not found in pending list.\n");
        
    delete event;
}

void Rtlmodel::commitReadEvent(bool isAXIReadRequest, const uint64_t address,
            const uint64_t virtAddress, const uint32_t length) {
    if(length > 0) {
        StandardMem::Read *req = new StandardMem::Read(address, length, 0, virtAddress);
    
        //memmgr_transactions->insert(std::pair<StandardMem::Request::id_t, int>(req->getID(), flag));
        if(isAXIReadRequest)
        {
            AXIReadPendingTransactions->insert(std::pair<StandardMem::Request::id_t, StandardMem::Request*>(req->getID(), req));
        }else
        {
            pending_transaction_count++;
            pendingTransactions->insert(std::pair<StandardMem::Request::id_t, StandardMem::Request*>(req->getID(), req));
        }
        // Actually send the event to the cache
        cacheLink->send(req);
    }
}

void Rtlmodel::commitWriteEvent(const uint64_t address,
        const uint64_t virtAddress, const uint32_t length, const uint8_t* payload) {

    if(length > 0) {

        std::vector<uint8_t> data;

        if( writePayloads ) {
                data.insert(data.end(), &payload[0], &payload[length]);
                char* buffer = new char[64];
                std::string payloadString = "";
                for(int i = 0; i < length; ++i) {
                    sprintf(buffer, "0x%X ", payload[i]);
                    payloadString.append(buffer);
                }

                delete[] buffer;

                output.verbose(CALL_INFO, 16, 0, "Write-Payload: Len=%" PRIu32 ", Data={ %s } %p\n",
                        length, payloadString.c_str(), (void*)virtAddress);
        } else {
            data.resize(length, 0);
        }
        
        StandardMem::Write *req = new StandardMem::Write(address, length, data, false, 0, virtAddress);
        pending_transaction_count++;
        pendingTransactions->insert( std::pair<StandardMem::Request::id_t, StandardMem::Request*>(req->getID(), req) );

        // Actually send the event to the cache
        cacheLink->send(req);
    }
}

void Rtlmodel::generateReadRequest(bool isAXIReadRequest, RtlReadEvent* rEv) {

    const uint64_t readAddress = rEv->getAddress();
    const uint64_t readLength  = std::min((uint64_t) rEv->getLength(), cacheLineSize); // Trim to cacheline size (occurs rarely for instructions such as xsave and fxsave)

    // NOTE: Physical and virtual addresses may not be aligned the same w.r.t. line size if map-on-malloc is being used (arielinterceptcalls != 0), so use physical offsets to determine line splits
    // There is a chance that the non-alignment causes an undetected bug if an access spans multiple malloc regions that are contiguous in VA space but non-contiguous in PA space.
    // However, a single access spanning multiple malloc'd regions shouldn't happen...
    // Addresses mapped via first touch are always line/page aligned
    /*if(rEv->physaddr == 0) { 
        physaddr = memmgr->translateAddress(readAddress);
    }
    else 
        physaddr = memmgr->translateAddress(readAddress);

    const uint64_t physAddr = physaddr;*/
    const uint64_t physAddr = memmgr->translateAddress(readAddress);
    const uint64_t addr_offset  = physAddr % ((uint64_t) cacheLineSize);

    if((addr_offset + readLength) <= cacheLineSize) {
        output.verbose(CALL_INFO, 4, 0, " generating a non-split read request: Addr=%" PRIu64 " Length=%" PRIu64 "\n",
                            readAddress, readLength);

        // We do not need to perform a split operation

        output.verbose(CALL_INFO, 4, 0, " issuing read, VAddr=%" PRIu64 ", Size=%" PRIu64 ", PhysAddr=%" PRIu64 "\n",
                            readAddress, readLength, physAddr);

        commitReadEvent(isAXIReadRequest, physAddr, readAddress, (uint32_t) readLength);
    } else {
        output.verbose(CALL_INFO, 4, 0, " generating a split read request: Addr=%" PRIu64 " Length=%" PRIu64 "\n",
                            readAddress, readLength);

        // We need to perform a split operation
        const uint64_t leftAddr = readAddress;
        const uint64_t leftSize = cacheLineSize - addr_offset;

        const uint64_t rightAddr = (readAddress + ((uint64_t) cacheLineSize)) - addr_offset;
        const uint64_t rightSize = readLength - leftSize;

        const uint64_t physLeftAddr = physAddr;
        const uint64_t physRightAddr = memmgr->translateAddress(rightAddr);

        output.verbose(CALL_INFO, 4, 0, " issuing split-address read, LeftVAddr=%" PRIu64 ", RightVAddr=%" PRIu64 ", LeftSize=%" PRIu64 ", RightSize=%" PRIu64 ", LeftPhysAddr=%" PRIu64 ", RightPhysAddr=%" PRIu64 "\n",
                            leftAddr, rightAddr, leftSize, rightSize, physLeftAddr, physRightAddr);

        if(((physLeftAddr + leftSize) % cacheLineSize) != 0) {
            output.fatal(CALL_INFO, -4, "Error leftAddr=%" PRIu64 " + size=%" PRIu64 " is not a multiple of cache line size: %" PRIu64 "\n",
                    leftAddr, leftSize, cacheLineSize);
        }

        commitReadEvent(isAXIReadRequest, physLeftAddr, leftAddr, (uint32_t) leftSize);
        commitReadEvent(isAXIReadRequest, physRightAddr, rightAddr, (uint32_t) rightSize);

        statSplitReadRequests->addData(1);
    }

    statReadRequests->addData(1);
    statReadRequestSizes->addData(readLength);
    delete rEv;
}

void Rtlmodel::generateWriteRequest(RtlWriteEvent* wEv) {

    const uint64_t writeAddress = wEv->getAddress();
    const uint64_t writeLength  = std::min((uint64_t) wEv->getLength(), cacheLineSize); // Trim to cacheline size (occurs rarely for instructions such as xsave and fxsave)

    // See note in handleReadRequest() on alignment issues
    const uint64_t physAddr = memmgr->translateAddress(writeAddress);
    const uint64_t addr_offset  = physAddr % ((uint64_t) cacheLineSize);

    // We do not need to perform a split operation
    if((addr_offset + writeLength) <= cacheLineSize) {
        
        output.verbose(CALL_INFO, 4, 0, " generating a non-split write request: Addr=%" PRIu64 " Length=%" PRIu64 "\n",
                            writeAddress, writeLength);


        output.verbose(CALL_INFO, 4, 0, " issuing write, VAddr=%" PRIu64 ", Size=%" PRIu64 ", PhysAddr=%" PRIu64 "\n",
                            writeAddress, writeLength, physAddr);

        if(writePayloads) {
            uint8_t* payloadPtr = wEv->getPayload();
            commitWriteEvent(physAddr, writeAddress, (uint32_t) writeLength, payloadPtr);
        } else {
            commitWriteEvent(physAddr, writeAddress, (uint32_t) writeLength, NULL);
        }
    } else {
        output.verbose(CALL_INFO, 4, 0, " generating a split write request: Addr=%" PRIu64 " Length=%" PRIu64 "\n",
                            writeAddress, writeLength);

        // We need to perform a split operation
        const uint64_t leftAddr = writeAddress;
        const uint64_t leftSize = cacheLineSize - addr_offset;

        const uint64_t rightAddr = (writeAddress + ((uint64_t) cacheLineSize)) - addr_offset;
        const uint64_t rightSize = writeLength - leftSize;

        const uint64_t physLeftAddr = physAddr;
        const uint64_t physRightAddr = memmgr->translateAddress(rightAddr);

        output.verbose(CALL_INFO, 4, 0, " issuing split-address write, LeftVAddr=%" PRIu64 ", RightVAddr=%" PRIu64 ", LeftSize=%" PRIu64 ", RightSize=%" PRIu64 ", LeftPhysAddr=%" PRIu64 ", RightPhysAddr=%" PRIu64 "\n",
                            leftAddr, rightAddr, leftSize, rightSize, physLeftAddr, physRightAddr);

        if(((physLeftAddr + leftSize) % cacheLineSize) != 0) {
            output.fatal(CALL_INFO, -4, "Error leftAddr=%" PRIu64 " + size=%" PRIu64 " is not a multiple of cache line size: %" PRIu64 "\n",
                            leftAddr, leftSize, cacheLineSize);
        }

        if(writePayloads) {
            uint8_t* payloadPtr = wEv->getPayload();
            commitWriteEvent(physLeftAddr, leftAddr, (uint32_t) leftSize, payloadPtr);
            commitWriteEvent(physRightAddr, rightAddr, (uint32_t) rightSize, &payloadPtr[leftSize]);
        } else {
            commitWriteEvent(physLeftAddr, leftAddr, (uint32_t) leftSize, NULL);
            commitWriteEvent(physRightAddr, rightAddr, (uint32_t) rightSize, NULL);
        }
        statSplitWriteRequests->addData(1);
    }

    statWriteRequests->addData(1);
    statWriteRequestSizes->addData(writeLength);
    delete wEv;
}

uint8_t* Rtlmodel::getDataAddress() {
    return dataAddress;
}

uint64_t* Rtlmodel::getAXIDataAddress() {
    return AXIdataAddress;
}


void Rtlmodel::setDataAddress(uint8_t* virtAddress){
    dataAddress = virtAddress;
}

uint8_t* Rtlmodel::getBaseDataAddress(){
    return baseDataAddress;
}

void Rtlmodel::setBaseDataAddress(uint8_t* virtAddress){
    baseDataAddress = virtAddress;
}

//AXI API definitions
void Rtlmodel::AXI_tick( bool reset, 
                         bool ar_valid, uint64_t ar_addr, uint64_t ar_id, uint64_t ar_size, uint64_t ar_len,
                         bool aw_valid, uint64_t aw_addr, uint64_t aw_id, uint64_t aw_size, uint64_t aw_len,
                         bool w_valid, uint64_t w_strb, void *w_data, bool w_last, 
                         bool r_ready, bool b_ready )
{
  bool ar_fire = !reset && ar_valid && AXI_ar_ready();
  bool aw_fire = !reset && aw_valid && AXI_aw_ready();
  bool w_fire = !reset && w_valid && AXI_w_ready();
  bool r_fire = !reset && AXI_r_valid() && r_ready;
  bool b_fire = !reset && AXI_b_valid() && b_ready;

  if (ar_fire) {
    uint64_t start_addr = (ar_addr / word_size) * word_size;
    for (size_t i = 0; i <= ar_len; i++) {
      /*auto dat = */AXI_read(start_addr + i * word_size);
      //rresp.push(mm_rresp_t(ar_id, dat, i == ar_len));
      curr_ar_id=ar_id;
    }
  }

  if (aw_fire) {
    store_addr = aw_addr;
    store_id = aw_id;
    store_count = aw_len + 1;
    store_size = 1 << aw_size;
    store_inflight = true;
  }

  if (w_fire) {
    AXI_write(store_addr, (char*)w_data, w_strb, store_size);
    store_addr += store_size;
    store_count--;

    if (store_count == 0) {
      store_inflight = false;
      bresp.push(store_id);
      assert(w_last);
    }
  }

  if (b_fire)
    bresp.pop();

  if (r_fire)
    rresp.pop();

  cycle++;

  if (reset) {
    while (!bresp.empty()) bresp.pop();
    while (!rresp.empty()) rresp.pop();
    cycle = 0;
  }
}

void Rtlmodel::AXI_write(uint64_t addr, char *data) {
  addr %= this->size;

  char* base = bin_ptr + addr;
  memcpy(base, data, word_size);
}

void Rtlmodel::AXI_write(uint64_t addr, char *data, uint64_t strb, uint64_t size)
{
  strb &= ((1L << size) - 1) << (addr % word_size);
  addr %= this->size;

  char *base = bin_ptr + addr;
  for (int i = 0; i < word_size; i++) {
    //if (strb & 1) base[i] = data[i];
    //strb >>= 1;
    if(strb & 1)
    {
        RtlWriteEvent* rtlrev_inp_ptr = new RtlWriteEvent((uint64_t)(&base[i]),(uint32_t)1,&data[i]); 
        generateWriteRequest(rtlrev_inp_ptr);
        base[i] = data[i];
    }
    strb >>= 1;
  }
}

void Rtlmodel::AXI_read(uint64_t addr)
{
    //TODO: read from memhierarchy and return data
    
  addr %= this->size;

  char *base = bin_ptr + addr;
  RtlReadEvent* axi_readev_ptr = new RtlReadEvent((uint64_t)base,(uint32_t)word_size);
  generateReadRequest(true, axi_readev_ptr);
  //return std::vector<char>(base, base + word_size);
}

void Rtlmodel::cpu_mem_tick(bool verbose, bool done_reset)
{
  dut->io_nasti_aw_ready = UInt<1>(AXI_aw_ready());
  dut->io_nasti_ar_ready = UInt<1>(AXI_ar_ready());
  dut->io_nasti_w_ready = UInt<1>(AXI_w_ready());
  dut->io_nasti_b_valid = UInt<1>(AXI_b_valid());
  dut->io_nasti_b_bits_id = UInt<5>(AXI_b_id());
  dut->io_nasti_b_bits_resp = UInt<2>(AXI_b_resp());
  dut->io_nasti_r_valid = UInt<1>(AXI_r_valid());
  dut->io_nasti_r_bits_id = UInt<5>(AXI_r_id());
  dut->io_nasti_r_bits_resp = UInt<2>(AXI_r_resp());
  dut->io_nasti_r_bits_last = UInt<1>(AXI_r_last());
  memcpy(&dut->io_nasti_r_bits_data, AXI_r_data(), 8);

  dut->eval(true, verbose, done_reset);

    AXI_tick(
    dut->reset, 
    dut->io_nasti_ar_valid, 
    dut->io_nasti_ar_bits_addr.as_single_word(), 
    dut->io_nasti_ar_bits_id.as_single_word(), 
    dut->io_nasti_ar_bits_size.as_single_word(), 
    dut->io_nasti_ar_bits_len.as_single_word(), 
    dut->io_nasti_aw_valid, 
    dut->io_nasti_aw_bits_addr.as_single_word(), 
    dut->io_nasti_aw_bits_id.as_single_word(), 
    dut->io_nasti_aw_bits_size.as_single_word(), 
    dut->io_nasti_aw_bits_len.as_single_word(), 
    dut->io_nasti_w_valid, 
    dut->io_nasti_w_bits_strb.as_single_word(), 
    &dut->io_nasti_w_bits_data, 
    dut->io_nasti_w_bits_last, 
    dut->io_nasti_r_ready, 
    dut->io_nasti_b_ready 
  );
}