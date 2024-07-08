#include "app_krnl.h"

void app(
    hls::stream<bool>& doorbell,
    hls::stream<int>& load_op
) {

    std::cout << "App" << std::endl; 

}

void queue_handler(
    hls::stream<bool>& doorbell,
    hls::stream<int>& load_op,
    hls::stream<ap_uint<256>>& m_axis_tx_meta,
    hls::stream<ap_uint<64>>& m_axis_tx_data
) {
    std::cout << "Queue Handler" << std::endl; 

    if(!m_axis_tx_meta.full()) {
        m_axis_tx_meta.write(1);
    }

    if(!m_axis_tx_data.full()) {
        m_axis_tx_data.write(1);
    }

}

extern "C" void app_krnl(
    volatile int *HBM_PTR
) {

    hls::stream<bool> doorbell;
    hls::stream<int> load_op;
    hls::stream<ap_uint<256>> m_axis_tx_meta;
    hls::stream<ap_uint<64>> m_axis_tx_data;

    #pragma HLS dataflow
    app(
        doorbell,
        load_op
    );

    queue_handler(
        doorbell,
        load_op,
        m_axis_tx_meta,
        m_axis_tx_data
    );

    remote_memory(
        m_axis_tx_meta,
        m_axis_tx_data,
        HBM_PTR        
    );

}