#ifndef _STACK_PATCH_LOG_H_
#define _STACK_PATCH_LOG_H_

/*=======================================MODULE NAME====================================*/
//#define YOUR_LOG_INDEX        0
/*======================================================================================*/

/*=======================================LOWERSTACK=====================================*/
#define LOWERSTACK_START         1000
#define STACK_PATCH_ENTER_BT_MODE               1001 //">>> Stack Patch git_ver:%d.%d.%d.%d, commit-id:%x"

//legacy
#define SNIFF_END_LOG            1051 //"[SNIFF END INT] ce:0x%x, piconet clk: 0x%x"

//le
#define PRIVACY_INITIATOR_PEER_RPA_LOG      1301 //"[PRIVACY] LE Enhanced Connection Complete Event Change peer_addr_type:%d, peer_rpa:0x%x%x%x, is_rpa:%d"
#define LE_MSG_HIT_CONN_ADV_NO_GNT_LOG      1302 //"[LL] hit_conn_adv conflict with conn update instant-1-2, switch to conn: entry_id:%d, cur:%d, counter:%d, next_ap:%d, instant:%d"

//le ISO
#define LE_MSG_BIS_TOGGLE_EMPTY_OPT         1601 //"[ISO] BIS enable bis_tx_empty_opt:%d"
#define LE_MSG_BIS_FW_CAL_FIRST_FREERUN_CLK 1602 //"[ISO] BIS FW cal freerun clk, clock latch: clock:%d, bis_uid:%d, sync_clk:%d, pn_clk:(%d, %d, %d), diff:%d, cam_ap_slot:%d, cam_ap_us:%d, ref_ap:%d"
#define LE_MSG_BIS_GET_HW_FREERUN_CLK       1603 //"[ISO] BIS get hw freerun clk %d, return ap (freerun clk) %d"
#define LE_CHECK_CIS_INTERLEAVED            1604 //"[ISO] evtcnt: %d.%d interval: %d.%d offset: %d.%d ap_diff: %d policy: %d sub_interval: %d"
#define LE_BIS_SET_TX_POWER_LOG             1605 //"[ISO][TXGAIN_SET] LE BIS: bis_uid: %d, big_uid: %d, rf_mode: %d, phy_rate: %d, txgain_index=0x%x""
#define LE_CIS_SET_TX_POWER_LOG             1606 //"[ISO][TXGAIN_SET] LE CIS: cis_uid: %d, rf_mode: %d, phy_rate: %d, txgain_index=0x%x"
#define LE_MSG_BIS_MONITOR_RSSI_UID_WARNING 1607 //"[ISO] Warning! BIS monitor rssi, The bis_uid %d is over the original design value %d"
#define LE_MSG_BIS_MONITOR_RSSI_STORE       1608 //"[ISO] BIS monitor rssi %d, rssi_sum %d, rx_cnt %d, bis_uid %d"

//proprietary
#define PPT_RD_REG                          2000 //"[PPT]RD offset:0x%x, value:0x%x"
#define PPT_WR_REG                          2001 //"[PPT]WR offset:0x%x, value:0x%x, mask:0x%x"
#define PPT_HOST_CMD                        2002 //"[PPT]-->CMD opcode:0x%x subcmd:0x%x ret status:0x%x len:%d, 0x%x %x %x %x, %x %x %x %x, %x %x %x %x, %x %x %x %x"
#define PPT_INVALID_HOST_CMD                2003 //"[PPT]Error: Invalid host cmd subcmd:%d"
#define PPT_FLOW_GO                         2004 //"[PPT]Set flow GO: %d"
#define PPT_FLOW_STOP                       2005 //"[PPT]Set flow STOP: %d"
#define PPT_RESERVED_BUFFER_CNT             2006 //"[PPT]Reserved buffer count %d"
#define PPT_HW_INSTRUCTION_CODE_ERROR       2007 //"[PPT] HW Instruction Code Error (code=%d)"
#define PPT_HW_INSTRUCTION_CODE             2008 //"[PPT] Instruction %d"
#define PPT_FW_UPDATE_RD_POINT              2009 //"[PPT]BZDMA segment valid bit: 0x%x, entry:%d"
#define PPT_PEND_TIMERTASK_FIAL             2010 //"[PPT]Error: pend timer task fail"
#define PPT_TX_INT_INFO                     2011 //"[PPT]tx_int entry:%d no_tx:%d, empty:%d, tx_ptr:%d s0:0x%x, length:%d, s1:0x%x, en0 tx_cnt:%d, en1 tx_cnt:%d"
#define PPT_MAX_LENGTH_WARN                 2012 //"[PPT]Error: Rx pkt reached max length. entry:%d kept:%d, fw_setting:%d, len+a:%d, len:%d"
#define PPT_CRC_ERROR                       2013 //"[PPT]Rx crc error pkt entry, crc: 0x%x"
#define PPT_RX_INT_INFO                     2014 //"[PPT]rx_int entry:%d rx_hit:%d, crc_err:%d, rx_time_out:%d, ack:%d, s0:0x%x, length:0x%x, s1:0x%x, en0 rx_cnt:%d, en1 rx_cnt:%d, rssi:%d"
#define PPT_SEND_SIG_TO_RX_TASK_FAIL        2015 //"[PPT]Error: proprietary_handle_rxfifo_in_task signal:0x%x"
#define PPT_PTX_INT                         2016 //"[PPT]ptx_int entry 0 tx_cnt:%d rx_cnt:%d"
#define PPT_PRX_INT                         2017 //"[PPT]prx_int entry 0 tx_cnt:%d rx_cnt:%d, entry tx_cnt:%d rx_cnt:%d"
#define PPT_TX_COMPLETE_INVALID_PKT_TYPE    2018 //"[PPT]Error: tx complete invalid pkt type:%d"
#define PPT_RX_PKT_SEND_DATA_IND            2019 //"[PPT]rx pkt send data indication type:%d, len:%d, buff:0x%x"
#define PPT_RX_PKT_SEND_DATA_IND_FAIL       2020 //"[PPT]Error: rx pkt send data indication fail type:%d, len:%d, buff:0x%x, hci_pkt_type:%d"
#define PPT_PKT_LEN_MISMATCH                2021 //"[PPT]pkt len mismatch hw_len:%d, h_len:%d, LENGTH:%d, addon:%d"
#define PPT_EXCEED_MAX_RX_LEN               2022 //"[PPT]rx pkt length exceed, length: %d, max_len:%d, entry:%d"
#define PPT_HANDLE_RX_PKT_IN_TASK           2023 //"[PPT]RX PKT: entry:%d, type:%d, dup:%d, header_payload_length:%d, header_payload_data:%b"
#define PPT_TX_ACK_PKT                      2024 //"[PPT]tx pkt ack len:%d, buff:0x%x"
#define PPT_TX_FIFO_PKT_FREE_FAIL           2025 //"[PPT]Error: free fail proprietary_tx_fifo_pkt_free pkt:0x%x"
#define PPT_RCVD_LOCAL_HOST_ACL_DATA        2026 //"[PPT]local_host_acl_data_payload: entry:%d, data:%b"
#define PPT_SEND_C2H_HOST_DATA              2027 //"[PPT]send c2h host_data:%b"
#define PPT_RETRANSMIT_CNT_LIMIT            2028 //"[PPT]Error: Exceed retransmission limit, threshold:%d"
#define PPT_GPIO_INT_INFO                   2029 //"[PPT]gpio_int type_report:%d, trigger_ptx:%d"
#define PPT_ERR_NO_FREE_DATA_NODE           2030 //"[PPT]Error: alloc data node fail"
#define PPT_ERR_GET_ENTRY_ID_FAIL           2031 //"[PPT]Error: get entry id by conn handle fail"

// common
#define UNIT_WAKEUP_TIME_FAIL               4000 //"[POWER] check fail! unit_wakeup_time_diff less than guard time"
#define MEMORY_HEAP_ESTIMATE_INFO           4001 //"[MEM_ALLOC] heap memory scan:data on %d, data off %d, buffer on %d, buffer off %d, RAM_TYPE_DTCM0: %d, RAM_TYPE_ITCM1: %d, RAM_TYPE_DSPSHARE: %d"
#define MEMORY_HEAP_ESTIMATE_DIFF_INFO      4002 //"[MEM_ALLOC] heap memory difference :data on %d, data off %d, buffer on %d, buffer off %d, RAM_TYPE_DTCM0: %d, RAM_TYPE_ITCM1: %d, RAM_TYPE_DSPSHARE: %d"
#define SET_PSD_ALIGN_PID                   4003 //"[PSD] align to piconet: %d"
#define LOG_START_OFFSET                    4004 //"[BTC] decide start offset, candidate_overlap_intvl[%d %d], pack_type:%d, start_offs:%d"
/*======================================================================================*/


// debug
#define DEBUG_MONITOR1                 5019 //"[Tony Debug] - Halting Debug Enabled - Can't Enable Monitor Mode Debug! -"
#define DEBUG_MONITOR2                 5020 //"[Tony Debug] Watch type can only be WATCH_POINT_FUNCTION_DADDR_RW, WATCH_POINT_FUNCTION_DADDR_RW or WATCH_POINT_FUNCTION_DADDR_R"
#define DEBUG_MONITOR3                 5021 //"[Tony Debug] memory_monitor_start: Byte align failed, please check it"
/*===========================================MP=========================================*/
#define MP_START            7000

/*======================================================================================*/

/*=======================================PM=============================================*/
#define PM_START                 14000

/*======================================================================================*/

#define LOG_MAX_INDEX   0xFFFFFF

#endif//_PATCH_LOG_H__
