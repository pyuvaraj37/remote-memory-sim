#include "bank_account_krnl.h"

#define TH 0


void leader_switch(
    hls::stream<ap_uint<256>>& m_axis_tx_meta,
    hls::stream<ap_uint<256>>& hb_increment,
    int board_number,
    int number_of_nodes
) { 
    static int hb = 0;
    hb++;
    int j=0;
    int qpn_tmp=board_number*(number_of_nodes-1);
    LEADER_SWITCH_READ_LOOP: while (j < number_of_nodes){
        if(j!=board_number) {
            if(!m_axis_tx_meta.full()){
                rdma_read(
                    qpn_tmp,
                    HB_BASE_PTR + 4 * j,
                    HB_BASE_PTR + 4 * j,
                    0x4,
                    m_axis_tx_meta
                    );
                j++;
                qpn_tmp++;
            }
        }
        else {
            j++;
        }
    }
    hb_increment.write(hb);
}



/*
    Manages the phase of replication the SMR is in.
*/
void replication_engine_fsm(
    hls::stream<ProposedValue>& propose,
    hls::stream<ap_uint<32>>& prepare_req,
    hls::stream<LogEntry>& prepare_rsp,
    hls::stream<LogEntry>& writeSlot_req,
    hls::stream<ap_uint<32>>& acceptedValue_req,
    hls::stream<updateLocalValue>& acceptedValue_rsq,
    hls::stream<updateLocalValue>& updateLocalValue_req,
    int board_number,
    int number_of_nodes

) {

    #pragma HLS INTERFACE axis port = propose
    #pragma HLS INTERFACE axis port = prepare_req
    #pragma HLS INTERFACE axis port = prepare_rsp
    #pragma HLS INTERFACE axis port = acceptedValue_req
    #pragma HLS INTERFACE axis port = acceptedValue_rsq
    #pragma HLS INTERFACE axis port = updateLocalValue_req

    enum fsmStateType {INIT, LEADER_REPLICA, PROPOSE, PREPARE_REQUEST, PREPARE_REPLY, ACCEPT};
    static fsmStateType state = INIT;
    static bool done = true;
    static int myValue = 0;
    static ap_uint<32> sGroup = 0;
    static ProposedValue pVal;
    static ap_uint<32> newHiPropNum = 0;
    static LogEntry slot;
    static int propValue = 0;

    switch (state) {

        case INIT:
            state = PROPOSE;
            break;

        /*
            If a new proposal arrrives and old proposal is finished, start new proposal.
            else if old proposal is not finished, continue to finish it.
            else wait in this state till new proposal arrives 
        */
        case PROPOSE:
            if (done == true && !propose.empty()) {
                propose.read(pVal);
                myValue = pVal.value;
                sGroup = pVal.syncronizationGroup;
                done = false;
                std::cout << "Value: " << myValue << std::endl; 
                state = PREPARE_REQUEST;
            } else if (!done) {
                state = PREPARE_REQUEST;
            }
            break;

        case PREPARE_REQUEST:
            if (!prepare_req.full()) {
                prepare_req.write(sGroup);
                state = PREPARE_REPLY;
            }
            break;


        case PREPARE_REPLY:
            if (!prepare_rsp.empty()) {
                prepare_rsp.read(slot);
                std::cout << "Value: " << myValue << std::endl; 
                if (slot.valid) {
                    propValue = slot.value;
                } else {
                    propValue = myValue;
                }
                std::cout << "Value: " << propValue << std::endl; 
                newHiPropNum = slot.propVal;
                state = ACCEPT;
            }
            break;

        case ACCEPT:
            if (!writeSlot_req.full() && !updateLocalValue_req.full()) {
                writeSlot_req.write(LogEntry(newHiPropNum, propValue, sGroup));
                if (myValue == propValue) {

                    done = true;
                }
                std::cout << "Value: " << propValue << std::endl; 
                updateLocalValue_req.write(updateLocalValue(propValue, sGroup));
                state = PROPOSE;
            }
            break;

        default:
            break;
    }


}


void log_handler_fsm(
    hls::stream<ap_uint<32>>& prepare_req,
    hls::stream<LogEntry>& prepare_rsp,
    hls::stream<LogEntry>& writeSlot_req,
    hls::stream<ap_uint<32>>& acceptedValue_req,
    hls::stream<updateLocalValue>& acceptedValue_rsq,
    hls::stream<LogEntry>& minPropReadBram_req,
    hls::stream<ap_uint<32>>& minPropReadBram_rsp,
    hls::stream<LogEntry>& readSlotsReadBram_req,
    hls::stream<LogEntry>& readSlotsReadBram_rsp,
    hls::stream<ap_uint<32>>& logReadBram_req,
    hls::stream<ap_uint<64>>& logReadBram_rsp,
    hls::stream<ap_uint<256>>& m_axis_tx_meta,
    hls::stream<ap_uint<64>>& m_axis_tx_data,
    int board_number,
    int number_of_nodes

) {

    enum fsmStateType {REQUEST, READ_MIN_PROP, READ_MIN_PROP_1_RSP, READ_MIN_PROP_2_RSP, WRITE_READ, WRITE_READ_RSP_1, WRITE_READ_RSP_2, WRITE_SLOT, ACCEPT_VALUE_1, ACCEPT_VALUE_2};
    static fsmStateType state = REQUEST;
    static int reads = 0;
    static ap_uint<32> prepare_sGroup = 0, prepare2_sGroup = 0, prepare3_sGroup = 0, accept_sGroup = 0;
    static volatile int minPropFifoIndex[SYNC_GROUPS], slotReadFifoIndex[SYNC_GROUPS], slotAcceptFifoIndex[SYNC_GROUPS];
    static LogEntry logSlot;
    static volatile int fuo[SYNC_GROUPS];
    static ap_uint<32> newMinProp;
    static LogEntry slot;
    static ap_uint<64> value;
    static ap_uint<32> minPropNumber = 0;
    static ap_uint<32> oldMinPropNumber = 0;
    static bool read_slot = true; 

    if (!prepare_req.empty() && !minPropReadBram_req.full()) {
        prepare_req.read(prepare_sGroup);

        int j=0;
        int qpn_tmp=board_number*(number_of_nodes-1);
        VITIS_LOOP_183_1: while (j<number_of_nodes){
            if(j!=board_number && FOLLOWER_LIST[j]) {
                if(!m_axis_tx_meta.full()){
                    int slot = (j < board_number) ? j : j-1;
                    rdma_read(
                        qpn_tmp,
                        LOG_BASE_ADDR + (LOG_ADDR_LEN * prepare_sGroup) + 8 + 4 * (FIFO_LENGTH * (slot) + (minPropFifoIndex[prepare_sGroup]%FIFO_LENGTH)),
                        LOG_BASE_ADDR + (LOG_ADDR_LEN * prepare_sGroup),
                        0x4,
                        m_axis_tx_meta
                        );
                    j++;
                    qpn_tmp++;
                }
            }
            else {
                j++;
            }
        }
        minPropReadBram_req.write(LogEntry(0, minPropFifoIndex[prepare_sGroup], prepare_sGroup));

    }

    if (!minPropReadBram_rsp.empty() && !readSlotsReadBram_req.full() && !minPropReadBram_req.full()) {
        minPropReadBram_rsp.read(minPropNumber);
       if (minPropNumber != oldMinPropNumber || minPropFifoIndex[prepare_sGroup] == 0) {

            minPropFifoIndex[prepare_sGroup]+=1;
            oldMinPropNumber = minPropNumber;
            minPropNumber+=1;
            int j=0;
            int qpn_tmp=board_number*(number_of_nodes-1);
            VITIS_LOOP_215_2: while (j<number_of_nodes){
                if(j!=board_number && FOLLOWER_LIST[j]){
                    if(!m_axis_tx_meta.full() && !m_axis_tx_data.full()){
                        rdma_write(
                            qpn_tmp,
                            0,
                            LOG_BASE_ADDR + (LOG_ADDR_LEN * prepare_sGroup),
                            0x8,
                            minPropNumber,
                            m_axis_tx_meta,
                            m_axis_tx_data
                            );
                        j++;
                        qpn_tmp++;
                    }
                }
                else {
                    j++;
                }
            }
            if (read_slot) {
                j=0;
                qpn_tmp=board_number*(number_of_nodes-1);
                VITIS_LOOP_237_3: while (j<number_of_nodes){
                    if(j!=board_number && FOLLOWER_LIST[j]){
                        if(!m_axis_tx_meta.full()){
                            int slot = (j < board_number) ? j : j-1;
                            rdma_read(
                                qpn_tmp,
                                LOG_BASE_ADDR + LOG_ADDR_LEN * prepare_sGroup + LOG_MIN_PROP_ADDR_LEN + LOG_LOCAL_LOG_ADDR_LEN + 4 * (2 * FIFO_LENGTH * slot + (slotReadFifoIndex[prepare_sGroup]%NUM_SLOTS)),
                                LOG_BASE_ADDR + LOG_ADDR_LEN * prepare_sGroup + LOG_MIN_PROP_ADDR_LEN + 4 * (fuo[prepare_sGroup]%NUM_SLOTS),
                                0x8,
                                m_axis_tx_meta
                                );
                            j++;
                            qpn_tmp++;
                        }
                    }
                    else {
                        j++;
                    }
                }
            }
            readSlotsReadBram_req.write(LogEntry(minPropNumber, slotReadFifoIndex[prepare_sGroup], prepare_sGroup));
        }
        else {
            minPropReadBram_req.write(LogEntry(0, minPropFifoIndex[prepare_sGroup], prepare_sGroup));
        }
    }

    if (!readSlotsReadBram_rsp.empty() && !prepare_rsp.full()) {
        readSlotsReadBram_rsp.read(slot);
        if(!slot.valid) {
            read_slot =false; 
        }
        prepare_rsp.write(slot);
        slotReadFifoIndex[prepare_sGroup]+=2;

    }

    if (!writeSlot_req.empty()) {
        writeSlot_req.read(logSlot);
        accept_sGroup = logSlot.syncGroup;

        ap_uint<64> sendLog;
        sendLog.range(31, 0) = logSlot.propVal;
        sendLog.range(63, 32) = logSlot.value;
        int j=0;
        int qpn_tmp=board_number*(number_of_nodes-1);
        VITIS_LOOP_279_4: while (j<number_of_nodes){
            if(j!=board_number && FOLLOWER_LIST[j]){
                if(!m_axis_tx_meta.full() && !m_axis_tx_data.full()){
                    rdma_write(
                        qpn_tmp,
                        0,
                        LOG_BASE_ADDR + LOG_ADDR_LEN * accept_sGroup + LOG_MIN_PROP_ADDR_LEN + 4 * (fuo[accept_sGroup]%NUM_SLOTS),
                        0x8,
                        sendLog,
                        m_axis_tx_meta,
                        m_axis_tx_data
                        );
                    j++;
                    qpn_tmp++;
                }
            }
            else {
                j++;
            }
        }
        fuo[accept_sGroup]+=2;

    }

}

void smr(
    hls::stream<ap_uint<256>>& smr_update,
    hls::stream<ProposedValue>& proposedValue,
    hls::stream<ap_uint<256>>& m_axis_tx_meta,
    hls::stream<ap_uint<64>>& m_axis_tx_data,
    hls::stream<ap_uint<32>>& logReadBram_req,
    hls::stream<ap_uint<64>>& logReadBram_rsp,
    hls::stream<LogEntry>& readSlotsReadBram_req,
    hls::stream<LogEntry>& readSlotsReadBram_rsp,
    hls::stream<LogEntry>& minPropReadBram_req,
    hls::stream<ap_uint<32>>& minPropReadBram_rsp,
    int board_number,
    int number_of_nodes


) {
    static ap_uint<64> localValues[SYNC_GROUPS];

    static hls::stream<ap_uint<32>> prepare_req;
    static hls::stream<LogEntry> prepare_rsp;
    static hls::stream<LogEntry> writeSlot_req;
    static hls::stream<ap_uint<32>> acceptedValue_req;
    static hls::stream<updateLocalValue> acceptedValue_rsq;
    static hls::stream<updateLocalValue> updateLocalValue_req;

    static updateLocalValue update;
    static int permissible;
    static int query;
    static int counter = 0;

    replication_engine_fsm(
        proposedValue,
        prepare_req,
        prepare_rsp,
        writeSlot_req,
        acceptedValue_req,
        acceptedValue_rsq,
        updateLocalValue_req,
        board_number,
        number_of_nodes

    );

    log_handler_fsm(
        prepare_req,
        prepare_rsp,
        writeSlot_req,
        acceptedValue_req,
        acceptedValue_rsq,
        minPropReadBram_req,
        minPropReadBram_rsp,
        readSlotsReadBram_req,
        readSlotsReadBram_rsp,
        logReadBram_req,
        logReadBram_rsp,
        m_axis_tx_meta,
        m_axis_tx_data,
        board_number,
        number_of_nodes
    );


    if (!updateLocalValue_req.empty() && !smr_update.full()) {
        updateLocalValue_req.read(update);
        localValues[update.syncGroup] = update.value;
        smr_update.write(update.value);
    }


}

void deposit(
    int board_number, 
    int number_of_nodes,
    hls::stream<ap_uint<32>>& broadcast_req, 
    hls::stream<ap_uint<256>>& m_axis_tx_meta, 
    hls::stream<ap_uint<64>>& m_axis_tx_data
) {
    #pragma HLS inline off

    #pragma HLS INTERFACE axis port = broadcast_req
    #pragma HLS INTERFACE axis port = m_axis_tx_meta
    #pragma HLS INTERFACE axis port = m_axis_tx_data
    static ap_uint<32> pValue; 

    if (!broadcast_req.empty()) {
        broadcast_req.read(pValue);
        int j=0; 
        int qpn_tmp=board_number*(number_of_nodes-1);

        //std::cout << "Stock Increment: Item " << pValue.range(31, 16) << " by " << pValue.range(15, 0) << std::endl; 
        while (j<number_of_nodes){
            if(j!=board_number){
                if(!m_axis_tx_meta.full() && !m_axis_tx_data.full()) { 
                    rdma_write(
                        qpn_tmp,
                        0,
                        DEPOSIT_ADDR + (4 * 2 * board_number),
                        0x8,
                        (ap_uint<64>) pValue.range(31, 0),
                        m_axis_tx_meta, 
                        m_axis_tx_data
                        );
                    j++;
                    qpn_tmp++;
                }
            }
            else {
                j++;
            }
        }
    }

}


void mem_manager( 
    volatile int *network_ptr,
    int number_of_nodes,
    int board_number,
    hls::stream<LogEntry>& minPropReadBram_req,
    hls::stream<ap_uint<32>>& minPropReadBram_rsp,
    hls::stream<LogEntry>& readSlotsReadBram_req,
    hls::stream<LogEntry>& readSlotsReadBram_rsp,
    hls::stream<ap_uint<32>>& logReadBram_req,
    hls::stream<ap_uint<64>>& logReadBram_rsp,
    hls::stream<ap_uint<32>>& permissibility_req,
    hls::stream<ap_uint<32>>& permissibility_rsp,
    hls::stream<ap_uint<64>>& update_rsp,
    hls::stream<ap_uint<256>>& hb_increment,
    hls::stream<pkt128>& permission_switch
){
    #pragma HLS INTERFACE axis port = minPropReadBram_req
    #pragma HLS INTERFACE axis port = minPropReadBram_rsp
    #pragma HLS INTERFACE axis port = readSlotsReadBram_req
    #pragma HLS INTERFACE axis port = readSlotsReadBram_rsp
    #pragma HLS INTERFACE axis port = logReadBram_req
    #pragma HLS INTERFACE axis port = logReadBram_rsp
    #pragma HLS INTERFACE axis port = update_rsp


    static ap_uint<512> internal_clock=0;

    static int hbm_tmp=0;

    static int remote_bank_accounts = 0;

    static LogEntry slotIndex, minPropIndex;
    static ap_uint<32> slotRead, psig, ptemp;
    static int log_index[NUM_NODES];

    volatile bool sig = false;
    static ap_uint<64>  s_sig = 0; 
    static int minProp = 0;
    static ap_uint<64> maxPropNumber = 0, update;
    static int propNum, propValue;
    static bool check_throughput_counter = false;
    static bool read_slot = true; 
    static pkt128 new_state;

    static int FUO[SYNC_GROUPS];
    static ap_uint<256> hb; 
    static int HB_values[NUM_NODES];
    static int HB_thresholds[NUM_NODES] = {5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5};
    static bool HB_Status[NUM_NODES] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    internal_clock++;

    #pragma DATAFLOW

    if (!permissibility_req.empty() && !permissibility_rsp.full()) {
        std::cout << "Checking Permisibility" << std::endl; 

        permissibility_req.read(psig);

        std::cout << "Method: " << psig.range(31, 30) << std::endl; 
        switch (psig.range(31, 30))
        {

        //Withdraw and Query
        case 0:
        case 1: {
            ptemp.range(31, 30) = psig.range(31, 30);
            ptemp.range(29, 0) = remote_bank_accounts;
            permissibility_rsp.write(ptemp);
            break; 
        }

        default:
            break;
        }


    }

    if (!minPropReadBram_req.empty() && !minPropReadBram_rsp.full()) {

        minPropReadBram_req.read(minPropIndex);

        VITIS_LOOP_622_2: for (int i = 0; i < number_of_nodes-1; i++) {
            int temp = network_ptr[LOG_BASE_PTR + (LOG_PTR_LEN * minPropIndex.syncGroup) + 2 + FIFO_LENGTH * i + (minPropIndex.value%FIFO_LENGTH)];
            if (temp > minProp) {
                minProp = temp;
            }
        }
        minPropReadBram_rsp.write(minProp);

    }

    if (!readSlotsReadBram_req.empty() && !readSlotsReadBram_rsp.full()) {

        readSlotsReadBram_req.read(slotIndex);
        if (read_slot) {
            maxPropNumber = 0;
            VITIS_LOOP_636_3: for (int i = 0; i < number_of_nodes-1; i++) {
                if (FOLLOWER_LIST[i]) {
                    propNum = network_ptr[LOG_BASE_PTR + (LOG_PTR_LEN * slotIndex.syncGroup) + LOG_MIN_PROP_PTR_LEN + LOG_LOCAL_LOG_PTR_LEN + (2 * i * FIFO_LENGTH) + (slotIndex.value%NUM_SLOTS)];
                    propValue = network_ptr[LOG_BASE_PTR + (LOG_PTR_LEN * slotIndex.syncGroup) + LOG_MIN_PROP_PTR_LEN + LOG_LOCAL_LOG_PTR_LEN + (2 * i * FIFO_LENGTH) + (slotIndex.value%NUM_SLOTS) + 1];
                    if (propNum != 0) {
                        maxPropNumber.range(31, 0) = propNum;
                        maxPropNumber.range(64, 32) = propValue;
                    }
                }
            }

            if (maxPropNumber.range(31,0) != 0) {
                readSlotsReadBram_rsp.write(LogEntry(maxPropNumber.range(31,0), maxPropNumber.range(63, 32), true));
            } else {
                read_slot = false; 
                readSlotsReadBram_rsp.write(LogEntry(slotIndex.propVal, 0));
            }
        } else {
            std::cout << "Value in MEM MNGER: " << slotIndex.propVal<< std::endl; 
            readSlotsReadBram_rsp.write(LogEntry(slotIndex.propVal, 0, false));
        }

    }

    if (internal_clock == 10000) {
        //deposit

        hbm_tmp = 0;
        for (int i=0; i<number_of_nodes; i++){
            if(i!=board_number){
                hbm_tmp+=network_ptr[DEPOSIT_PTR + 2 * i];
            }
        }
        remote_bank_accounts = hbm_tmp;
    
        if (!update_rsp.full()) {
            for (int i = 0; i < SYNC_GROUPS; i++) {
                int index = LOG_BASE_PTR + LOG_PTR_LEN * i + FUO[i]; 
                int log_proposal_number = network_ptr[LOG_BASE_PTR + LOG_PTR_LEN * i + LOG_MIN_PROP_PTR_LEN + FUO[i]];
                int log_operation = network_ptr[LOG_BASE_PTR + LOG_PTR_LEN * i + LOG_MIN_PROP_PTR_LEN + FUO[i] + 1];
                //std::cout << "Checking at HBM_PTR: " << LOG_BASE_PTR + LOG_PTR_LEN * i + LOG_MIN_PROP_PTR_LEN  + FUO[i] << " Prop: " << log_proposal_number << " Value: " << log_operation << std::endl; 
                if (log_proposal_number != 0 && log_operation != 0) {
                    std::cout << "Log change found! Prop: " << log_proposal_number << " Operation: " << log_operation << std::endl; 
                    update = log_operation;
                    update_rsp.write(update);
                    FUO[i]+=2; 
                }
            }
        }

        internal_clock = 0; 
    }


    if (!hb_increment.empty()) {
        hb_increment.read(hb);
        network_ptr[board_number] = hb;

        for (int i = 0; i < NUM_NODES; i++) {
            if (i != board_number && network_ptr[i] > HB_values[i]) {
                HB_values[i] = network_ptr[i]; 
                HB_thresholds[i]++;
            } else {
                HB_thresholds[i]--;
            }
        }
        
        int j=0; 
        int qpn_tmp=board_num*(NUM_NODES-1);
        while (j<NUM_NODES){
            if (j != board_num) {
                if (HB_thresholds[i] < 5 && HB_Status[i]) {
                    HB_Status[i] = false; 
                    std::cout << "Remote Node " << i << " crashed." << std::endl; 
                    new_state.data(15, 0) = qpn_tmp;//qp_num
                    new_state.data(31, 16) = 0;//new_state init
                    new_state.keep(7, 0) = 0xff;
                    new_state.last = 1; 
                    if(!permission_switch.full()) { 
                        permission_switch.write(new_state);
                        j++;
                        qpn_tmp++;
                    }

                } else if (HB_thresholds[i] >= 5 && !HB_Status[i]) {
                    HB_Status[i] = true; 
                    std::cout << "Remote Node " << i << " un-crashed." << std::endl; 
                    new_state.data(15, 0) = qpn_tmp;//qp_num
                    new_state.data(31, 16) = 2;//new_state init
                    new_state.keep(7, 0) = 0xff;
                    new_state.last = 1; 
                    if(!permission_switch.full()) { 
                        permission_switch.write(new_state);
                        j++;
                        qpn_tmp++;
                    }
                } else {
                    j++;
                    qpn_tmp++;
                }
            } else {
                j++;
                qpn_tmp++;
            }
        }

    }

    // #if TH
    // static bool fin_sig_found = false;
    // // VITIS_LOOP_862_6: while (board_number != 0 && !fin_sig_found) {
    // //     if (network_ptr[LOG_BASE_PTR] >= exe) {
    // //         fin_sig_found = true;
    // //     }
    // // }

    // fin_sig_found = false;
    // while (board_number != 1 && !fin_sig_found) {
    //     if (network_ptr[LOG_BASE_PTR + 2 * LOG_PTR_LEN] >= exe) {
    //         fin_sig_found = true;
    //     }
    // }
    // #endif


}


void bank(
    hls::stream<pkt256>& m_axis_tx_meta,
    hls::stream<pkt64>& m_axis_tx_data,
    hls::stream<pkt128>& permission_switch,
    int board_number,
    int* operation_list,
    ap_uint<32>* amount_list,
    int number_of_operations,
    int number_of_nodes,
    int debug_exe,
    volatile int* HBM_PTR
) {

    #pragma HLS INTERFACE axis port = m_axis_tx_meta
    #pragma HLS INTERFACE axis port = m_axis_tx_data

    /*
        Internal streams for SMR module
    */
    static hls::stream<ap_uint<256>> smr_updated;
    static hls::stream<ProposedValue> proposed;
    static hls::stream<ap_uint<256>> smr_tx_meta;
    static hls::stream<ap_uint<64>> smr_tx_data;
    #pragma HLS STREAM depth=8 variable=smr_updated
    #pragma HLS STREAM depth=8 variable=proposed
    #pragma HLS STREAM depth=48 variable=smr_tx_meta
    #pragma HLS STREAM depth=48 variable=smr_tx_data

    /*
        Internal streams for sellItem module
    */
    static hls::stream<ap_uint<32>> stock_req;
    static hls::stream<ap_uint<256>> stock_tx_meta;
    static hls::stream<ap_uint<64>> stock_tx_data;
    #pragma HLS STREAM depth=64 variable=stock_tx_meta
    #pragma HLS STREAM depth=64 variable=stock_tx_data
    
    /*
        Internal streams for openAuction module
    */
    static hls::stream<ap_uint<32>> bid_req;
    static hls::stream<ap_uint<256>> bid_tx_meta;
    static hls::stream<ap_uint<64>> bid_tx_data;
    #pragma HLS STREAM depth=64 variable=bid_tx_meta
    #pragma HLS STREAM depth=64 variable=bid_tx_data

    /*
        Internal streams for leaderswitch
    */
    static hls::stream<ap_uint<256>> leader_switch_tx_meta;
    /* ONLY FOR DEBUGGING*/
    static hls::stream<ap_uint<64>> leader_switch_tx_data;
    #pragma HLS STREAM depth=64 variable=leader_switch_tx_meta

    /*
        HB Increment
    */
    static hls::stream<ap_uint<256>> hb_increment;
    #pragma HLS STREAM depth=64 variable=hb_increment

    /*DEBUG STREAMS*/
    static hls::stream<ap_uint<256>> debug_tx_meta;
    static hls::stream<ap_uint<64>> debug_tx_data;

    /*
        Interal streams between SMR and MEM Manager
    */
    static hls::stream<LogEntry> minPropReadBram_req;
    static hls::stream<ap_uint<32>> minPropReadBram_rsp;
    static hls::stream<LogEntry> readSlotsReadBram_req;
    static hls::stream<LogEntry> readSlotsReadBram_rsp;
    static hls::stream<ap_uint<32>> logReadBram_req;
    static hls::stream<ap_uint<64>> logReadBram_rsp;
    static hls::stream<ap_uint<32>> permissibility_req;
    static hls::stream<ap_uint<32>> permissibility_rsp;
    static hls::stream<ap_uint<64>> update_rsp;
    #pragma HLS STREAM depth=8 variable=minPropReadBram_req
    #pragma HLS STREAM depth=8 variable=minPropReadBram_rsp
    #pragma HLS STREAM depth=8 variable=readSlotsReadBram_req
    #pragma HLS STREAM depth=8 variable=readSlotsReadBram_rsp
    #pragma HLS STREAM depth=8 variable=logReadBram_req
    #pragma HLS STREAM depth=8 variable=logReadBram_rsp
    #pragma HLS STREAM depth=8 variable=permissibility_req
    #pragma HLS STREAM depth=8 variable=permissibility_rsp
    #pragma HLS STREAM depth=8 variable=update_rsp

    ap_uint<32> proposed_value, temp_amount, permiss_rsp;
    ap_uint<64> update; 
    static int counter = 0;
    static int debug_counter = 0;
    static bool done = true;

    static int bank_accounts = 100000;
    static int deposits = 0; 

    std::cout << "Starting account fail accelerator..." << std::endl; 
    RUBIS_MAIN_LOOP: while (debug_counter < debug_exe && counter < number_of_operations) {
    //while (counter < number_of_operations) {
        debug_counter++;
        if (done) {
            std::cout << "Counter: " << counter <<  " Method: " << operation_list[counter] << std::endl; 
            switch (operation_list[counter])
            {

                case 0: {
                    //Withdraw
                    if (!permissibility_req.full()) {
                        temp_amount = amount_list[counter];
                        proposed_value.range(31, 30) = 0; 
                        proposed_value.range(29, 0) = 1;
                        permissibility_req.write(proposed_value);
                        done = false;
                    }
                    break;
                }

                case 1: {
                    //Deposit
                    if (!stock_req.full()) {
                        temp_amount = amount_list[counter];
                        bank_accounts += temp_amount.range(31, 0);
                        deposits += temp_amount.range(31, 0);
                        stock_req.write(deposits);
                        counter++; 
                    }
                    break;
                }

                case 2: {
                    //Query
                    if (!permissibility_req.full()) {
                        temp_amount = amount_list[counter];
                        proposed_value.range(31, 30) = 1; 
                        proposed_value.range(29, 0) = 1;
                        permissibility_req.write(proposed_value);
                        counter++; 
                    }
                    break;
                }

            }

        }

        if (!permissibility_rsp.empty() && !proposed.full()) {
            permissibility_rsp.read(permiss_rsp);
            std::cout << "Permissibility Check" << std::endl; 
            switch(permiss_rsp.range(31, 30)) {
                
                /*    
                    Withrdaw
                    31 - 0 (2 bits) :Withdraw Amount
                */
                case 0: {
                    temp_amount = amount_list[counter];
                    if (bank_accounts + permiss_rsp.range(29, 0) - temp_amount >= 0) {
                        std::cout << "Withdraw: " << temp_amount.range(31, 0) << " Quantity: " << bank_accounts + permiss_rsp.range(29, 0) << std::endl;
                        proposed_value.range(31, 0) = temp_amount;
                        proposed.write(ProposedValue(proposed_value, 0));
                    } else {
                        done = true;
                        counter++; 
                    }
                    break;
                }

                /*    
                    Query
                */
                case 1: {
                    std::cout << "Total: " << bank_accounts + permiss_rsp.range(29, 0) << std::endl;
                    break;
                }

            }

        }

        leader_switch(
            leader_switch_tx_meta,
            hb_increment,
            board_number,
            number_of_nodes
        );

        // smr(
        //     smr_updated,
        //     proposed,
        //     smr_tx_meta,
        //     smr_tx_data,
        //     logReadBram_req,
        //     logReadBram_rsp,
        //     readSlotsReadBram_req,
        //     readSlotsReadBram_rsp,
        //     minPropReadBram_req,
        //     minPropReadBram_rsp,
        //     board_number,
        //     number_of_nodes
        // );

        // deposit(
        //     board_number, 
        //     number_of_nodes,
        //     stock_req, 
        //     stock_tx_meta, 
        //     stock_tx_data
        // );

        // meta_merger(
        //     smr_tx_meta,
        //     stock_tx_meta,
        //     m_axis_tx_meta
        // );

        // data_merger(
        //     smr_tx_data,
        //     stock_tx_data,
        //     m_axis_tx_data
        // );

        remote_ls_memory(
            leader_switch_tx_meta, 
            leader_switch_tx_data,
            HBM_PTR
        ); 

        mem_manager(
            HBM_PTR,
            number_of_nodes,
            board_number,
            minPropReadBram_req,
            minPropReadBram_rsp,
            readSlotsReadBram_req,
            readSlotsReadBram_rsp,
            logReadBram_req,
            logReadBram_rsp,
            permissibility_req,
            permissibility_rsp,
            update_rsp,
            hb_increment,
            permission_switch
        );


        if (!smr_updated.empty()) {
            ap_uint<256> temp;
            smr_updated.read(temp);
            done = true;
            counter++;

            switch (temp.range(31, 30))
            {
            case 0:
                std::cout << "Withdraw Amount: " << temp.range(29, 0) << std::endl; 
                bank_accounts -= temp.range(31, 0);
                break;

            default:
                break;
            }

        }


        if (!update_rsp.empty()) {

            update_rsp.read(update);
            std::cout << "Updaing from Log! Method: " << update.range(31, 30) << " Operation: " << update.range(29, 0) << std::endl; 
            switch (update.range(31, 30))
            {
            case 0:
                std::cout << "Withdraw Amount: " << update.range(29, 0) << std::endl; 
                bank_accounts -= update.range(31, 0);
                break;

            default:
                break;
            }
        }

    }

}

extern "C" void account_fail_krnl(
    hls::stream<pkt256>& m_axis_tx_meta,
    hls::stream<pkt64>& m_axis_tx_data,
    hls::stream<pkt128>& permission_switch,
    hls::stream<pkt64>& s_axis_tx_status,
    volatile int* HBM_PTR,
    int board_number,
    int* operation_list,
    ap_uint<32>* amount_list,
    int number_of_operations,
    int number_of_nodes,
    int debug_exe
) {

    #pragma HLS INTERFACE m_axi port=operation_list bundle=gmem0
    #pragma HLS INTERFACE m_axi port=amount_list bundle=gmem0
    #pragma HLS INTERFACE m_axi port=HBM_PTR bundle=gmem1
    #pragma HLS INTERFACE axis port = m_axis_tx_meta
    #pragma HLS INTERFACE axis port = m_axis_tx_data
    #pragma HLS INTERFACE axis port = s_axis_tx_status

    pkt64 status; 

    if (!s_axis_tx_status.empty()) {
        s_axis_tx_status.read(status);
    }

    #pragma HLS dataflow
    bank(
        m_axis_tx_meta,
        m_axis_tx_data,
        permission_switch,
        board_number,
        operation_list,
        amount_list,
        number_of_operations,
        number_of_nodes,
        debug_exe,
        HBM_PTR
    );

}