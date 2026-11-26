#ifndef _PATCH_LOG_H_
#define _PATCH_LOG_H_

/*=======================================MODULE NAME====================================*/
//#define YOUR_LOG_INDEX        0
/*======================================================================================*/

/*=========================================PHY==========================================*/
#define PHY_START                0
#define PHY_LOG_001              1       //"packet_parameter is not assigned"
/*======================================================================================*/

/*=======================================UPPERSTACK=====================================*/
#define UPPERSTACK_START         5000
#define UPPER_LOG_INIT_01        5001    //"Upperstack Patch Init"

#define UPPER_LOG_HCI_01         5101    //"imp_patch_upperstack_hci_send_init_cmd: hci.le_isoc_buf_size %d, hci.le_isoc_xmit_window %d"
#define UPPER_LOG_HCI_02         5102    //"hci_cmd_cmpl_evt: discard opcode 0x%04x before init"
#define UPPER_LOG_HCI_03         5103    //"patch_hci_handle_read_tx_power_cmpl failed, handle 0x%04x"
#define UPPER_LOG_HCI_04         5104    //"hci_cmd_status_evt: discard opcode 0x%04x before init"
#define UPPER_LOG_HCI_05         5105    //"hci_cmd_status_evt: opcode 0x%04x, can not find handle for addr %s"
#define UPPER_LOG_HCI_15         5115    //"hci_le_conn_cmpl_evt: handle 0x%04x, new peer_addr %b, peer_addr_type %d"
#define UPPER_LOG_HCI_16         5116    //"hci_le_conn_cmpl_evt: handle 0x%04x, peer_addr %b connection already exist"

#define UPPER_LOG_SMP_01         5201    //"sm_handle_link_key_notif_evt: bd_addr %s, link %p, key type 0x%02x, key %b"
#define UPPER_LOG_SMP_02         5202    //"sm_br_trigger_link_key_to_ltk_proc: addr %s, link state 0x%x, link mode 0x%08x"

/*======================================================================================*/

/*===========================================MP=========================================*/
#define PLATFORM_MP_START        7000
#define PLATFORM_MP_LOG_001      7001 //"[ADC]: mode = 0x%x, index = %d, average_times = %d"
#define PLATFORM_MP_LOG_002      7002 //"[ADC]: ADC Register Request Fail."
#define PLATFORM_MP_LOG_003      7003 //"[ADC]: sar_adc_result = %d"
#define PLATFORM_MP_LOG_004      7004 //"[CHARGER]: test_type = 0x%x, full_current = %d"
#define PLATFORM_MP_LOG_005      7005 //"[CHARGER]: Charger Test Timer Start Fail."
#define PLATFORM_MP_LOG_006      7006 //"[GPIO]: PAD_num = %d is not support"
#define PLATFORM_MP_LOG_007      7007 //"[GPIO]: sub_opcode = 0x%x, PAD_num = %d, direct_level = %d"
#define PLATFORM_MP_LOG_008      7008 //"[XTAL_AUTO_K] xtal_value = 0x%x"
#define PLATFORM_MP_LOG_009      7009 //"[XTAL_AUTO_K] freq_drift = %d Hz"
#define PLATFORM_MP_LOG_010      7010 //"[XTAL_AUTO_K] index = %d, dft_out = %d"
#define PLATFORM_MP_LOG_011      7011 //"[XTAL_AUTO_K] peak_index = %d"
#define PLATFORM_MP_LOG_012      7012 //"[PHY] pouter: packet_type not support"
#define PLATFORM_MP_LOG_013      7013 //"[PHY] DFT: IF_val = %d, dft_out = %d"
#define PLATFORM_MP_LOG_014      7014 //"[PHY] thermal_log = %d degree"
/*======================================================================================*/

/*========================================CHARGER=======================================*/
#define CHG_LOG_START            13000
#define CHG_LOG_001              13001   //"charger_init: MBIAS_POW_BIAS_500nA %d"
#define CHG_LOG_002              13002   //"charger_init: CHG_POW_M1 %d, CHG_POW_M2 %d"
#define CHG_LOG_003              13003   //"charger_adc_init: Timer Start Fail"
#define CHG_LOG_004              13004   //"charger_adc_init: ADC Register Request Fail"
#define CHG_LOG_005              13005   //"charger_adc_init: xTimerPendFunctionCallFromISR fail."
#define CHG_LOG_006              13006   //"charger_adc_deinit: Timer Stop Fail"
#define CHG_LOG_007              13007   //"charger_adc_deinit: Timer Delete Fail"
#define CHG_LOG_008              13008   //"[CHARGER_STATE] --> STATE_CHARGER_START"
#define CHG_LOG_009              13009   //"[CHARGER_STATE] --> STATE_CHARGER_PRE_CHARGE"
#define CHG_LOG_010              13010   //"[CHARGER_STATE] --> STATE_CHARGER_FAST_CHARGE"
#define CHG_LOG_011              13011   //"[CHARGER_STATE] --> STATE_CHARGER_FINISH"
#define CHG_LOG_012              13012   //"[CHARGER_STATE] --> STATE_CHARGER_ERROR (ErrorCode: %d)"
#define CHG_LOG_013              13013   //"[CHARGER_STATE] --> STATE_CHARGER_END"
#define CHG_LOG_014              13014   //"[CHARGER_STATE] Total Time Stayed: %d ms"
#define CHG_LOG_015              13015   //"[CHARGER_STATE] STATE_CHARGER_START"
#define CHG_LOG_016              13016   //"[CHARGER_STATE] STATE_CHARGER_PRE_CHARGE"
#define CHG_LOG_017              13017   //"[CHARGER_STATE] STATE_CHARGER_FAST_CHARGE"
#define CHG_LOG_018              13018   //"[CHARGER_STATE] STATE_CHARGER_FINISH"
#define CHG_LOG_019              13019   //"[CHARGER_STATE] STATE_CHARGER_ERROR (ErrorCode: %d)"
#define CHG_LOG_020              13020   //"[CHARGER_STATE] STATE_CHARGER_END"
#define CHG_LOG_021              13021   //"[CURRENT_AVERAGE] %d"
#define CHG_LOG_022              13022   //"[SELF_CALIBRATION] %d"
#define CHG_LOG_023              13023   //"[BATTERY_STATE] %d %d %d %d %d"
#define CHG_LOG_024              13024   //"[STATE_OF_CHARGE] %d %%"
#define CHG_LOG_025              13025   //"charger_timer_handler: Charger FSM Update Fail"
#define CHG_LOG_026              13026   //"discharger_init: MBIAS_POW_BIAS_500nA %d"
#define CHG_LOG_027              13027   //"discharger_adc_init: Timer Start Fail"
#define CHG_LOG_028              13028   //"discharger_adc_init: ADC Register Request Fail"
#define CHG_LOG_029              13029   //"discharger_adc_init: xTimerPendFunctionCallFromISR fail."
#define CHG_LOG_030              13030   //"discharger_adc_deinit: Timer Stop Fail"
#define CHG_LOG_031              13031   //"discharger_adc_deinit: Timer Delete Fail"
#define CHG_LOG_032              13032   //"[BATTERY_STATE] %d %d"
#define CHG_LOG_033              13033   //"[STATE_OF_CHARGE] %d %%"
#define CHG_LOG_034              13034   //"discharger_timer_handler: Timer Start Fail"
#define CHG_LOG_035              13035   //"discharger_timer_handler: Discharger SOC Update Fail"
#define CHG_LOG_036              13036   //"[CHARGER_API] Set Adapter Current: 0x%x"
#define CHG_LOG_037              13037   //"[CHARGER_API] Set Full Current: 0x%x"
#define CHG_LOG_038              13038   //"charger_self_calibration: xTimerPendFunctionCallFromISR fail."
#define CHG_LOG_039              13039   //"[CHARGER]: test_type = 0x%x, full_current = %d"
#define CHG_LOG_040              13040   //"[CHARGER]: Charger Test Timer Start Fail."
/*======================================================================================*/

/*==============================PLATFORM===============================*/
#define PLATFORM_START           16000
#define PLATFORM_APPCFG_PARSE    16001 //"parse sys_cfg in app cfg 0x%x error"
#define PLATFORM_SYSCFG_PARSE    16002 //">>>>>> CFG_Header:0x%x, Parsing Error"
#define PLATFORM_LOG_003         16003 //"ADC_Handler: p_ADC_mgr or cb null error."
#define PLATFORM_LOG_004         16004 //"ADC_Mgr_Handler: os_timer_pend_function_call fail."
/*======================================================================================*/

/*========================================IO=======================================*/
#define IO_LOG_START             17000
#define IO_LOG_001               17001   //"[ADC VENDOR CMD]: ADC Register Request Fail."
#define IO_LOG_002               17002   //"[ADC VENDOR CMD]: mode = 0x%x, index = %d, average_times = %d"
#define IO_LOG_003               17003   //"[ADC VENDOR CMD]: adc_result = %d"
#define IO_LOG_004               17004   //"[GPIO VENDOR CMD]: Invalid Pin Number."
#define IO_LOG_005               17005   //"[GPIO VENDOR CMD]: pin number %d, input_level = %d"
#define IO_LOG_006               17006   //"[GPIO VENDOR CMD]: Invalid Output Level."
#define IO_LOG_007               17007   //"[GPIO VENDOR CMD]: pin number %d, output_level = %d"
#define IO_LOG_008               17008   //"[GPIO VENDOR CMD]: gpio init pin number %d, direction %d"
/*======================================================================================*/

/*=======================================FLASH=====================================*/
#define FLASH_START                                     4000
#define FLASH_CAL_OUT_OF_MALLOC_SPACE                   4001 //"[Flash Nor] Avaliable setting number is larger than the malloc space!"
#define FLASH_BP_LEVEL                                  4002 //"[Flash Nor SPIC0] BP Lv = %d, cmp = %d, sec = %d, tb = %d"
#define FLASH_SET_BP_LEVEL_0                            4003 //"[Flash Nor SPIC0] BP Lv = %d"
#define FLASH_CS_SUSPEND_CMD_NODE_INFO                  4004 //"[Flash Nor] Context switch suspend cmd node - owner = 0x%08x"
#define FLASH_CS_RESUME_CMD_NODE_INFO                   4005 //"[Flash Nor] Context switch resume cmd node - owner = 0x%08x"
#define FLASH_RESUME_HIGHEST_PRIORITY_DMA_ERROR_LOG     4006 //"[Flash Nor] Should not exist a erase blocking node here. cmd_node = %p, highest = %p"
/*======================================================================================*/

#endif//_PATCH_LOG_H__
