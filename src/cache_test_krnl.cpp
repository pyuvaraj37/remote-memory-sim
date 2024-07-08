#include "ap_int.h"
extern "C" void cache_test_krnl(
    int* operation_list,
    ap_uint<32>* amount_list,
    int number_of_operations,
    int* output
) {

    #pragma HLS INTERFACE m_axi port=operation_list bundle=gmem0
    #pragma HLS INTERFACE m_axi port=amount_list bundle=gmem0
    #pragma HLS cache port=operation_list lines=8 depth=8
    #pragma HLS cache port=amount_list lines=8 depth=8
    #pragma HLS INTERFACE m_axi port=output bundle=gmem1

    int index = 0;
    int operation;
    ap_uint<32> amount;

    while (index < number_of_operations) {
        operation = operation_list[index];
        amount = amount_list[index];
        output[index] = operation + amount; 
        index++; 
    }
}

