#ifndef REMOTE_MEMORY_HEADER
#define REMOTE_MEMORY_HEADER

#include <hls_stream.h>
#include <iostream>
#include <stdlib.h>
#include <ap_int.h>
#include <ap_fixed.h>
#include "communication.hpp"

#define MAX_NUMBER_NODES 12

static int BRAM_REMOTE_MEMORY[MAX_NUMBER_NODES-1][1000000];

extern "C" void remote_ls_memory(hls::stream<ap_uint<256>>& s_axis_tx_meta, hls::stream<ap_uint<64>>& s_axis_tx_data, volatile int* HBM_PTR);
#endif