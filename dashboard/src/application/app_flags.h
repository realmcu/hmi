/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef _APP_FLAGS_H_
#define _APP_FLAGS_H_

//Init value of default features are defined here
//----- [Device related] -----
#define F_APP_AUTO_POWER_TEST_LOG           0
#define F_APP_TEST_SUPPORT                  1
#define F_APP_DATA_CAPTURE_SUPPORT          1
#define F_APP_SAIYAN_MODE                   0
#define F_APP_SAIYAN_EQ_FITTING             1
#define F_APP_SUPPORT_CAPTURE_ACOUSTICS_MP  1
#define F_APP_CONSOLE_SUPPORT               1
#define F_APP_DUT_MODE_AUTO_POWER_OFF       0
#define F_APP_VOICE_NREC_SUPPORT            1
#define F_APP_VOICE_SPK_EQ_SUPPORT          1
#define F_APP_VOICE_MIC_EQ_SUPPORT          1
#define F_APP_SIDETONE_SUPPORT              1
#define F_APP_SMOOTH_BAT_REPORT             1
#define F_APP_USER_EQ_SUPPORT               1
#define F_APP_DISABLE_NOTIFICATION_SUPPORT  0
#define F_APP_MONITOR_MEMORY_AND_TIMER      0
#define F_APP_UART_DFU                      0
#define F_APP_SD_CARD_PLAY                  0
#define F_APP_USB_HID_PC_TOOL               0
#define F_APP_UAC_MEDIA_SILENCE_DETECT      0
#define CONFIG_REALTEK_GFPS_FEATURE_SUPPORT                0
#define CONFIG_REALTEK_GFPS_FINDER_SUPPORT                (0 && CONFIG_REALTEK_GFPS_FEATURE_SUPPORT)
#define CONFIG_REALTEK_GFPS_LE_DEVICE_SUPPORT             (0 && CONFIG_REALTEK_GFPS_FEATURE_SUPPORT)
#define F_APP_CFU_FEATURE_SUPPORT           0
#define F_APP_USB_HID_SUPPORT               0
#define F_APP_USB_HID_SEC_SUPPORT           0
#define F_APP_MALLEUS_SUPPORT               0

#define F_APP_DSP_SHM_80KB_TO_MCU_CHECK_SUPPORT   1
#define CONFIG_REALTEK_APP_BOND_MGR_SUPPORT              1

//----- [Dual Mode related] -----
#define F_APP_BT_ANCS_CLIENT_SUPPORT       0



//----- [BT related] -----
#define F_APP_MULTILINK_ENABLE              0
#define F_APP_A2DP_CODEC_LDAC_SUPPORT       0
#define F_APP_BT_PROFILE_PBAP_PCE_SUPPORT   0
#define F_APP_BT_PROFILE_MAP_MCE_SUPPORT    0
#define F_APP_IAP_RTK_SUPPORT               0
#define F_APP_IAP_SUPPORT                   0
#define F_APP_BT_HID_DEVICE_SUPPORT         0
#define F_APP_HID_MOUSE_SUPPORT             0
#define F_APP_HID_KEYBOARD_SUPPORT          0
#define F_APP_BT_HID_HOST_SUPPORT           0
#define F_APP_A2DP_SOURCE_SUPPORT           0
#define F_APP_A2DP_SINK_SUPPORT             0
#define F_APP_HFP_AG_SUPPORT                0
#define F_APP_HFP_HF_SUPPORT                0
#define F_APP_ACL_ROLE_FORCE_MASTER         0
#define F_APP_GATT_OVER_BREDR_SUPPORT       1
#define F_APP_BREDR_SC_CTKD_SUPPORT         1
#define F_APP_A2DP_MULTI_SINK_SUPPORT       0

//----- [LE related] -----
#define F_APP_GATT_SERVER_EXT_API_SUPPORT   1
#define F_BT_GATT_SERVER_EXT_API            1
#define F_APP_BLE_AMS_CLIENT_SUPPORT        0
#define F_APP_BLE_HID_DEVICE_SUPPORT        0
#define F_APP_BLE_HID_HOST_SUPPORT          0
#define F_APP_LE_AUDIO_INITIATOR_SUPPORT    0
#define F_APP_LE_AUDIO_ACCEPTOR_SUPPORT     0
#define F_APP_SC_KEY_DERIVE_SUPPORT         1
#define CONFIG_REALTEK_BT_GATT_CLIENT_SUPPORT            1

//----- [Peripheral related] -----
#define F_APP_ADC_SUPPORT                   1
#define F_APP_LINEIN_SUPPORT                0
#define F_APP_USB_AUDIO_SUPPORT             0
#define F_APP_USB_MSC_SUPPORT               0
#define F_APP_USB_CDC_SUPPORT               0
#if F_APP_USB_CDC_SUPPORT
#define IAD_SUPPORT                         1
#endif

#define F_APP_AMP_SUPPORT                   0
#define F_APP_EXT_FLASH_SUPPORT             0
#define F_APP_HIFI4_SUPPORT                 0
#define F_APP_SPDIF_SUPPORT                 0

#define F_APP_CAN_SUPPORT                   0

#define F_APP_HFP_CMD_SUPPORT               1
#define F_APP_DEVICE_CMD_SUPPORT            1
#define F_APP_AVRCP_CMD_SUPPORT             1
#define F_APP_PBAP_CMD_SUPPORT              0

#define F_APP_CUSTOMER_VD_SPP_SUPPORT       1
#define F_APP_CUSTOMER_RECORD_SUPPORT       1

#define F_APP_ENABLE_TWO_ONE_WIRE_UART      0
#define F_APP_DISABLE_B2S_SUPPORT           0

#define F_APP_MULTI_CHANNEL_SUPPORT         0

#if (CONFIG_SOC_SERIES_RTL8773D|| TARGET_RTL8773DFL)
#define IC_NAME                         "RTL87X3D"
#else
#define IC_NAME                         "RTL87X3E"
#endif

#if F_APP_AMP_SUPPORT
#define F_APP_CUSTOMER_EXT_PA_SUPPORT_AW88394           0
#define F_APP_CUSTOMER_EXT_PA_SUPPORT_AW87390           1
#define F_APP_CUSTOMER_EXT_PA_SUPPORT_AW_CUSTOMER       1
#endif

#define F_APP_FINDMY_FEATURE_SUPPORT        0

//----- [Sample configuration] -----
/**
 *  NOTE: Only one demo support flags shall be set 1
 */
#define F_APP_BT_AUDIO_TRANSMITTER_DEMO_SUPPORT         1
#define F_APP_BT_AUDIO_RECEIVER_DEMO_SUPPORT            0
#define F_APP_BT_AUDIO_TRANSCEIVER_DEMO_SUPPORT         0
#define F_APP_BT_AUDIO_TRANSMITTER_MP3_DEMO_SUPPORT     0
#define F_APP_CHARGE_CASE_DEMO_SUPPORT                  0

#if CONFIG_REALTEK_APP_DASHBOARD_DEMO_SUPPORT
#undef F_APP_BT_AUDIO_TRANSMITTER_DEMO_SUPPORT
#define F_APP_BT_AUDIO_TRANSMITTER_DEMO_SUPPORT         0
#undef F_APP_GATT_OVER_BREDR_SUPPORT
#define F_APP_GATT_OVER_BREDR_SUPPORT                   0
#endif


#if F_APP_BT_AUDIO_TRANSMITTER_DEMO_SUPPORT
#undef F_APP_A2DP_SOURCE_SUPPORT
#define F_APP_A2DP_SOURCE_SUPPORT               1
#undef F_APP_HFP_AG_SUPPORT
#define F_APP_HFP_AG_SUPPORT                    1

#undef F_SOURCE_PLAY_SUPPORT
#define F_SOURCE_PLAY_SUPPORT                   1
#undef F_APP_LE_AUDIO_INITIATOR_SUPPORT
#define F_APP_LE_AUDIO_INITIATOR_SUPPORT        1
#undef F_APP_DISABLE_NOTIFICATION_SUPPORT
#define F_APP_DISABLE_NOTIFICATION_SUPPORT      1
#undef F_APP_USB_AUDIO_SUPPORT
#define F_APP_USB_AUDIO_SUPPORT                 1
#undef F_APP_BT_HID_HOST_SUPPORT
#define F_APP_BT_HID_HOST_SUPPORT               1
#undef BLE_HID_CLIENT_SUPPORT
#define BLE_HID_CLIENT_SUPPORT                  0
#undef F_APP_SD_CARD_PLAY
#define F_APP_SD_CARD_PLAY                      0
#undef F_APP_VOICE_NREC_SUPPORT
#define F_APP_VOICE_NREC_SUPPORT                0
#if F_APP_USB_AUDIO_SUPPORT
#define F_APP_FWK_PIPE_DEMO_SUPPORT             1
#else
#define F_APP_FWK_PIPE_DEMO_SUPPORT             0
#endif

#if F_APP_SD_CARD_PLAY
#define F_APP_SD_CARD_LOCALPLAY                 0
#endif

#endif /* end of F_APP_BT_AUDIO_TRANSMITTER_DEMO_SUPPORT */

#if F_APP_BT_AUDIO_TRI_DONGLE
#undef F_APP_A2DP_SOURCE_SUPPORT
#define F_APP_A2DP_SOURCE_SUPPORT           1
#undef F_APP_HFP_AG_SUPPORT
#define F_APP_HFP_AG_SUPPORT                1

#define F_APP_LEAUDIO_SPP_CMD_SUPPORT       0
#define F_APP_HID_TELEPHONY_SUPPORT         0
#undef F_APP_USB_HID_PC_TOOL
#define F_APP_USB_HID_PC_TOOL               1
#undef F_SOURCE_PLAY_SUPPORT
#define F_SOURCE_PLAY_SUPPORT               1
#undef F_APP_LE_AUDIO_INITIATOR_SUPPORT
#define F_APP_LE_AUDIO_INITIATOR_SUPPORT    1
#undef F_APP_DISABLE_NOTIFICATION_SUPPORT
#define F_APP_DISABLE_NOTIFICATION_SUPPORT  1
#undef F_APP_USB_AUDIO_SUPPORT
#define F_APP_USB_AUDIO_SUPPORT             1
#undef F_APP_USB_HID_SUPPORT
#define F_APP_USB_HID_SUPPORT               1
#undef F_APP_FWK_PIPE_DEMO_SUPPORT
#define F_APP_FWK_PIPE_DEMO_SUPPORT         1
#undef TRANSMIT_CLIENT_SUPPORT
#define TRANSMIT_CLIENT_SUPPORT             1
#undef BLE_HID_CLIENT_SUPPORT
#define BLE_HID_CLIENT_SUPPORT              1
#undef F_APP_BT_AUDIO_TRI_DONGLE_2_4G
#define F_APP_BT_AUDIO_TRI_DONGLE_2_4G      0
#undef F_APP_UAC_MEDIA_SILENCE_DETECT
#define F_APP_UAC_MEDIA_SILENCE_DETECT      0
#undef F_APP_VOICE_NREC_SUPPORT
#define F_APP_VOICE_NREC_SUPPORT            0
#undef F_APP_USB_HID_SEC_SUPPORT
#define F_APP_USB_HID_SEC_SUPPORT           1
#undef F_APP_BT_AUDIO_TRI_DONGLE_LEAUDIO
#define F_APP_BT_AUDIO_TRI_DONGLE_LEAUDIO   1
#undef F_APP_USB_CDC_SUPPORT
#define F_APP_USB_CDC_SUPPORT               0
#define F_APP_1_EP_1_DEVICE_HID_SUPPORT     0
#define F_APP_2_EP_1_HID_UPDATE_ON_EP2      0
#define F_APP_UAC_SDK_SUPPORT               0

#if CONFIG_UHID_DONGLE_FEATURE
/* cdc + 1 ble hide */
#undef F_APP_USB_CDC_SUPPORT
#define F_APP_USB_CDC_SUPPORT               1
#undef F_APP_USB_AUDIO_SUPPORT
#define F_APP_USB_AUDIO_SUPPORT             0
#undef F_APP_DISABLE_B2S_SUPPORT
#define F_APP_DISABLE_B2S_SUPPORT           1
#endif

//feature
#if F_APP_USB_HID_PC_TOOL
#undef F_APP_CFU_FEATURE_SUPPORT
#define F_APP_CFU_FEATURE_SUPPORT           1
#else
#undef F_APP_UART_DFU
#define F_APP_UART_DFU                      1
#endif

#if F_APP_USB_CDC_SUPPORT
#undef F_APP_1_EP_1_DEVICE_HID_SUPPORT
#define F_APP_1_EP_1_DEVICE_HID_SUPPORT     1
#undef IAD_SUPPORT
#define IAD_SUPPORT                         1
#undef F_APP_USB_HID_SEC_SUPPORT
#define F_APP_USB_HID_SEC_SUPPORT           0
#endif

#if F_APP_BT_AUDIO_TRI_DONGLE_2_4G
/* USB HID DEVICE TYPE */
#define USB_2DOT4G_DONGLE                   1
#define USB_2DOT4G_DONGLE_PCB               2
#define USB_HID_DEVICE_TYPE                 USB_2DOT4G_DONGLE
#define TRIPLE_ENABLE_SPI                   1
#endif

#if F_APP_BT_AUDIO_TRI_DONGLE_LEAUDIO
#undef F_APP_LEAUDIO_SPP_CMD_SUPPORT
#define F_APP_LEAUDIO_SPP_CMD_SUPPORT       0
#endif
#endif

#if F_APP_BT_AUDIO_RECEIVER_DEMO_SUPPORT
#undef F_APP_A2DP_SINK_SUPPORT
#define F_APP_A2DP_SINK_SUPPORT             1
#undef F_APP_HFP_HF_SUPPORT
#define F_APP_HFP_HF_SUPPORT                1

#undef F_APP_BT_PROFILE_PBAP_PCE_SUPPORT
#define F_APP_BT_PROFILE_PBAP_PCE_SUPPORT   1
#undef F_APP_BT_PROFILE_MAP_MCE_SUPPORT
#define F_APP_BT_PROFILE_MAP_MCE_SUPPORT    1
#undef F_APP_IAP_RTK_SUPPORT
#define F_APP_IAP_RTK_SUPPORT               0
#undef F_APP_IAP_SUPPORT
#define F_APP_IAP_SUPPORT                   0
#undef F_APP_BT_HID_DEVICE_SUPPORT
#define F_APP_BT_HID_DEVICE_SUPPORT         1
#undef F_APP_HID_MOUSE_SUPPORT
#define F_APP_HID_MOUSE_SUPPORT             0
#undef F_APP_HID_KEYBOARD_SUPPORT
#define F_APP_HID_KEYBOARD_SUPPORT          1
#undef F_APP_LE_AUDIO_ACCEPTOR_SUPPORT
#define F_APP_LE_AUDIO_ACCEPTOR_SUPPORT     1

#undef F_APP_SCO_XMIT_AG_SUPPORT
#define F_APP_SCO_XMIT_AG_SUPPORT           0

#undef F_APP_PBAP_CMD_SUPPORT
#define F_APP_PBAP_CMD_SUPPORT              1

#undef F_APP_MALLEUS_SUPPORT
#define F_APP_MALLEUS_SUPPORT               0

#undef CONFIG_REALTEK_GFPS_FEATURE_SUPPORT
#define CONFIG_REALTEK_GFPS_FEATURE_SUPPORT                0
#undef CONFIG_REALTEK_GFPS_FINDER_SUPPORT
#define CONFIG_REALTEK_GFPS_FINDER_SUPPORT                (1 && CONFIG_REALTEK_GFPS_FEATURE_SUPPORT)
#undef CONFIG_REALTEK_GFPS_LE_DEVICE_SUPPORT
#define CONFIG_REALTEK_GFPS_LE_DEVICE_SUPPORT             (1 && CONFIG_REALTEK_GFPS_FEATURE_SUPPORT)

#endif /* end of F_APP_BT_AUDIO_RECEIVER_DEMO_SUPPORT */


#if F_APP_BT_AUDIO_TRANSCEIVER_DEMO_SUPPORT
#undef F_APP_DISABLE_NOTIFICATION_SUPPORT
#define F_APP_DISABLE_NOTIFICATION_SUPPORT  1

#define F_APP_SPI_ROLE_MASTER               0
#define F_APP_SPI_ROLE_SLAVE                0
#define F_APP_INTEGRATED_TRANSCEIVER        0

#undef F_APP_AMP_SUPPORT
#define F_APP_AMP_SUPPORT                   0

#if F_APP_SPI_ROLE_MASTER
#undef F_APP_A2DP_SINK_SUPPORT
#define F_APP_A2DP_SINK_SUPPORT             1
#undef F_APP_HFP_HF_SUPPORT
#define F_APP_HFP_HF_SUPPORT                1

#define F_APP_A2DP_XMIT_SNK_LEA_SUPPORT     1
#define F_APP_A2DP_XMIT_SNK_SUPPORT         1
#define F_APP_SCO_XMIT_HF_SUPPORT           1
#endif

#if F_APP_SPI_ROLE_SLAVE
#undef F_APP_A2DP_SOURCE_SUPPORT
#define F_APP_A2DP_SOURCE_SUPPORT           1
#undef F_APP_HFP_AG_SUPPORT
#define F_APP_HFP_AG_SUPPORT                1

#define F_APP_A2DP_XMIT_SRC_LEA_SUPPORT     1
#define F_APP_A2DP_XMIT_SRC_SUPPORT         1
#define F_APP_SCO_XMIT_AG_SUPPORT           1
#if F_APP_A2DP_XMIT_SRC_LEA_SUPPORT
#undef F_APP_LE_AUDIO_INITIATOR_SUPPORT
#define F_APP_LE_AUDIO_INITIATOR_SUPPORT    1
#endif /* end of F_APP_A2DP_XMIT_SRC_LEA_SUPPORT */
#endif /* end of F_APP_SPI_ROLE_SLAVE */

#if F_APP_INTEGRATED_TRANSCEIVER
#undef F_APP_MULTILINK_ENABLE
#define F_APP_MULTILINK_ENABLE              1
#undef F_APP_ACL_ROLE_FORCE_MASTER
#define F_APP_ACL_ROLE_FORCE_MASTER         1

#undef F_SOURCE_PLAY_SUPPORT
#define F_SOURCE_PLAY_SUPPORT               1
#undef F_APP_VOICE_NREC_SUPPORT
#define F_APP_VOICE_NREC_SUPPORT            0
#undef F_APP_FWK_PIPE_DEMO_SUPPORT
#define F_APP_FWK_PIPE_DEMO_SUPPORT         1

#undef F_APP_A2DP_SOURCE_SUPPORT
#define F_APP_A2DP_SOURCE_SUPPORT           1
#undef F_APP_HFP_AG_SUPPORT
#define F_APP_HFP_AG_SUPPORT                1
#undef F_APP_A2DP_SINK_SUPPORT
#define F_APP_A2DP_SINK_SUPPORT             1
#undef F_APP_HFP_HF_SUPPORT
#define F_APP_HFP_HF_SUPPORT                1
#endif

/* Shall be set 1 when support a2dp multi_sink function */
#undef F_APP_A2DP_MULTI_SINK_SUPPORT
#define F_APP_A2DP_MULTI_SINK_SUPPORT       0

#undef F_APP_MULTI_CHANNEL_SUPPORT
#define F_APP_MULTI_CHANNEL_SUPPORT         0

#undef F_APP_LE_AUDIO_INITIATOR_SUPPORT
#define F_APP_LE_AUDIO_INITIATOR_SUPPORT    1
#undef F_APP_LE_AUDIO_ACCEPTOR_SUPPORT
#define F_APP_LE_AUDIO_ACCEPTOR_SUPPORT     1

#endif /* end of F_APP_BT_AUDIO_TRANSCEIVER_DEMO_SUPPORT */


#if F_APP_BT_AUDIO_TRANSMITTER_MP3_DEMO_SUPPORT
#undef F_APP_A2DP_SOURCE_SUPPORT
#define F_APP_A2DP_SOURCE_SUPPORT           1
#undef F_APP_HFP_AG_SUPPORT
#define F_APP_HFP_AG_SUPPORT                1

#define F_APP_MUSIC_LOCAL_PLAY_SUPPORT                  1
#define F_APP_MUSIC_A2DP_SOURCE_SUPPORT                 1
#define F_APP_CUSTOMER_AUDIO_POLICY_SUPPORT             1
#endif /* end of F_APP_BT_AUDIO_TRANSMITTER_MP3_DEMO_SUPPORT */


#if F_APP_CHARGE_CASE_DEMO_SUPPORT
#undef F_APP_A2DP_SOURCE_SUPPORT
#define F_APP_A2DP_SOURCE_SUPPORT               1
#undef F_APP_HFP_AG_SUPPORT
#define F_APP_HFP_AG_SUPPORT                    1

#undef F_SOURCE_PLAY_SUPPORT
#define F_SOURCE_PLAY_SUPPORT                   1
#undef F_APP_VOICE_NREC_SUPPORT
#define F_APP_VOICE_NREC_SUPPORT                0
#undef F_APP_LE_AUDIO_INITIATOR_SUPPORT
#define F_APP_LE_AUDIO_INITIATOR_SUPPORT        1
#undef F_APP_DISABLE_NOTIFICATION_SUPPORT
#define F_APP_DISABLE_NOTIFICATION_SUPPORT      1
#undef F_APP_USB_AUDIO_SUPPORT
#define F_APP_USB_AUDIO_SUPPORT                 1
#undef TRANSMIT_CLIENT_SUPPORT
#define TRANSMIT_CLIENT_SUPPORT                 1
#undef F_APP_CHARGING_CASE_CMD_SUPPORT
#define F_APP_CHARGING_CASE_CMD_SUPPORT         1
#undef F_APP_CHARGING_CASE_CMD_TEST_SUPPORT
#define F_APP_CHARGING_CASE_CMD_TEST_SUPPORT    0
#undef F_APP_SD_CARD_PLAY
#define F_APP_SD_CARD_PLAY                      0
#if F_APP_USB_AUDIO_SUPPORT
#define F_APP_FWK_PIPE_DEMO_SUPPORT             1
#else
#define F_APP_FWK_PIPE_DEMO_SUPPORT             0
#endif

#undef F_APP_FINDMY_FEATURE_SUPPORT
#define F_APP_FINDMY_FEATURE_SUPPORT            0

#undef F_APP_ENABLE_TWO_ONE_WIRE_UART
#define F_APP_ENABLE_TWO_ONE_WIRE_UART          0

#if (TARGET_RTL8763EW_VC || TARGET_RTL8763EWE_VP || TARGET_RTL8763EWE || CONFIG_SOC_SERIES_RTL8773D|| (CONFIG_SOC_SERIES_RTL8773E && !CONFIG_REALTEK_TRAGET_4M))
#undef F_APP_GUI_SUPPORT
#define F_APP_GUI_SUPPORT                       1
#undef ENABLE_PSRAM_FOR_LCD
#define ENABLE_PSRAM_FOR_LCD                    0
//#define  ENABLE_RTK_GUI_SCRIPT_AS_A_APP
#define FB_DIRECTION_ROTATE                     1
#if (CONFIG_SOC_SERIES_RTL8773D|| (CONFIG_SOC_SERIES_RTL8773E && !CONFIG_REALTEK_TRAGET_4M))
#define FB_DATA_ROTATE                          1
#endif
#endif

#if F_APP_GUI_SUPPORT
#define F_GUI_CHARGEBOX_DEMO                    1  /*only select one demo flag to set true*/
#define F_GUI_SIMPLE_SPEED_DEMO                 0  /*only select one demo flag to set true*/
#define F_GUI_BR_BLE_LINK_STATUS_DEMO           0  /*only select one demo flag to set true*/
#define F_GUI_SDCARD_LIST_DEMO                  0  /*only select one demo flag to set true*/


#if F_GUI_SDCARD_LIST_DEMO
#undef F_APP_SD_CARD_PLAY
#define F_APP_SD_CARD_PLAY                      1

#endif
#endif

#if F_APP_SD_CARD_PLAY
#define F_APP_SD_CARD_LOCALPLAY                 1
#endif

#endif /* end of F_APP_CHARGE_CASE_DEMO_SUPPORT */


#if(CONFIG_REALTEK_APP_DASHBOARD_DEMO_SUPPORT == 1)

#undef F_APP_BT_AUDIO_TRANSMITTER_DEMO_SUPPORT
#define F_APP_BT_AUDIO_TRANSMITTER_DEMO_SUPPORT        0

#undef F_APP_DSP_SHM_80KB_TO_MCU_CHECK_SUPPORT
#define F_APP_DSP_SHM_80KB_TO_MCU_CHECK_SUPPORT         1

#undef F_APP_A2DP_SINK_SUPPORT
#define F_APP_A2DP_SINK_SUPPORT                 1
#undef F_APP_HFP_HF_SUPPORT
#define F_APP_HFP_HF_SUPPORT                    1

#undef F_APP_DISABLE_NOTIFICATION_SUPPORT
#define F_APP_DISABLE_NOTIFICATION_SUPPORT      1
#undef F_APP_BLE_HID_DEVICE_SUPPORT
#define F_APP_BLE_HID_DEVICE_SUPPORT            1
#undef F_APP_BT_PROFILE_PBAP_PCE_SUPPORT
#define F_APP_BT_PROFILE_PBAP_PCE_SUPPORT       1
#undef F_APP_PBAP_CMD_SUPPORT
#define F_APP_PBAP_CMD_SUPPORT                  1
#undef F_APP_BT_PROFILE_MAP_MCE_SUPPORT
#define F_APP_BT_PROFILE_MAP_MCE_SUPPORT        1
#undef F_APP_BT_ANCS_CLIENT_SUPPORT
#define F_APP_BT_ANCS_CLIENT_SUPPORT           1
#undef F_APP_BLE_AMS_CLIENT_SUPPORT
#define F_APP_BLE_AMS_CLIENT_SUPPORT            1


#ifdef CONFIG_REALTEK_APP_DASHBOARD_DEMO_SUPPORT
#define F_APP_HONEY_GUI                     1
#define F_APP_GUI_USE_PSRAM                 1
#define F_APP_GUI_RAMLESS                   0
#define F_APP_PACKAGE_QFN68                 1
#define F_APP_SUPPORT_USB                   0
//#define RTK_MODULE_RTK_PPEV2

#if F_APP_GUI_USE_PSRAM
#define PSRAM_FRAME_BUF1_ADDR               0x4000000
#define PSRAM_FRAME_BUF2_ADDR               (0x4000000 + 800 * 480 * 2)
#ifndef PSRAM_GUI_HEAP_ADDR
#define PSRAM_GUI_HEAP_ADDR                 (0x4000000 + 800 * 480 * 2 * 2)
#endif
#define PSRAM_GUI_HEAP_SIZE                 0x200000
#else
#define ENABLE_RTK_GUI_OS_HEAP
#endif

#endif

#endif /* end of CONFIG_REALTEK_APP_DASHBOARD_DEMO_SUPPORT */

#if F_APP_FINDMY_FEATURE_SUPPORT
#define APP_FINDMY_MAX_LINKS                            2 /** APP LE link number */
#define F_APP_FINDMY_USE_UARP                           0 //this macro enable firmware update function
#define F_APP_FINDMY_SUPPORT_NFC                        0 /* set 1 to support NFC */
#endif

#if F_APP_ENABLE_TWO_ONE_WIRE_UART
#undef F_APP_ONE_WIRE_UART_SUPPORT
#define F_APP_ONE_WIRE_UART_SUPPORT 1
#endif

#if TARGET_RTL8763EW_VC
#if ENABLE_PSRAM_FOR_LCD
#define APM_PSRAM_SUPPORT                               1
#endif
#endif

#if TARGET_RTL8763EWE_VP
#if ENABLE_PSRAM_FOR_LCD
#define WB_PSRAM_SUPPORT                                1
#endif
#endif

#if TARGET_RTL8763EWE
#if ENABLE_PSRAM_FOR_LCD
/*enable psram hardware support*/
#endif
#endif

#if (CONFIG_SOC_SERIES_RTL8773D|| TARGET_RTL8773DFL)
#undef F_APP_DSP_SHM_80KB_TO_MCU_CHECK_SUPPORT
#define F_APP_DSP_SHM_80KB_TO_MCU_CHECK_SUPPORT         0
#undef F_APP_HIFI4_SUPPORT
#define F_APP_HIFI4_SUPPORT                             1
#undef F_APP_SPDIF_SUPPORT
#define F_APP_SPDIF_SUPPORT                             1
#if ENABLE_PSRAM_FOR_LCD
/*enable psram hardware support*/
#endif
#endif

#if (CONFIG_SOC_SERIES_RTL8773E)
#undef F_APP_DSP_SHM_80KB_TO_MCU_CHECK_SUPPORT
#define F_APP_DSP_SHM_80KB_TO_MCU_CHECK_SUPPORT         1
#undef F_APP_USB_AUDIO_SUPPORT
#define F_APP_USB_AUDIO_SUPPORT             0
#undef F_APP_USB_MSC_SUPPORT
#define F_APP_USB_MSC_SUPPORT               0
#undef F_APP_USB_HID_SUPPORT
#define F_APP_USB_HID_SUPPORT               0

#if TARGET_RTL8773EWE
#if ENABLE_PSRAM_FOR_LCD
#endif
#endif

#if TARGET_RTL8773EWE_VP
#if ENABLE_PSRAM_FOR_LCD
#define WB_PSRAM_SUPPORT                                1
#endif
#endif

#if TARGET_RTL8773EWP
#if ENABLE_PSRAM_FOR_LCD
#define WB_PSRAM_SUPPORT                                1
#endif
#endif

#endif

#if (F_APP_USER_EQ_SUPPORT == 1)
#undef F_APP_AUDIO_VOICE_SPK_EQ_INDEPENDENT_CFG
#define F_APP_AUDIO_VOICE_SPK_EQ_INDEPENDENT_CFG    1
#undef F_APP_AUDIO_VOICE_SPK_EQ_COMPENSATION_CFG
#define F_APP_AUDIO_VOICE_SPK_EQ_COMPENSATION_CFG   1
#endif

#ifdef CONFIG_SOC_SERIES_RTL8773E
#undef TARGET_RTL8773E
#define TARGET_RTL8773E
#endif

#ifdef CONFIG_SOC_SERIES_RTL8773D
#undef TARGET_RTL8773D
#define TARGET_RTL8773D
#endif

#endif




