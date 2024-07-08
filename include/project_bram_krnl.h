#ifndef UTIL_HEADER
#define UTIL_HEADER

#include <hls_stream.h>
#include <iostream>
#include <stdlib.h>
#include <ap_int.h>
#include <ap_fixed.h>
#include "communication.hpp"
#include "remote_memory.h"
#include "hash_table.hpp"

#pragma once

const int NUM_NODES = 12; 
const int SYNC_GROUPS = 1; 

// Need for QP info
const ap_uint<32> BASE_IP_ADDR = 0xe0d4010b;
const uint32_t UDP = 0x000012b7;

// Written by Leader Switch and read by Log Handler
static bool FOLLOWER_LIST[NUM_NODES-1] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

//25% write log size (smaller % can use this as well)
const int NUM_SLOTS = 750000 * 2; 
const int FIFO_LENGTH = 5;

// Constants for HeartBeat Memory
// Only 1 HB regardless of number of sync groups
const int HB_BASE_PTR = 0;
const int HB_BASE_ADDR = 0;
const int HB_PTR_LEN = NUM_NODES; 
const int HB_ADDR_LEN = 4 * HB_PTR_LEN; 

// Constants for replication logs
// Scales with numberof sync groups
const int LOG_BASE_PTR = HB_PTR_LEN; 
const int LOG_BASE_ADDR = HB_ADDR_LEN; 
const int LOG_MIN_PROP_PTR_LEN = 2 + (NUM_NODES-1) * FIFO_LENGTH; // local heartbeat and remote heartbeat queue
const int LOG_MIN_PROP_ADDR_LEN = 4 * LOG_MIN_PROP_PTR_LEN; 
const int LOG_LOCAL_LOG_PTR_LEN = NUM_SLOTS; // local log 
const int LOG_LOCAL_LOG_ADDR_LEN = 4 * LOG_LOCAL_LOG_PTR_LEN; 
const int LOG_REMOTE_LOG_QUEUE_PTR_LEN = 2 * (NUM_NODES-1) * FIFO_LENGTH;
const int LOG_REMOTE_LOG_QUEUE_ADDR_LEN = 4 * LOG_REMOTE_LOG_QUEUE_PTR_LEN; 
const int LOG_PTR_LEN = LOG_MIN_PROP_PTR_LEN + LOG_LOCAL_LOG_PTR_LEN + LOG_REMOTE_LOG_QUEUE_PTR_LEN; 
const int LOG_ADDR_LEN = LOG_PTR_LEN * 4; 

const int BROADCAST_EMPLOYEE_PTR = HB_PTR_LEN + SYNC_GROUPS * LOG_PTR_LEN;
const int BROADCAST_EMPLOYEE_ADDR = HB_ADDR_LEN + SYNC_GROUPS * LOG_ADDR_LEN;
const int BROADCAST_EMPLOYEE_LEN = 2000; 

struct LocalMemOp {
    bool read; 
    ap_uint<32> index;
    ap_uint<32> value; 
    LocalMemOp()
        :read(false) {}
    LocalMemOp(bool r, ap_uint<32> i, ap_uint<32> v)
        :read(r), index(i), value(v) {}
};

struct ProposedValue {
    ap_uint<32> value; 
    ap_uint<32> syncronizationGroup;
    ProposedValue()
        :value(0), syncronizationGroup(0){}
    ProposedValue(int v, ap_uint<32> s)
        : value(v), syncronizationGroup(s) {}
};

struct updateLocalValue {
    ap_uint<32> value; 
    ap_uint<32> syncGroup;
    updateLocalValue()
        :value(0), syncGroup(0) {}
    updateLocalValue(ap_uint<32> v, ap_uint<32> s)
        :value(v), syncGroup(s) {} 
};

struct LogEntry
{
    ap_uint<32> propVal;
    ap_uint<32> value;
	ap_uint<32> fuo;
	ap_uint<32> syncGroup; 
	bool valid; 
    LogEntry()
        :valid(false) {}
    LogEntry(ap_uint<32> p, ap_uint<32> f)
        :propVal(p), fuo(f) {}
    LogEntry(ap_uint<32> s)
        :valid(false), syncGroup(s) {}
    LogEntry(ap_uint<32> p, ap_uint<32> v, ap_uint<32> s) 
        :propVal(p), value(v), syncGroup(s) {}
    LogEntry(ap_uint<32> p, ap_uint<32> v, bool va) 
        :propVal(p), value(v), valid(va) {}
};


void stream_2_to_1(
    hls::stream<ap_uint<256>>& a_tx_meta,
    hls::stream<ap_uint<256>>& b_tx_meta,
    hls::stream<ap_uint<64>>& a_tx_data,
    hls::stream<ap_uint<64>>& b_tx_data,
    hls::stream<ap_uint<256>>& d_tx_meta,
    hls::stream<ap_uint<64>>& d_tx_data
) {

    static ap_uint<256> temp_val_256; 
    static ap_uint<64> temp_val_64; 

    if (!a_tx_meta.empty()) {
        a_tx_meta.read(temp_val_256);
        d_tx_meta.write(temp_val_256);
    } else if (!b_tx_meta.empty()) {
        b_tx_meta.read(temp_val_256);
        d_tx_meta.write(temp_val_256);
    } 

    if (!a_tx_data.empty()) {
        a_tx_data.read(temp_val_64);
        d_tx_data.write(temp_val_64);
    } else if (!b_tx_data.empty()) {
        b_tx_data.read(temp_val_64);
        d_tx_data.write(temp_val_64);
    } 

}


void meta_merger(
    hls::stream<ap_uint<256>>& a_tx_meta,
    hls::stream<ap_uint<256>>& b_tx_meta,
    hls::stream<pkt256>& d_tx_meta
) {
    #pragma HLS inline off
    #pragma HLS pipeline II=1

    static ap_uint<256> temp_val_256; 
    static pkt256 temp_pkt_256; 

    if (!a_tx_meta.empty() && !d_tx_meta.full()) {
        a_tx_meta.read(temp_val_256);
        temp_pkt_256.data(255, 0) = temp_val_256.range(255, 0); 
        d_tx_meta.write(temp_pkt_256);
    } else if (!b_tx_meta.empty() && !d_tx_meta.full()) {
        b_tx_meta.read(temp_val_256);
        temp_pkt_256.data(255, 0) = temp_val_256.range(255, 0); 
        d_tx_meta.write(temp_pkt_256);  
    } 

}

void data_merger(
    hls::stream<ap_uint<64>>& a_tx_data,
    hls::stream<ap_uint<64>>& b_tx_data,
    hls::stream<pkt64>& d_tx_data
) {

    #pragma HLS inline off
    #pragma HLS pipeline II=1
    static ap_uint<64> temp_val_64; 
    static pkt64 temp_pkt_64; 

    if (!a_tx_data.empty() && !d_tx_data.full()) {
        a_tx_data.read(temp_val_64);
        temp_pkt_64.data(63, 0) = temp_val_64.range(63, 0); 
        temp_pkt_64.keep(7, 0) = 0xff;
        temp_pkt_64.last = 1; 
        d_tx_data.write(temp_pkt_64);
        
    } else if (!b_tx_data.empty() && !d_tx_data.full()) {
        b_tx_data.read(temp_val_64);
        temp_pkt_64.data(63, 0) = temp_val_64.range(63, 0); 
        temp_pkt_64.keep(7, 0) = 0xff;
        temp_pkt_64.last = 1; 
        d_tx_data.write(temp_pkt_64);       

    } 

}


void rdma_read(
    int s_axi_lqpn,
    ap_uint<64> s_axi_laddr,
    ap_uint<64> s_axi_raddr,
    int s_axi_len,
    hls::stream<ap_uint<256>>& m_axis_tx_meta
){
    //#pragma HLS dataflow
    #pragma HLS inline off
    #pragma HLS pipeline II=1
    #pragma HLS INTERFACE axis port = m_axis_tx_meta
    
    ap_uint<256> tx_meta;
    ap_uint<64> tx_data;

    /*RDMA OP*/
    tx_meta.range(2,0) = 0x00000000; 
    /*lQPN*/
    tx_meta.range(26,3) = s_axi_lqpn; 
    /*
    lAddr
    */
    tx_meta.range(74, 27) = s_axi_laddr; 
    /*rAddr*/
    tx_meta.range(122, 75) = s_axi_raddr; 
    //+(itt*4)
    /*len*/
    tx_meta.range(154, 123) = s_axi_len;
    m_axis_tx_meta.write(tx_meta);
    
}

void rdma_write(
    int s_axi_lqpn,
    ap_uint<64> s_axi_laddr,
    ap_uint<64> s_axi_raddr,
    int s_axi_len,
    ap_uint<64>  write_value,
    hls::stream<ap_uint<256>>& m_axis_tx_meta, 
    hls::stream<ap_uint<64>>& m_axis_tx_data
){
    //#pragma HLS dataflow
    #pragma HLS inline off
    #pragma HLS pipeline II=1
    #pragma HLS INTERFACE axis port = m_axis_tx_meta
    #pragma HLS INTERFACE axis port = m_axis_tx_data
    
    ap_uint<256> tx_meta;
    ap_uint<64> tx_data;

    /*RDMA OP*/
    tx_meta.range(2,0) = 0x00000001; 
    /*lQPN*/
    tx_meta.range(26,3) = s_axi_lqpn; 
    /*
    lAddr
    if 0 writes from tx_data. 
    */
    tx_meta.range(74, 27) = s_axi_laddr; 
    /*rAddr*/
    tx_meta.range(122, 75) = s_axi_raddr; 
    //+(itt*4)
    /*len*/
    tx_meta.range(154, 123) = s_axi_len;
    

    m_axis_tx_meta.write(tx_meta);

    //Write data only if laddr is 0
    if (s_axi_laddr == 0) {
        tx_data.range(63, 0) = write_value;
        //tx_data.keep(7, 0) = 0xff;
        //tx_data.last = 1; 
        
        m_axis_tx_data.write(tx_data);
    }

}

#endif