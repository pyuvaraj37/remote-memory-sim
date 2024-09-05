#include "load_krnl.h"

extern "C" void load_krnl(
    int* operation_list,
    ap_uint<32>* amount_list,
    int number_of_operations,
    hls::stream<int>& operation_stream,
    hls::stream<ap_uint<32>>& amount_stream
) {

    #pragma HLS INTERFACE m_axi port=operation_list bundle=gmem0
    #pragma HLS INTERFACE m_axi port=amount_list bundle=gmem0
    #pragma HLS INTERFACE axis port = operation_stream
    #pragma HLS INTERFACE axis port = amount_stream

    static int counter = 0;
    while (counter < number_of_operations) {
        if (!operation_stream.full() && !amount_stream.full()) {
            operation_stream.write(operation_list[counter]);
            amount_stream.write(amount_list[counter]);
            counter++; 
        }
    }

}

