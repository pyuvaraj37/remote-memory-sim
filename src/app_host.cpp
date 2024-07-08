/**********
Copyright (c) 2019, Xilinx, Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
may be used to endorse or promote products derived from this software
without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**********/
#include "host.h"
#include <vector>
#include <chrono>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> 
#include <cstdlib> 
#include <iostream> 
#include <fstream>

#define DATA_SIZE 62500000

//Set IP address of FPGA
#define IP_ADDR 0x0A01D498
#define BOARD_NUMBER 0
#define ARP 0x0A01D498

const int LOG_SIZE = 2 + 55 + 2 * 90000 + 110; 
const int HB_START = 0; 
const int HB_END = 12; 
const int SYNC_GROUPS = 2; 

const int PROP_START = HB_END;
const int PROP_END = PROP_START + 2 + 55; 

const int LOCAL_LOG_START = PROP_END; 
const int LOCAL_LOG_END = LOCAL_LOG_START + 2 * 90000; 

const int LOG_FIFO_START = LOCAL_LOG_END; 
const int LOG_FIFO_END = LOG_FIFO_START + 110; 

const int CRDT_START = HB_END + LOG_SIZE * SYNC_GROUPS;
const int CRDT_END = CRDT_START + 200; 

void wait_for_enter(const std::string &msg) {
    std::cout << msg << std::endl;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

int main(int argc, char **argv) {

    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <XCLBIN File> <NUM_NODES> <NUM_OPS> <WRITE_%>" << std::endl;
        return EXIT_FAILURE;
    }
    /*===============================================================Handle INPUT ARGs===============================================================*/
    std::string binaryFile = argv[1];
    int NUM_NODES = 2;
    int ID = 0;
    /*===============================================================Program FPGA with input bitstream===============================================================*/

    cl_int err;
    cl::CommandQueue q;
    cl::Context context;
    cl::Kernel user_kernel;
    cl::Kernel remote_memory_kernel;

    //OPENCL HOST CODE AREA START
    //Create Program and Kernel
    auto devices = get_xil_devices();

    // read_binary_file() is a utility API which will load the binaryFile
    // and will return the pointer to file buffer.
    auto fileBuf = read_binary_file(binaryFile);
    cl::Program::Binaries bins{{fileBuf.data(), fileBuf.size()}};
    int valid_device = 0;
    for (unsigned int i = 0; i < devices.size(); i++) {
        auto device = devices[i];
        // Creating Context and Command Queue for selected Device
        OCL_CHECK(err, context = cl::Context(device, NULL, NULL, NULL, &err));
        OCL_CHECK(err,
                  q = cl::CommandQueue(
                      context, {device}, CL_QUEUE_PROFILING_ENABLE, &err));

        std::cout << "Trying to program device[" << i
                  << "]: " << device.getInfo<CL_DEVICE_NAME>() << std::endl;
                  cl::Program program(context, {device}, bins, NULL, &err);
        if (err != CL_SUCCESS) {
            std::cout << "Failed to program device[" << i
                      << "] with xclbin file!\n";
        } else {
            std::cout << "Device[" << i << "]: program successful!\n";
            // OCL_CHECK(err,
            //           remote_memory_kernel = cl::Kernel(program, "remote_memory", &err));
            OCL_CHECK(err,
                      user_kernel = cl::Kernel(program, "app_krnl", &err));
            valid_device++;
            break; // we break because we found a valid device
        }
    }
    if (valid_device == 0) {
        std::cout << "Failed to program any device found, exit!\n";
        exit(EXIT_FAILURE);
    }
    
    //wait_for_enter("\nPress ENTER to continue after setting up ILA trigger...");

    /*===============================================================Init and start Network Kernel===============================================================*/    
    auto size = DATA_SIZE;
    auto vector_size_bytes = sizeof(int) * size;
    std::vector<int, aligned_allocator<int>> network_ptr0(size);


    // network_ptr0[LOCAL_LOG_START] = 1;
    // network_ptr0[LOCAL_LOG_START + 1] = 0xa022ffdd ;

    OCL_CHECK(err,
              cl::Buffer buffer_network(context,
                                   CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE,
                                   vector_size_bytes,
                                   network_ptr0.data(),
                                   &err));

    /*===============================================================Init and Start User kernel===============================================================*/

    OCL_CHECK(err, err = user_kernel.setArg(0, buffer_network));
    
    printf("Host->Device user kernel... \n");
    OCL_CHECK(err, err = q.enqueueMigrateMemObjects({buffer_network}, 0 /* 0 means from host*/));
    OCL_CHECK(err, err = q.finish());

    double durationUs = 0.0;
    
    printf("enqueue user kernel... \n");
    auto start = std::chrono::high_resolution_clock::now();
    OCL_CHECK(err, err = q.enqueueTask(user_kernel));
    OCL_CHECK(err, err = q.finish());
    auto end = std::chrono::high_resolution_clock::now();
    durationUs = (std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count() / 1000.0);

    /*===============================================================OUTPUT===============================================================*/
    printf("Host<-Device user kernel... \n");
    OCL_CHECK(err, err = q.enqueueMigrateMemObjects({buffer_network}, CL_MIGRATE_MEM_OBJECT_HOST));
    OCL_CHECK(err, err = q.finish());

    printf("durationUs:%f\n",durationUs);

    std::cout << "EXIT recorded" << std::endl;
}

