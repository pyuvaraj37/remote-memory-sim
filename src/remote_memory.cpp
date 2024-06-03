#include "remote_memory.h"

#define DEBUG_MEMORY 1


/*
    Simulates Remote Memory of mulitple nodes.
    Assumed node requesting is Node ID 0, QPNs are set with this assumption. 

    Free Running Kernel

    RDMA Reads - Read value from BRAM and write into HBM. 
    RDMA Write - Write value into BRAM. 

*/
extern "C" void remote_memory(
    hls::stream<ap_uint<256>>& s_axis_tx_meta, 
    hls::stream<ap_uint<64>>& s_axis_tx_range,
    volatile int* HBM_PTR
) {
    #pragma HLS INTERFACE axis port = s_axis_tx_meta
    #pragma HLS INTERFACE axis port = s_axis_tx_range

    static ap_uint<256> temp_pkt_256; 
    static ap_uint<64> temp_pkt_64; 

    int qpn, len;

    //Since this memory is on the remote side these will be fliped. 
    ap_uint<64> local_address, remote_address; 
    //std::cout << "Remote Memory start" << std::endl; 
    #pragma HLS DATAFLOW

    //Triger when a META pkt arrives
    while (!s_axis_tx_meta.empty()) {
        #if DEBUG_MEMORY
        std::cout << "Received packet..." << std::endl; 
        #endif
        s_axis_tx_meta.read(temp_pkt_256);

        switch(temp_pkt_256.range(2, 0)) {

            //RDMA READ
            case 0: {
                qpn = temp_pkt_256.range(26, 3);
                remote_address = temp_pkt_256.range(74, 27);
                local_address = temp_pkt_256.range(122, 75);
                len = temp_pkt_256.range(154, 123);

                remote_address /= 4; 
                local_address /= 4; 
                len /= 4; 
                #if DEBUG_MEMORY
                std::cout << "READING " << len << " int(s) at " << local_address << " WRITING to " << remote_address << " FROM Node" << qpn+1 << std::endl; 
                #endif
                for (int i = 0; i < len; i++) {
                    HBM_PTR[remote_address + i] = BRAM_REMOTE_MEMORY[qpn][local_address + i];
                }
                break; 
            }

            //RDMA WRITE
            case 1: {
                qpn = temp_pkt_256.range(26, 3);
                remote_address = temp_pkt_256.range(74, 27);
                local_address = temp_pkt_256.range(122, 75);
                len = temp_pkt_256.range(154, 123);

                remote_address /= 4; 
                local_address /= 4; 
                len /= 4; 

                s_axis_tx_range.read(temp_pkt_64);

                #if DEBUG_MEMORY
                std::cout << "WRITING " << len <<  " ints at " << local_address << " IN Node" << qpn+1 << std::endl;
                std::cout << temp_pkt_64 << std::endl;
                #endif
                for (int i = 0; i < len; i++) {
                    BRAM_REMOTE_MEMORY[qpn][local_address + i] = temp_pkt_64.range(32 * i + 31 , 32 * i);
                }
                break; 
            }

            //Potentially add more Verbs in future

        }

    }

}