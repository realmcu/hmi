#include "app_ota_mode.h"

#include "app_ota.h"
#include "gap_conn_le.h"
#include "indirect_access.h"
#include "wdg.h"

#define BTAON_FAST_REBOOT_SW_INFO0 0x2

typedef union
{
    uint16_t d16;
    struct
    {
        uint16_t ota_valid: 1;
        uint16_t is_rom_code_patch: 1;
        uint16_t is_fw_trig_wdg_to: 1;
        uint16_t is_airplane_mode: 1;
        uint16_t is_send_patch_end_evt: 1;
        uint16_t ota_mode: 1;
        uint16_t reserved: 10;
    };
} T_OTA_FASTBOOT_FLAG;

static bool ota_mode_active;
static volatile bool ota_switch_pending;

static T_OTA_FASTBOOT_FLAG read_fastboot_flag(void)
{
    T_OTA_FASTBOOT_FLAG flag;
    flag.d16 = btaon_fast_read_safe(BTAON_FAST_REBOOT_SW_INFO0);
    return flag;
}

static void write_fastboot_flag(T_OTA_FASTBOOT_FLAG flag)
{
    btaon_fast_write_safe(BTAON_FAST_REBOOT_SW_INFO0, flag.d16);
}

bool app_ota_mode_requested(void)
{
    return read_fastboot_flag().ota_mode != 0;
}

void app_ota_mode_clear_request(void)
{
    T_OTA_FASTBOOT_FLAG flag = read_fastboot_flag();
    flag.ota_mode = 0;
    write_fastboot_flag(flag);
}

bool app_ota_mode_is_active(void)
{
    return ota_mode_active;
}

void app_ota_mode_set_active(bool active)
{
    ota_mode_active = active;
}

void app_ota_mode_request_switch(uint8_t conn_id)
{
    ota_switch_pending = true;
    (void)le_disconnect(conn_id);
}

bool app_ota_mode_consume_switch_request(void)
{
    bool pending = ota_switch_pending;
    ota_switch_pending = false;
    return pending;
}

void app_ota_mode_switch_and_reset(void)
{
    T_OTA_FASTBOOT_FLAG flag = read_fastboot_flag();
    flag.ota_mode = 1;
    write_fastboot_flag(flag);
    chip_reset(RESET_ALL_EXCEPT_AON);
}
