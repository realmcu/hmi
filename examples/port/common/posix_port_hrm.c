/*
 * hrm_vc30x.c
 *
 * Register-level VC30x heart-rate / SpO2 sensor driver + boot self-test.
 * See hrm_vc30x.h for the wiring facts and the scope of the port.
 *
 * The HR path below is a port of the vendor sample (drv_vc30x_sensor_v0.14,
 * vc30x_driver.c) with VC30x_CFG_DRV_BY_TIMER = enabled, so the comparison
 * stays line-for-line where it matters:
 *
 *   vendor                                  here
 *   vc30x_drv_reg_init(HR)                  hrm_hr_configure()
 *   vc30x_drv_get_sampling_data()           hrm_vc30x_poll()
 *     vc30x_drv_read_reg()                    hrm_read_regs()
 *     vc30x_drv_status_event_verify()         hrm_agc_verify()
 *     vc30x_drv_calibration_osc_clk()         hrm_osc_calibrate()
 *     vc30x_drv_read_fifo_result()            hrm_fifo_read()
 *   vc30x_drv_get_wear_status()             hrm_wear_update()
 *
 * Deviations, all deliberate:
 *  - BIO wear detection (f_BIODetect_V14_1) is not in this repository, so the
 *    wear decision is the vendor's PS + ENV threshold path alone
 *    (equivalent to VC30x_CFG_WEAR_BIO_EN = disabled).
 *  - The free-running platform cycle counter is read through the POSIX port
 *    API; differences are taken modulo 2^32 instead of the vendor's 24-bit
 *    wrap special case.
 *  - The raw boot self-test (FIFO off, single frame) is unchanged from the
 *    first bring-up of this board and keeps its own configuration.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "posix.h"
#include "posix_init.h"
#include "posix_port.h"
#include "ioctls/posix_ioctl_gpio.h"
#include "ioctls/posix_ioctl_hrm.h"
#include "ioctls/posix_ioctl_i2c.h"

#define APP_LOGE(fmt, ...) printf("[hrm] " fmt "\n", ##__VA_ARGS__)
#define APP_LOGW(fmt, ...) printf("[hrm] " fmt "\n", ##__VA_ARGS__)
#define APP_LOGI(fmt, ...) printf("[hrm] " fmt "\n", ##__VA_ARGS__)

#define HRM_I2C_PATH       "/dev/i2c0"
#define HRM_SENSOR_EN_PATH "/dev/gpio1/p16" /* P5_1 / GPIOB16 */

/* ---------------------------------------------------------------- *
 * Bus wiring (MOY.M81023.01): SCL/SDA = MCU pin 36/37 = P3_5/P3_4,
 * i.e. /dev/i2c0, the same POSIX bus the SC7A20 accelerometer uses. The
 * platform I2C port serialises both users.
 * ---------------------------------------------------------------- */
#define HRM_I2C_ADDR 0x33

/* Register map (subset used here). */
#define HRM_REG_CHIP_ID      0x00
#define HRM_REG_WORK_STATUS  0x01
#define HRM_REG_INT_EVENT    0x02
#define HRM_REG_FIFO_INDEX   0x03
#define HRM_REG_PRE_ENV      0x04 /* 0x04..0x06: pre/env/ps nibbles */
#define HRM_REG_OSC_COUNT    0x07 /* sampling-engine counter, 2x u16 BE */
#define HRM_REG_CFG          0x10 /* 17-byte configuration block */
#define HRM_REG_INT_CFG      0x11
#define HRM_REG_CLK_DIV      0x14
#define HRM_REG_FIFO_DIV     0x16
#define HRM_REG_LED_CUR      0x17 /* 0x17..0x19, one per slot */
#define HRM_REG_PD_RES       0x1A /* 0x1A..0x1C, one per slot */
#define HRM_REG_OVERSAMPLE   0x20
#define HRM_REG_RES_EXT      0x22
#define HRM_REG_BIO_CFG      0x23
#define HRM_REG_TEMP_CFG     0x25
#define HRM_REG_VERSION      0x40
#define HRM_REG_ENV_PS       0x41 /* 0x41..0x46: env0..2, pre0/1, ps */
#define HRM_REG_PS           0x46
#define HRM_REG_PDBIAS       0x47 /* 0x47..0x4A */
#define HRM_REG_BIO          0x4B /* 0x4B..0x4E: bio in hi/lo, out hi/lo */
#define HRM_REG_SOFT_RESET   0x3B
#define HRM_REG_FIFO_DATA    0x80

#define HRM_SOFT_RESET_CODE  0x5A
#define HRM_FIFO_BASE        0x80

/* reg 0x02 bits (VC30x_EVENT_*_MARK). */
#define HRM_EVENT_PPG         (1u << 0)
#define HRM_EVENT_ENV         (1u << 1)
#define HRM_EVENT_FIFO        (1u << 2)
#define HRM_EVENT_OV          (1u << 3)
#define HRM_EVENT_PS          (1u << 4)

/* reg 0x01 bits (VC30x_STATUS_*). */
#define HRM_STATUS_CONFLICT   (1u << 4)
#define HRM_STATUS_INSAMPLE   (1u << 3)
#define HRM_STATUS_OVERLOAD2  (1u << 2)
#define HRM_STATUS_OVERLOAD1  (1u << 1)
#define HRM_STATUS_OVERLOAD0  (1u << 0)
#define HRM_STATUS_ERROR_MASK 0xF8u

#define HRM_FIFO_GAP_MS       40u
#define HRM_EVENT_TIMEOUT_MS  400
#define HRM_ENGINE_WINDOW_MS  60
#define HRM_RESET_DELAY_MS    20

/* VC30x transport-private contract. The public contract for this driver is
 * the /dev/hrm0 POSIX ioctl interface in posix_ioctl_hrm.h. */
typedef enum
{
    HRM_VC30X_MODE_HR = 0,
    HRM_VC30X_MODE_SPO2,
    HRM_VC30X_MODE_HRV,
} hrm_vc30x_mode_t;

#define HRM_VC30X_SAMPLE_HZ          25u
#define HRM_VC30X_FIFO_PERIOD_MS     800u
#define HRM_VC30X_HRV_SAMPLE_HZ      100u
#define HRM_VC30X_HRV_FIFO_PERIOD_MS 200u
#define HRM_VC30X_MAX_SAMPLES        64u

typedef struct
{
    uint8_t chip_id;
    uint8_t version;
    bool link_ok;
    bool hr_ok;
    bool spo2_ok;
    uint16_t hr_raw;
    uint16_t spo2_red;
    uint16_t spo2_ir;
    uint8_t status;
    uint8_t event;
    uint8_t ps;
    uint8_t env;
    uint16_t bio_out;
    int last_error;
} hrm_vc30x_status_t;

typedef struct
{
    uint16_t ppg[HRM_VC30X_MAX_SAMPLES];
    uint8_t count;
    uint8_t env;
    uint8_t ps;
    uint8_t led_current;
    bool worn;
    bool wear_changed;
} hrm_vc30x_frame_t;

/* vc30x_common.h configuration, HR path. */
#define HRM_CFG_INTERRUPT_TIME  3u     /* 400 us */
#define HRM_CFG_PS_SLOT_CUR     0x4Au
#define HRM_CFG_PS_SLOT_RES     0x57u
#define HRM_CLK_HZ              32000u /* VC30x_CFG_CLK_CHECK_FREQUENCY */
#define HRM_CLK_CHECK_GAP_S     10u
#define HRM_CLK_CHECK_DEVIATION 3u
#define HRM_WEAR_PARAM_PS       128u
#define HRM_WEAR_PARAM_ENVMIN   64u
#define HRM_WEAR_HOLD_CNT       1
#define HRM_WEAR_DROP_CNT       3

/* vc30x_driver.h AGC constants. */
#define HRM_MAX_RES_LEVEL       21u
#define HRM_MAX_CUR_LEVEL       0x6Fu
#define HRM_ENV_MAX_TH          (8u * 16u)
#define HRM_ADJUST_INCREASE     22u /* 1.4 in Q4 */
#define HRM_ADJUST_DECREASE     11u /* 0.7 in Q4 */
#define HRM_ADJUST_STEP_MAX     32u
#define HRM_ADJUST_STEP_MIN     1u
#define HRM_PPG_HIGH            3770u
#define HRM_PPG_LOW             320u
#define HRM_PPG_AGC_MARK        0x1000u

static posix_fd_t s_bus = POSIX_FD_NULL;
static posix_fd_t s_sensor_en_fd = POSIX_FD_NULL;
static bool s_bus_checked;
static hrm_vc30x_mode_t s_mode = HRM_VC30X_MODE_HR;
static hrm_vc30x_status_t s_status;

static uint32_t hrm_sample_hz(void)
{
    return s_mode == HRM_VC30X_MODE_HRV ? HRM_VC30X_HRV_SAMPLE_HZ : HRM_VC30X_SAMPLE_HZ;
}

static uint32_t hrm_fifo_period_ms(void)
{
    return s_mode == HRM_VC30X_MODE_HRV ? HRM_VC30X_HRV_FIFO_PERIOD_MS : HRM_VC30X_FIFO_PERIOD_MS;
}

/* ---------------------------------------------------------------- *
 * I2C helpers
 * ---------------------------------------------------------------- */
static int hrm_bus_ready(void)
{
    if (s_bus_checked)
    {
        return s_bus == POSIX_FD_NULL ? -ENODEV : 0;
    }

    s_bus_checked = true;
    s_bus = posix_open(HRM_I2C_PATH);
    if (s_bus == POSIX_FD_NULL)
    {
        return -ENODEV;
    }

    posix_i2c_config_t cfg = {
        .speed_hz = POSIX_I2C_SPEED_FAST,
        .addr_bits = POSIX_I2C_ADDR_7BIT,
    };
    if (posix_ioctl(s_bus, POSIX_I2C_IOCTL_SET_CONFIG, &cfg) != POSIX_OK)
    {
        posix_close(s_bus);
        s_bus = POSIX_FD_NULL;
        return -EIO;
    }
    return 0;
}

static void hrm_sensor_enable(void)
{
    if (s_sensor_en_fd == POSIX_FD_NULL)
    {
        s_sensor_en_fd = posix_open(HRM_SENSOR_EN_PATH);
        if (s_sensor_en_fd == POSIX_FD_NULL)
        {
            return;
        }
        posix_gpio_config_t cfg = {
            .pin = 0,
            .direction = POSIX_GPIO_DIR_OUTPUT,
            .pull = POSIX_GPIO_PULL_NONE,
            .initial_value = 1,
        };
        (void)posix_ioctl(s_sensor_en_fd, POSIX_GPIO_IOCTL_SET_DIR, &cfg);
    }
    posix_gpio_value_t value = { .pin = 0, .value = 1 };
    (void)posix_ioctl(s_sensor_en_fd, POSIX_GPIO_IOCTL_SET_VALUE, &value);
}

static int hrm_wr(uint8_t reg, const uint8_t *buf, size_t len)
{
    int rc = hrm_bus_ready();
    if (rc != 0)
    {
        return rc;
    }
    posix_i2c_msg_t msg = {
        .addr = HRM_I2C_ADDR,
        .reg = reg,
        .reg_len = 1,
        .buf = (uint8_t *)buf,
        .len = (uint16_t)len,
    };
    return posix_ioctl(s_bus, POSIX_I2C_IOCTL_WRITE_REG, &msg);
}

static int hrm_wr8(uint8_t reg, uint8_t val)
{
    return hrm_wr(reg, &val, 1);
}

static int hrm_rd(uint8_t reg, uint8_t *buf, size_t len)
{
    int rc = hrm_bus_ready();
    if (rc != 0)
    {
        return rc;
    }
    posix_i2c_msg_t msg = {
        .addr = HRM_I2C_ADDR,
        .reg = reg,
        .reg_len = 1,
        .buf = buf,
        .len = (uint16_t)len,
    };
    return posix_ioctl(s_bus, POSIX_I2C_IOCTL_READ_REG, &msg);
}

/* ================================================================ *
 * HR path: vendor driver port
 * ================================================================ */

typedef enum
{
    AGC_DIR_NULL = 0,
    AGC_DIR_UP,
    AGC_DIR_DOWN,
} hrm_agc_dir_t;

typedef struct
{
    uint8_t max_current;
    uint8_t max_res_level;
    uint8_t record_env;
    uint8_t slot_env;
    uint8_t current;
    uint8_t res_level;
    hrm_agc_dir_t dir;
    hrm_agc_dir_t last_dir;
    uint8_t current_step;
} hrm_adjust_t;

typedef struct
{
    uint8_t res;
    uint8_t offset;
    uint8_t low;
} hrm_res_row_t;

/* PD resistor level -> (res, offset, low-res) register fields. */
static const hrm_res_row_t s_res_table[HRM_MAX_RES_LEVEL + 1u] = {
    {0, 0, 1}, {1, 0, 1}, {2, 0, 1}, {3, 0, 1}, {4, 0, 1}, {5, 0, 1},
    {0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}, {4, 0, 0}, {5, 0, 0},
    {6, 0, 0}, {7, 0, 0}, {6, 1, 0}, {7, 1, 0}, {6, 2, 0}, {7, 2, 0},
    {6, 3, 0}, {7, 3, 0}, {6, 4, 0}, {7, 4, 0},
};

/* Driver state. The HR path has a single caller (hrm_service's task), so
 * none of this needs a lock; the boot self-test finishes before that task
 * opens the sensor. */
static struct
{
    hrm_adjust_t adjust[3];     /* slot 0 = green LED, slot 2 = PS */
    uint8_t ppg_mark[HRM_VC30X_MAX_SAMPLES];
    uint8_t read_index;         /* FIFO read pointer, 0x80..0xFF */
    uint8_t work_status;
    uint8_t int_event;
    bool    led_on;             /* slot 0 enabled (worn) */
    /* oscillator calibration */
    bool     osc_ok;
    uint32_t osc_gap_cnt;
    uint32_t osc_attempts;
    uint8_t  osc_index;
    uint32_t osc_count[6];
    uint32_t clk_count[6];
    uint32_t last_fifo_clk;
    uint32_t last_clk_check;
    /* wear */
    bool    worn;
    int8_t  wear_hold_cnt;
    int8_t  wear_drop_cnt;
    uint8_t wear_ps;
    uint8_t wear_env;
    bool    moving;
    /* AGC oscillation guard (vc30x_drv_adjust_res_error statics) */
    hrm_agc_dir_t err_last_dir;
    uint8_t err_last_level;
    uint8_t err_last_cur[3];
    uint8_t err_cnt;
    uint16_t resize_qares;
    uint16_t resize_pdres;
    uint8_t  agc_last_index;
} s_hr;

static uint32_t hrm_clock(void)
{
    return posix_port_cycle_get_32();
}

static uint8_t hrm_absdiff(uint32_t a, uint32_t b)
{
    uint32_t d = (a > b) ? (a - b) : (b - a);
    return (d > 0xFFu) ? 0xFFu : (uint8_t)d;
}

/* ---- PD resistor / LED current ------------------------------------ */

static void hrm_set_current(uint8_t slot, uint8_t cur)
{
    uint8_t reg = 0;
    cur = (cur > 0x7Fu) ? 0x7Fu : cur;
    (void)hrm_rd((uint8_t)(HRM_REG_LED_CUR + slot), &reg, 1);
    reg &= (uint8_t)(1u << 7);
    reg |= cur;
    (void)hrm_wr8((uint8_t)(HRM_REG_LED_CUR + slot), reg);
}

static void hrm_set_resistance(uint8_t slot, uint8_t level)
{
    uint8_t res_ext[2] = {0};
    uint8_t res_reg = 0;
    uint8_t over = 0;

    level = (level > HRM_MAX_RES_LEVEL) ? HRM_MAX_RES_LEVEL : level;
    const hrm_res_row_t *row = &s_res_table[level];
    const uint8_t offset = (row->offset >= 4u) ? 0u : row->offset;
    const uint8_t offset3 = (row->offset >= 4u) ? 1u : 0u;

    (void)hrm_rd(HRM_REG_RES_EXT, res_ext, 2);
    if (slot == 0u)
    {
        res_ext[0] = (uint8_t)((res_ext[0] & ~(3u << 4)) | (offset << 4));
        res_ext[0] = (uint8_t)((res_ext[0] & ~1u) | row->low);
        res_ext[1] = (uint8_t)((res_ext[1] & ~1u) | offset3);
    }
    else if (slot == 1u)
    {
        res_ext[0] = (uint8_t)((res_ext[0] & ~(3u << 6)) | (offset << 6));
        res_ext[0] = (uint8_t)((res_ext[0] & ~(1u << 1)) | (row->low << 1));
        res_ext[1] = (uint8_t)((res_ext[1] & ~(1u << 2)) | (offset3 << 2));
    }
    (void)hrm_rd((uint8_t)(HRM_REG_PD_RES + slot), &res_reg, 1);
    res_reg = (uint8_t)((res_reg & 0x8Fu) | (row->res << 4));
    (void)hrm_wr8((uint8_t)(HRM_REG_PD_RES + slot), res_reg);
    (void)hrm_wr(HRM_REG_RES_EXT, res_ext, 2);

    /* HR mode: oversampling follows the offset level. */
    const uint8_t offset_lvl = (uint8_t)(offset3 * 4u + offset);
    const uint8_t oversample = (offset_lvl * 2u < 5u) ? (uint8_t)(offset_lvl * 2u) : 5u;
    (void)hrm_rd(HRM_REG_OVERSAMPLE, &over, 1);
    over = (uint8_t)((over & ~7u) | oversample | 0x80u);
    (void)hrm_wr8(HRM_REG_OVERSAMPLE, over);
}

static uint8_t hrm_res_level_of(uint8_t res_val, uint8_t low, uint8_t offset, uint8_t offset3)
{
    const uint8_t want_offset = offset3 ? 4u : offset;
    for (uint8_t i = 0; i <= HRM_MAX_RES_LEVEL; i++)
    {
        if (s_res_table[i].res == res_val && s_res_table[i].offset == want_offset &&
            s_res_table[i].low == low)
        {
            return i;
        }
    }
    return 0;
}

/* ---- slot on/off --------------------------------------------------- */

/* vc30x_drv_channel_disable(): drop a slot and arm the PS interrupt so the
 * wear decision keeps running on the PS channel alone. */
static void hrm_slot_disable(uint8_t slot)
{
    uint8_t reg = 0;
    (void)hrm_rd(HRM_REG_CFG, &reg, 1);
    reg &= (uint8_t)~(1u << slot);
    (void)hrm_wr8(HRM_REG_CFG, reg);
    (void)hrm_rd(HRM_REG_INT_CFG, &reg, 1);
    reg |= (uint8_t)(1u << 4);
    (void)hrm_wr8(HRM_REG_INT_CFG, reg);
    (void)hrm_wr8(HRM_REG_FIFO_DIV, (uint8_t)(HRM_VC30X_FIFO_PERIOD_MS / HRM_FIFO_GAP_MS - 1u));
}

/* ---- configuration (vc30x_drv_reg_init, WORK_MODE_HR) --------------- */

static void hrm_hr_state_reset(void)
{
    memset(&s_hr, 0, sizeof(s_hr));
    for (int i = 0; i < 2; i++)
    {
        s_hr.adjust[i].max_current = HRM_MAX_CUR_LEVEL;
        s_hr.adjust[i].max_res_level = HRM_MAX_RES_LEVEL;
        s_hr.adjust[i].current_step = 4;
    }
    s_hr.adjust[2].max_res_level = HRM_MAX_RES_LEVEL;
    s_hr.read_index = HRM_FIFO_BASE;
    /* VC30x_CFG_WEAR_DEFAULT_STATUS = WEAR_IS_HOLD */
    s_hr.worn = true;
    s_hr.led_on = true;
    s_hr.wear_hold_cnt = HRM_WEAR_HOLD_CNT;
    s_hr.wear_drop_cnt = HRM_WEAR_DROP_CNT;
}

static void hrm_osc_clear(void)
{
    s_hr.osc_gap_cnt = 0;
    s_hr.osc_attempts = 0;
    s_hr.osc_index = 0;
    s_hr.last_fifo_clk = 0;
    s_hr.osc_ok = false;
    s_hr.last_clk_check = 0;
}

static int hrm_hr_configure(bool led_on)
{
    const uint32_t sample_hz = hrm_sample_hz();
    const uint8_t fifo_div = (uint8_t)(hrm_fifo_period_ms() / (1000u / sample_hz) - 1u);
    const uint16_t clk_div = (uint16_t)(20000u / sample_hz - 1u);
    uint8_t cfg[17] = {0x00, 0x1D, 0x8A, 0x00, 0x03, 0x1F, 0x00, 0x00, 0x80,
                       0x00, 0x57, 0x57, 0x57, 0x16, 0x56, 0x16, 0x00};
    const uint8_t res_ext = 0x00;
    const uint8_t bio_cfg[2] = {0x00, 0xA2};
    const uint8_t temp_cfg = 0x00;

    cfg[1] |= (uint8_t)(HRM_CFG_INTERRUPT_TIME << 5);
    cfg[3] = fifo_div;
    cfg[4] = (uint8_t)(clk_div >> 8);
    cfg[5] = (uint8_t)clk_div;
    cfg[9] = HRM_CFG_PS_SLOT_CUR;
    cfg[12] = HRM_CFG_PS_SLOT_RES;
    cfg[0] = 0x05;            /* slot 0 (green) + slot 2 (PS) */
    cfg[10] = 0x57;
    cfg[13] = 0x16;
    cfg[16] = 0x00;
    cfg[1] &= (uint8_t)~(1u << 4);
    cfg[6] = 0x00;            /* the vendor HR mode keeps reg 0x16 at 0 */

    if (hrm_wr(HRM_REG_CFG, cfg, sizeof(cfg)) != 0 ||
        hrm_wr8(HRM_REG_RES_EXT, res_ext) != 0 ||
        hrm_wr(HRM_REG_BIO_CFG, bio_cfg, sizeof(bio_cfg)) != 0 ||
        hrm_wr8(HRM_REG_TEMP_CFG, temp_cfg) != 0)
    {
        return -EIO;
    }

    /* vc30x_drv_system_param_init(): AGC limits, FIFO pointer, clock. */
    for (int i = 0; i < 2; i++)
    {
        s_hr.adjust[i].max_current = HRM_MAX_CUR_LEVEL;
        s_hr.adjust[i].max_res_level = HRM_MAX_RES_LEVEL;
        s_hr.adjust[i].record_env = 0;
        s_hr.adjust[i].dir = AGC_DIR_NULL;
        s_hr.adjust[i].last_dir = AGC_DIR_NULL;
        s_hr.adjust[i].current_step = 4;
    }
    s_hr.adjust[2].max_res_level = HRM_MAX_RES_LEVEL;
    s_hr.read_index = HRM_FIFO_BASE;
    hrm_osc_clear();
    s_hr.led_on = led_on;
    if (!led_on)
    {
        hrm_slot_disable(0);
    }
    /* vc30x_drv_start_sampling() */
    uint8_t reg = 0;
    if (hrm_rd(HRM_REG_CFG, &reg, 1) != 0)
    {
        return -EIO;
    }
    return hrm_wr8(HRM_REG_CFG, (uint8_t)(reg | (1u << 7)));
}

/* ---- oscillator calibration (vc30x_drv_calibration_osc_clk) ---------- */

static void hrm_osc_calibrate(void)
{
    const uint32_t gap_polls = HRM_CLK_CHECK_GAP_S * 1000u / hrm_fifo_period_ms();
    if (s_hr.osc_ok && s_hr.osc_gap_cnt++ <= gap_polls)
    {
        return;
    }
    /* The vendor waits for a valid calibration forever and drops every FIFO
     * frame meanwhile. A preempted I2C read can keep the 3-tick deviation
     * check failing, so after one check period run on the nominal divider
     * (the chip default) rather than never producing data. */
    if (!s_hr.osc_ok && ++s_hr.osc_attempts > gap_polls)
    {
        APP_LOGW("[vc30] osc calibration did not converge, using nominal divider");
        s_hr.osc_ok = true;
        s_hr.osc_gap_cnt = 0;
        return;
    }

    uint8_t osc_buf[4] = {0};
    const uint32_t clk0 = hrm_clock();
    if (hrm_rd(HRM_REG_OSC_COUNT, osc_buf, 4) != 0)
    {
        return;
    }
    const uint32_t clk1 = hrm_clock();
    const uint32_t osc0 = ((uint32_t)osc_buf[0] << 8) | osc_buf[1];
    const uint32_t osc1 = ((uint32_t)osc_buf[2] << 8) | osc_buf[3];
    if (osc0 > osc1)
    {
        s_hr.osc_index = 0;
        return;
    }

    const uint8_t i = s_hr.osc_index;
    s_hr.osc_count[i * 2] = osc0;
    s_hr.osc_count[i * 2 + 1] = osc1;
    s_hr.clk_count[i * 2] = clk0;
    s_hr.clk_count[i * 2 + 1] = clk1;
    if (++s_hr.osc_index < 3u)
    {
        return;
    }
    s_hr.osc_index = 0;

    /* The chip counter is 16 bits at ~20 kHz and wraps every ~3 s, i.e.
     * often inside the 1.6 s window; take its deltas modulo 2^16. */
    bool valid = true;
    for (int k = 0; k < 2; k++)
    {
        const uint32_t dclk_a = s_hr.clk_count[2 * k + 2] - s_hr.clk_count[2 * k];
        const uint32_t dclk_b = s_hr.clk_count[2 * k + 3] - s_hr.clk_count[2 * k + 1];
        const uint32_t dosc_a = (s_hr.osc_count[2 * k + 2] - s_hr.osc_count[2 * k]) & 0xFFFFu;
        const uint32_t dosc_b = (s_hr.osc_count[2 * k + 3] - s_hr.osc_count[2 * k + 1]) & 0xFFFFu;
        valid = valid && hrm_absdiff(dclk_a, dclk_b) < HRM_CLK_CHECK_DEVIATION;
        valid = valid && hrm_absdiff(dosc_a, dosc_b) < HRM_CLK_CHECK_DEVIATION;
        valid = valid && dclk_a != 0u;
    }
    if (!valid)
    {
        return;
    }

    uint32_t div[2];
    const uint32_t nominal = 20000u / hrm_sample_hz();
    for (int k = 0; k < 2; k++)
    {
        const uint32_t dosc = (s_hr.osc_count[2 * k + 2] - s_hr.osc_count[2 * k]) & 0xFFFFu;
        const uint32_t dclk = s_hr.clk_count[2 * k + 2] - s_hr.clk_count[2 * k];
        div[k] = dosc * HRM_CLK_HZ / hrm_sample_hz() / dclk - 1u;
        if (div[k] * 4u < nominal * 3u || div[k] * 4u > nominal * 5u)
        {
            return;
        }
    }
    /* The vendor's "(a - b <= 20) || (b - a <= 20)" is always true for
     * unsigned operands; keep the intent (the two estimates agree). */
    if (hrm_absdiff(div[0], div[1]) > 20u)
    {
        return;
    }
    const uint16_t mean = (uint16_t)((div[0] + div[1]) / 2u);
    const uint8_t reg[2] = {(uint8_t)(mean >> 8), (uint8_t)mean};
    (void)hrm_wr(HRM_REG_CLK_DIV, reg, 2);
    if (!s_hr.osc_ok)
    {
        APP_LOGI("[vc30] osc calibrated: clk_div=%u (nominal %u)",
                 (unsigned)mean, (unsigned)(nominal - 1u));
    }
    s_hr.osc_gap_cnt = 0;
    s_hr.osc_ok = true;
}

/* ---- AGC (vc30x_drv_status_event_verify and helpers) ---------------- */

static void hrm_mark_fifo(uint8_t slot)
{
    uint8_t windex = 0;
    if (hrm_rd(HRM_REG_FIFO_INDEX, &windex, 1) != 0)
    {
        return;
    }
    const uint8_t rindex = s_hr.read_index;
    const uint8_t offset = (windex > rindex) ? (uint8_t)(windex - rindex)
                                             : (uint8_t)(windex + 128u - rindex);
    if (offset < 2u || offset > 128u)
    {
        return;
    }
    const int idx = offset / 2 - 1 + slot;
    if (idx >= 0 && idx < (int)HRM_VC30X_MAX_SAMPLES)
    {
        s_hr.ppg_mark[idx] = 1;
    }
}

static bool hrm_agc_oscillating(hrm_agc_dir_t dir, uint8_t level, uint8_t cur)
{
    if (s_hr.err_last_dir != dir && s_hr.err_last_level == level)
    {
        s_hr.err_last_cur[0] = s_hr.err_last_cur[1];
        s_hr.err_last_cur[1] = s_hr.err_last_cur[2];
        s_hr.err_last_cur[2] = cur;
        if (s_hr.err_last_cur[0] == s_hr.err_last_cur[2])
        {
            s_hr.err_cnt++;
        }
    }
    else
    {
        s_hr.err_cnt = 0;
    }
    s_hr.err_last_dir = dir;
    s_hr.err_last_level = level;
    if (s_hr.err_cnt >= 3u)
    {
        s_hr.err_cnt = 0;
        return true;
    }
    return false;
}

static void hrm_agc_step(hrm_adjust_t *a, uint8_t thh_res, uint8_t thh_cur, uint8_t slot)
{
    if (a->current == thh_cur)
    {
        if (a->res_level == thh_res)
        {
            return; /* RET_PPG_ERR: already at the limit */
        }
        if (a->dir == AGC_DIR_UP)
        {
            a->res_level = (a->res_level >= thh_res) ? thh_res : (uint8_t)(a->res_level + 1u);
        }
        else
        {
            a->res_level = (a->res_level < 1u) ? 0u : (uint8_t)(a->res_level - 1u);
        }
        hrm_mark_fifo(slot);
    }
    else
    {
        uint16_t step = a->current_step;
        if (a->last_dir == AGC_DIR_NULL)
        {
            step *= 16u;
        }
        else if (a->dir == a->last_dir)
        {
            step = (step == 1u || step == 2u) ? (uint16_t)((step + 1u) * 16u)
                                              : (uint16_t)(step * HRM_ADJUST_INCREASE);
        }
        else
        {
            step = (uint16_t)(step * HRM_ADJUST_DECREASE);
        }
        step >>= 4;
        step = (step <= HRM_ADJUST_STEP_MIN) ? HRM_ADJUST_STEP_MIN : step;
        step = (step >= HRM_ADJUST_STEP_MAX) ? HRM_ADJUST_STEP_MAX : step;
        a->current_step = (uint8_t)step;
        if (a->dir == AGC_DIR_UP)
        {
            a->current = ((uint16_t)a->current + step > thh_cur) ? thh_cur
                                                                 : (uint8_t)(a->current + step);
        }
        else
        {
            a->current = (a->current < step) ? thh_cur : (uint8_t)(a->current - step);
        }
        hrm_mark_fifo(slot);
        a->last_dir = a->dir;
    }
    if (hrm_agc_oscillating(a->dir, a->res_level, a->current) && a->res_level >= 13u)
    {
        a->res_level--;
    }
    hrm_set_resistance(slot, a->res_level);
    hrm_set_current(slot, a->current);
}

static void hrm_agc_ppg(hrm_adjust_t *a, uint8_t slot)
{
    uint8_t windex = 0;
    uint8_t buf[2] = {0};
    /* vc30x_drv_adjust_ppg_read(): newest FIFO word of this slot. */
    if (hrm_rd(HRM_REG_FIFO_INDEX, &windex, 1) != 0)
    {
        return;
    }
    uint8_t addr = (uint8_t)(windex - 2u + slot * 2u);
    if (addr < HRM_FIFO_BASE)
    {
        addr = (uint8_t)(addr + HRM_FIFO_BASE);
    }
    if (hrm_rd(addr, buf, 2) != 0)
    {
        return;
    }
    /* A poll never lands exactly one sample after the previous one, so the
     * vendor's "index gap != slot_num * 2" check always resets the history. */
    if ((uint8_t)(windex - s_hr.agc_last_index) != 2u)
    {
        a->last_dir = AGC_DIR_NULL;
    }
    s_hr.agc_last_index = windex;
    const uint16_t data = (uint16_t)((buf[0] << 8) | buf[1]);
    if (data >= HRM_PPG_HIGH)
    {
        a->dir = AGC_DIR_UP;
        hrm_agc_step(a, a->max_res_level, a->max_current, slot);
    }
    else if (data < HRM_PPG_LOW)
    {
        a->dir = AGC_DIR_DOWN;
        hrm_agc_step(a, 0, 0, slot);
    }
}

static void hrm_agc_overload(hrm_adjust_t *a, uint8_t slot, uint8_t status)
{
    const uint8_t mask[3] = {HRM_STATUS_OVERLOAD0, HRM_STATUS_OVERLOAD1, HRM_STATUS_OVERLOAD2};
    if ((status & mask[slot]) && a->current != 0u)
    {
        a->current--;
        a->max_current = a->current;
        hrm_mark_fifo(slot);
        hrm_set_current(slot, a->current);
    }
}

static void hrm_agc_res_max_resize(hrm_adjust_t *a)
{
    if (a->max_res_level >= HRM_MAX_RES_LEVEL)
    {
        return;
    }
    const uint8_t min_env = (a->record_env / 2u < HRM_ENV_MAX_TH) ? (uint8_t)(a->record_env / 2u)
                                                                  : (uint8_t)HRM_ENV_MAX_TH;
    if (a->slot_env < min_env)
    {
        if (a->slot_env <= 1u)
        {
            s_hr.resize_pdres = 0;
            s_hr.resize_qares = (s_hr.resize_qares == 30u) ? 0u : (uint16_t)(s_hr.resize_qares + 1u);
            if (s_hr.resize_qares == 30u)
            {
                a->max_res_level = HRM_MAX_RES_LEVEL;
            }
        }
        else
        {
            s_hr.resize_qares = 0;
            s_hr.resize_pdres++;
            if (s_hr.resize_pdres == 120u)
            {
                a->max_res_level++;
            }
        }
    }
    else
    {
        s_hr.resize_pdres = 0;
        s_hr.resize_qares = 0;
    }
}

static void hrm_agc_verify(void)
{
    if (s_hr.work_status & (HRM_STATUS_CONFLICT | HRM_STATUS_INSAMPLE))
    {
        return;
    }
    if (!s_hr.led_on)
    {
        return; /* only the PS slot runs; nothing to regulate */
    }
    hrm_adjust_t *a = &s_hr.adjust[0];
    hrm_agc_res_max_resize(a);
    if (s_hr.int_event & HRM_EVENT_OV)
    {
        hrm_agc_overload(a, 0, s_hr.work_status);
    }
    if (s_hr.int_event & HRM_EVENT_PPG)
    {
        hrm_agc_ppg(a, 0);
    }
    else
    {
        a->dir = AGC_DIR_NULL;
        a->last_dir = AGC_DIR_NULL;
    }
}

/* ---- register snapshot (vc30x_drv_read_reg) ------------------------- */

static int hrm_read_regs(uint8_t *env_nibble)
{
    uint8_t pre[3] = {0};
    uint8_t cur[6] = {0};
    uint8_t aval[6] = {0};
    uint8_t bias[4] = {0};
    uint8_t res_ext[2] = {0};
    uint8_t bio[4] = {0};

    if (hrm_rd(HRM_REG_PRE_ENV, pre, 3) != 0 ||
        hrm_rd(HRM_REG_LED_CUR, cur, 6) != 0 ||
        hrm_rd(HRM_REG_ENV_PS, aval, 6) != 0 ||
        hrm_rd(HRM_REG_PDBIAS, bias, 4) != 0 ||
        hrm_rd(HRM_REG_RES_EXT, res_ext, 2) != 0 ||
        hrm_rd(HRM_REG_BIO, bio, 4) != 0)
    {
        return -EIO;
    }

    const uint8_t ps_nibble = pre[2] & 0x0Fu;
    *env_nibble = (pre[0] >> 4) & 0x0Fu;

    /* Slot env comes from 0x47.. (the vendor overwrites aenv with it). */
    s_hr.adjust[0].slot_env = bias[0];
    s_hr.adjust[0].current = cur[0] & 0x7Fu;
    s_hr.adjust[0].res_level = hrm_res_level_of((cur[3] >> 4) & 0x07u,
                                                res_ext[0] & 0x01u,
                                                (res_ext[0] >> 4) & 0x03u,
                                                res_ext[1] & 0x01u);
    s_hr.adjust[2].slot_env = bias[2];
    s_hr.adjust[2].current = cur[2] & 0x7Fu;

    const uint8_t aps = aval[5];
    const uint16_t pdbias = (uint16_t)(bias[2] * 16u + (bias[3] & 0x0Fu));
    /* Only accept the averaged PS when it agrees with the live nibble. The
     * vendor stores the 12-bit pdbias into a u8 (silently truncating); clamp
     * instead, so bright ambient light reads as bright, not as dark. */
    if ((aps >> 4) == ps_nibble)
    {
        s_hr.wear_ps = aps;
        s_hr.wear_env = (pdbias > 0xFFu) ? 0xFFu : (uint8_t)pdbias;
    }

    s_status.ps = aps;
    s_status.env = aval[0];
    s_status.bio_out = (uint16_t)((bio[2] << 8) | bio[3]);
    return 0;
}

/* ---- running check (vc30x_drv_check_running_gaptime) ---------------- */

static bool hrm_running_too_soon(void)
{
    uint8_t cnt[4] = {0};
    if (hrm_rd(HRM_REG_OSC_COUNT, cnt, 4) != 0)
    {
        return false;
    }
    const uint32_t c1 = ((uint32_t)cnt[0] << 8) | cnt[1];
    const uint32_t c2 = ((uint32_t)cnt[2] << 8) | cnt[3];
    const uint32_t diff = (c1 <= c2) ? (c2 - c1) : (c2 - c1 + 0xFFFFu);
    const uint32_t now = (diff <= 5u) ? c1 : 0u;
    const uint32_t last = s_hr.last_clk_check;
    uint32_t gap = 8000u;
    if (now != 0u && last != 0u)
    {
        gap = (last <= now) ? (now - last) : (now - last + 0xFFFFu);
    }
    s_hr.last_clk_check = now;
    const uint32_t check_ms = (1000u / hrm_sample_hz()) * 3u / 4u;
    return gap <= 20u * check_ms;
}

/* ---- FIFO (vc30x_drv_fifo_infomation_check / read_fifo_data) -------- */

static void hrm_fifo_read(hrm_vc30x_frame_t *frame)
{
    uint8_t windex = 0;
    if (hrm_rd(HRM_REG_FIFO_INDEX, &windex, 1) != 0)
    {
        return;
    }

    /* fifo_infomation_check: a gap longer than the FIFO can hold means
     * the pointer wrapped unseen; restart from the write pointer. */
    const uint32_t now = hrm_clock();
    const uint32_t elapsed = now - s_hr.last_fifo_clk;
    const uint32_t limit = (uint32_t)((64u * HRM_CLK_HZ * 8u) / (hrm_sample_hz() * 10u));
    s_hr.last_fifo_clk = now;
    if (elapsed > limit)
    {
        s_hr.read_index = HRM_FIFO_BASE;
        return;
    }
    if (windex < HRM_FIFO_BASE)
    {
        s_hr.read_index = (uint8_t)(s_hr.read_index + hrm_fifo_period_ms() / HRM_FIFO_GAP_MS * 2u);
        if (s_hr.read_index < HRM_FIFO_BASE)
        {
            s_hr.read_index = (uint8_t)(s_hr.read_index + HRM_FIFO_BASE);
        }
        return;
    }
    if (s_hr.read_index < HRM_FIFO_BASE)
    {
        s_hr.read_index = windex;
        return;
    }
    if (windex == s_hr.read_index || !s_hr.osc_ok)
    {
        s_hr.read_index = windex;
        return;
    }

    uint8_t raw[128];
    size_t len;
    const uint8_t rindex = s_hr.read_index;
    if (windex > rindex)
    {
        len = (size_t)(windex - rindex);
        if (hrm_rd(rindex, raw, len) != 0)
        {
            return;
        }
    }
    else
    {
        /* Wrapped: [rindex, 0x100) then [0x80, windex). */
        const size_t head = 0x100u - rindex;
        const size_t tail = (size_t)(windex - HRM_FIFO_BASE);
        len = head + tail;
        if (hrm_rd(rindex, raw, head) != 0 ||
            (tail > 0u && hrm_rd(HRM_FIFO_BASE, &raw[head], tail) != 0))
        {
            return;
        }
    }
    s_hr.read_index = windex;

    size_t count = len / 2u;
    if (count > HRM_VC30X_MAX_SAMPLES)
    {
        count = HRM_VC30X_MAX_SAMPLES;
    }
    for (size_t i = 0; i < count; i++)
    {
        uint16_t v = (uint16_t)((raw[i * 2] << 8) | raw[i * 2 + 1]);
        if (s_hr.ppg_mark[i])
        {
            s_hr.ppg_mark[i] = 0;
            v |= HRM_PPG_AGC_MARK;
        }
        frame->ppg[i] = v;
    }
    frame->count = (uint8_t)count;
    if (count > 0u)
    {
        s_status.hr_raw = frame->ppg[count - 1u] & 0x0FFFu;
    }
}

/* ---- wear (vc30x_drv_get_wear_status, BIO disabled) ------------------ */

static bool hrm_wear_update(void)
{
    const uint8_t ps = s_hr.wear_ps;
    const uint8_t env = s_hr.wear_env;
    bool changed = false;

    if (!s_hr.worn)
    {
        /* DROP -> HOLD: skin close (PS) and dark (ENV). The vendor also
         * requires BIO hold and, with ACT enabled, motion; both disabled. */
        if (ps >= HRM_WEAR_PARAM_PS && env <= HRM_WEAR_PARAM_ENVMIN)
        {
            if (--s_hr.wear_hold_cnt <= 0)
            {
                s_hr.wear_hold_cnt = HRM_WEAR_HOLD_CNT;
                s_hr.wear_drop_cnt = HRM_WEAR_DROP_CNT;
                s_hr.worn = true;
                changed = true;
                /* Re-run the full HR configuration with the LED enabled. */
                (void)hrm_hr_configure(true);
            }
        }
        else
        {
            s_hr.wear_hold_cnt = HRM_WEAR_HOLD_CNT;
        }
    }
    else
    {
        if (ps < HRM_WEAR_PARAM_PS || env > HRM_WEAR_PARAM_ENVMIN)
        {
            if (--s_hr.wear_drop_cnt <= 0)
            {
                s_hr.wear_hold_cnt = HRM_WEAR_HOLD_CNT;
                s_hr.wear_drop_cnt = HRM_WEAR_DROP_CNT;
                s_hr.worn = false;
                changed = true;
                hrm_slot_disable(0);
                s_hr.led_on = false;
            }
        }
        else
        {
            s_hr.wear_drop_cnt = HRM_WEAR_DROP_CNT;
        }
    }
    return changed;
}

/* ---- public HR API -------------------------------------------------- */

static int hrm_vc30x_hr_open(void)
{
    int rc = hrm_bus_ready();
    if (rc != 0)
    {
        return rc;
    }
    rc = hrm_wr8(HRM_REG_SOFT_RESET, HRM_SOFT_RESET_CODE);
    if (rc != 0)
    {
        s_status.last_error = rc;
        return rc;
    }
    posix_port_delay_ms(HRM_RESET_DELAY_MS);
    s_mode = HRM_VC30X_MODE_HR;
    hrm_hr_state_reset();
    rc = hrm_hr_configure(true);
    s_status.last_error = rc;
    return rc;
}

static int hrm_vc30x_hrv_open(void)
{
    int rc = hrm_bus_ready();
    if (rc != 0)
    {
        return rc;
    }
    rc = hrm_wr8(HRM_REG_SOFT_RESET, HRM_SOFT_RESET_CODE);
    if (rc != 0)
    {
        s_status.last_error = rc;
        return rc;
    }
    posix_port_delay_ms(HRM_RESET_DELAY_MS);
    s_mode = HRM_VC30X_MODE_HRV;
    hrm_hr_state_reset();
    rc = hrm_hr_configure(true);
    s_status.last_error = rc;
    return rc;
}

static int hrm_vc30x_hr_close(void)
{
    uint8_t reg = 0;
    int rc = hrm_rd(HRM_REG_CFG, &reg, 1);
    if (rc == 0)
    {
        /* Sampling off and every slot disabled: LEDs dark. */
        rc = hrm_wr8(HRM_REG_CFG, (uint8_t)(reg & ~((1u << 7) | 0x07u)));
    }
    s_hr.led_on = false;
    s_status.last_error = rc;
    return rc;
}

static void hrm_vc30x_set_motion(bool moving)
{
    s_hr.moving = moving;
}

static int hrm_vc30x_poll(hrm_vc30x_frame_t *frame)
{
    uint8_t env_nibble = 0;
    memset(frame, 0, sizeof(*frame));

    int rc = hrm_rd(HRM_REG_WORK_STATUS, &s_hr.work_status, 1);
    if (rc == 0)
    {
        rc = hrm_rd(HRM_REG_INT_EVENT, &s_hr.int_event, 1);
    }
    if (rc != 0)
    {
        s_status.last_error = rc;
        return rc;
    }
    s_status.status = s_hr.work_status;
    s_status.event = s_hr.int_event;

    /* Timer-driven: treat every poll as a FIFO + PS event. */
    const bool status_ok = (s_hr.work_status & HRM_STATUS_ERROR_MASK) == 0u;
    if (status_ok && !hrm_running_too_soon() && hrm_read_regs(&env_nibble) == 0)
    {
        hrm_agc_verify();
        hrm_osc_calibrate();
        if (s_hr.led_on)
        {
            hrm_fifo_read(frame);
        }
        frame->wear_changed = hrm_wear_update();
    }

    frame->env = env_nibble;
    frame->ps = s_hr.wear_ps;
    frame->led_current = s_hr.adjust[0].current;
    frame->worn = s_hr.worn;
    if (!s_hr.worn)
    {
        frame->count = 0;
    }
    s_status.last_error = 0;
    return 0;
}

static int hrm_vc30x_hrv_poll(hrm_vc30x_frame_t *frame)
{
    return hrm_vc30x_poll(frame);
}

/* ================================================================ *
 * Raw access + boot self-test (first bring-up path, FIFO off)
 * ================================================================ */

static int hrm_configure(hrm_vc30x_mode_t mode)
{
    uint8_t cfg[17] = {
        0x00, 0x1D, 0x8A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
        0x00, 0x57, 0x57, 0x57, 0x16, 0x56, 0x16, 0x00,
    };
    const uint8_t res_ext = 0x00;
    const uint8_t bio_cfg[2] = {0x00, 0xA2};
    const uint8_t temp_cfg = 0x00;
    const uint8_t fifo_div = (uint8_t)(HRM_FIFO_GAP_MS / (1000u / HRM_VC30X_SAMPLE_HZ) - 1u);
    const uint16_t clk_div = (uint16_t)(20000u / HRM_VC30X_SAMPLE_HZ - 1u);

    cfg[1] |= (uint8_t)(HRM_CFG_INTERRUPT_TIME << 5);
    cfg[3] = fifo_div;
    cfg[6] = fifo_div;
    cfg[4] = (uint8_t)((clk_div >> 8) & 0xFFu);
    cfg[5] = (uint8_t)(clk_div & 0xFFu);
    cfg[9] = HRM_CFG_PS_SLOT_CUR;
    cfg[12] = HRM_CFG_PS_SLOT_RES;

    if (mode == HRM_VC30X_MODE_SPO2)
    {
        cfg[0] = 0x07;      /* slots 0,1 (red/IR) + slot 2 (PS) */
        cfg[1] &= (uint8_t)~(1u << 4);
        cfg[6] = 0x00;      /* FIFO disabled: read the frame directly */
        cfg[7] = 0x80;
        cfg[13] = 0xB6;
        cfg[14] = 0x76;
        cfg[16] = 0x04;
    }
    else
    {
        cfg[0] = 0x05;      /* slot 0 (LED/PD) + slot 2 (PS) */
        cfg[1] &= (uint8_t)~(1u << 4);
        cfg[6] = 0x00;
        cfg[10] = 0x57;
        cfg[13] = 0x16;
        cfg[16] = 0x00;
    }

    if (hrm_wr(HRM_REG_CFG, cfg, sizeof(cfg)) != 0 ||
        hrm_wr8(HRM_REG_RES_EXT, res_ext) != 0 ||
        hrm_wr(HRM_REG_BIO_CFG, bio_cfg, sizeof(bio_cfg)) != 0 ||
        hrm_wr8(HRM_REG_TEMP_CFG, temp_cfg) != 0)
    {
        return -EIO;
    }
    s_mode = mode;
    return 0;
}

static int hrm_set_sampling(bool on)
{
    uint8_t reg = 0;
    int rc = hrm_rd(HRM_REG_CFG, &reg, 1);
    if (rc != 0)
    {
        return rc;
    }
    if (on)
    {
        reg |= (uint8_t)(1u << 7);
    }
    else
    {
        reg &= (uint8_t)~(1u << 7);
    }
    return hrm_wr8(HRM_REG_CFG, reg);
}

static int hrm_vc30x_start(hrm_vc30x_mode_t mode)
{
    int rc;
    if (mode != s_mode)
    {
        rc = hrm_configure(mode);
        if (rc != 0)
        {
            s_status.last_error = rc;
            return rc;
        }
    }
    rc = hrm_set_sampling(true);
    if (rc != 0)
    {
        s_status.last_error = rc;
    }
    return rc;
}

static int hrm_vc30x_stop(void)
{
    int rc = hrm_set_sampling(false);
    if (rc != 0)
    {
        s_status.last_error = rc;
    }
    return rc;
}

static int hrm_vc30x_reset(hrm_vc30x_mode_t mode)
{
    int rc = hrm_wr8(HRM_REG_SOFT_RESET, HRM_SOFT_RESET_CODE);
    if (rc != 0)
    {
        s_status.last_error = rc;
        return rc;
    }
    posix_port_delay_ms(HRM_RESET_DELAY_MS);
    rc = hrm_configure(mode);
    if (rc != 0)
    {
        s_status.last_error = rc;
    }
    return rc;
}

/*
 * Internal sampling-engine counter (reg 0x07: two 16-bit big-endian counts).
 *
 * While sampling is enabled the counter must advance. A frozen counter means
 * the chip is answering I2C but its PPG state machine is not running (stuck
 * reset, PLL not locked, wrong clock divider).
 */
static int hrm_read_osc_count(uint16_t *count)
{
    uint8_t raw[4] = {0};
    int rc = hrm_rd(HRM_REG_OSC_COUNT, raw, sizeof(raw));
    if (rc != 0)
    {
        return rc;
    }
    *count = (uint16_t)(((uint16_t)raw[0] << 8) | raw[1]);
    return 0;
}

/* Wait until the sensor reports a fresh sample, then return its event bits. */
static int hrm_wait_event(uint8_t *event, int timeout_ms)
{
    int waited = 0;
    while (waited <= timeout_ms)
    {
        uint8_t ev = 0;
        uint8_t status = 0;
        int rc = hrm_rd(HRM_REG_WORK_STATUS, &status, 1);
        if (rc != 0)
        {
            return rc;
        }
        s_status.status = status;
        if ((status & HRM_STATUS_ERROR_MASK) != 0)
        {
            return -EIO;
        }

        rc = hrm_rd(HRM_REG_INT_EVENT, &ev, 1);
        if (rc != 0)
        {
            return rc;
        }
        s_status.event = ev;
        if ((ev & (HRM_EVENT_PPG | HRM_EVENT_FIFO | HRM_EVENT_PS)) != 0)
        {
            *event = ev;
            return 0;
        }
        posix_port_delay_ms(5);
        waited += 5;
    }
    return -EAGAIN;
}

/*
 * Read the most recent frame. The self-test configuration leaves the FIFO
 * disabled, so the newest frame sits at 0x80 (vc30x_drv_read_fifo_data with
 * enfifo == 0).
 */
static int hrm_read_frame(uint8_t slots, uint16_t *out)
{
    uint8_t raw[4] = {0};
    const size_t bytes = (size_t)slots * 2u;
    int rc = hrm_rd((uint8_t)HRM_FIFO_BASE, raw, bytes);
    if (rc != 0)
    {
        return rc;
    }
    for (uint8_t i = 0; i < slots; i++)
    {
        out[i] = (uint16_t)(((uint16_t)raw[i * 2] << 8) | raw[i * 2 + 1]);
    }
    return 0;
}

static void hrm_read_extras(void)
{
    uint8_t buf[4] = {0};

    if (hrm_rd(HRM_REG_ENV_PS, buf, 1) == 0)
    {
        s_status.env = buf[0];
    }
    if (hrm_rd(HRM_REG_PS, buf, 1) == 0)
    {
        s_status.ps = buf[0];
    }
    if (hrm_rd(HRM_REG_BIO, buf, 4) == 0)
    {
        s_status.bio_out = (uint16_t)(((uint16_t)buf[2] << 8) | buf[3]);
    }
}

static int hrm_vc30x_read_hr(uint16_t *ppg)
{
    uint8_t ev = 0;
    uint16_t frame[1] = {0};
    int rc = hrm_wait_event(&ev, HRM_EVENT_TIMEOUT_MS);
    if (rc != 0)
    {
        return rc;
    }
    rc = hrm_read_frame(1, frame);
    if (rc != 0)
    {
        return rc;
    }
    hrm_read_extras();
    *ppg = frame[0];
    s_status.hr_raw = frame[0];
    return 0;
}

static int hrm_vc30x_read_spo2(uint16_t *red, uint16_t *ir)
{
    uint8_t ev = 0;
    uint16_t frame[2] = {0};
    int rc = hrm_wait_event(&ev, HRM_EVENT_TIMEOUT_MS);
    if (rc != 0)
    {
        return rc;
    }
    rc = hrm_read_frame(2, frame);
    if (rc != 0)
    {
        return rc;
    }
    hrm_read_extras();
    *red = frame[0];
    *ir = frame[1];
    s_status.spo2_red = frame[0];
    s_status.spo2_ir = frame[1];
    return 0;
}

static const hrm_vc30x_status_t *hrm_vc30x_status(void)
{
    return &s_status;
}

/*
 * Judge "the sampling engine is alive" from the internal counter, not from
 * the PPG value. A bare sensor sitting on the bench with no finger returns a
 * constant rail code (observed: ppg=1024, red=1024, ir=256), which looks
 * perfectly plausible to a "non-zero" test yet proves nothing.
 */
static int hrm_check_engine_alive(uint16_t *first, uint16_t *last)
{
    uint16_t a = 0;
    uint16_t b = 0;
    int rc = hrm_read_osc_count(&a);
    if (rc != 0)
    {
        return rc;
    }
    posix_port_delay_ms(HRM_ENGINE_WINDOW_MS);
    rc = hrm_read_osc_count(&b);
    if (rc != 0)
    {
        return rc;
    }
    *first = a;
    *last = b;
    return (a != b) ? 0 : -ENODATA;
}

static int hrm_vc30x_boot_test(void)
{
    uint16_t hr = 0;
    uint16_t red = 0;
    uint16_t ir = 0;
    uint16_t osc_first = 0;
    uint16_t osc_last = 0;
    int rc;

    memset(&s_status, 0, sizeof(s_status));

    hrm_sensor_enable();
    posix_port_delay_ms(5);

    rc = hrm_bus_ready();
    if (rc != 0)
    {
        APP_LOGE("[vc30] i2c0 not ready (%d), sensor not probed", rc);
        s_status.last_error = rc;
        return rc;
    }

    /* 1. Link: soft reset, then read the ID registers. */
    rc = hrm_vc30x_reset(HRM_VC30X_MODE_HR);
    if (rc != 0)
    {
        APP_LOGE("[vc30] reset/config failed (%d) - chip not answering at 0x%02x",
                 rc, HRM_I2C_ADDR);
        s_status.last_error = rc;
        return rc;
    }

    rc = hrm_rd(HRM_REG_CHIP_ID, &s_status.chip_id, 1);
    if (rc != 0)
    {
        APP_LOGE("[vc30] chip id read failed (%d)", rc);
        s_status.last_error = rc;
        return rc;
    }
    (void)hrm_rd(HRM_REG_VERSION, &s_status.version, 1);
    s_status.link_ok = true;
    APP_LOGI("[vc30] link ok: chip_id=0x%02x version=0x%02x addr=0x%02x bus=i2c0",
             s_status.chip_id, s_status.version, HRM_I2C_ADDR);

    /* 2. HR mode: the sampling engine must advance, and a frame must arrive. */
    rc = hrm_vc30x_start(HRM_VC30X_MODE_HR);
    if (rc == 0)
    {
        rc = hrm_check_engine_alive(&osc_first, &osc_last);
        if (rc != 0)
        {
            APP_LOGE("[vc30] hr engine stalled: osc counter stuck at %u", osc_first);
        }
    }
    if (rc == 0)
    {
        rc = hrm_vc30x_read_hr(&hr);
    }
    (void)hrm_vc30x_stop();
    if (rc == 0)
    {
        s_status.hr_ok = true;
        APP_LOGI("[vc30] hr ok: osc %u->%u, ppg=%u ps=%u env=%u bio_out=%u",
                 osc_first, osc_last, hr, s_status.ps, s_status.env,
                 s_status.bio_out);
    }
    else
    {
        APP_LOGE("[vc30] hr acquisition failed (%d), event=0x%02x status=0x%02x",
                 rc, s_status.event, s_status.status);
    }

    /* 3. SpO2 mode: same check with both LED channels enabled. */
    if (rc == 0)
    {
        rc = hrm_vc30x_reset(HRM_VC30X_MODE_SPO2);
    }
    if (rc == 0)
    {
        rc = hrm_vc30x_start(HRM_VC30X_MODE_SPO2);
    }
    if (rc == 0)
    {
        rc = hrm_check_engine_alive(&osc_first, &osc_last);
        if (rc != 0)
        {
            APP_LOGE("[vc30] spo2 engine stalled: osc counter stuck at %u", osc_first);
        }
    }
    if (rc == 0)
    {
        rc = hrm_vc30x_read_spo2(&red, &ir);
    }
    (void)hrm_vc30x_stop();
    if (rc == 0)
    {
        s_status.spo2_ok = true;
        APP_LOGI("[vc30] spo2 ok: osc %u->%u, red=%u ir=%u ps=%u env=%u",
                 osc_first, osc_last, red, ir, s_status.ps, s_status.env);
    }
    else
    {
        APP_LOGE("[vc30] spo2 acquisition failed (%d), event=0x%02x status=0x%02x",
                 rc, s_status.event, s_status.status);
    }

    /* Leave the sensor configured for HR but not sampling, LEDs dark. */
    (void)hrm_configure(HRM_VC30X_MODE_HR);
    (void)hrm_vc30x_hr_close();

    s_status.last_error = rc;
    if (rc == 0)
    {
        APP_LOGI("[vc30] driver ok: link + hr + spo2 sampling verified");
    }
    else
    {
        APP_LOGE("[vc30] driver FAILED (rc=%d): link=%d hr=%d spo2=%d",
                 rc, (int)s_status.link_ok, (int)s_status.hr_ok, (int)s_status.spo2_ok);
    }
    return rc;
}

static int hrm_posix_ioctl(void *drv, void *file, unsigned long cmd, void *arg)
{
    (void)drv;
    (void)file;

    switch (cmd)
    {
    case POSIX_HRM_IOCTL_SELF_TEST:
        return hrm_vc30x_boot_test() == 0 ? POSIX_OK : POSIX_ERR_IO;

    case POSIX_HRM_IOCTL_OPEN:
        if (arg == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        {
            posix_hrm_mode_t mode = *(const posix_hrm_mode_t *)arg;
            int rc;
            if (mode == POSIX_HRM_MODE_SPO2)
            {
                rc = hrm_vc30x_reset(HRM_VC30X_MODE_SPO2);
                if (rc == 0) rc = hrm_vc30x_start(HRM_VC30X_MODE_SPO2);
            }
            else if (mode == POSIX_HRM_MODE_HRV)
            {
                rc = hrm_vc30x_hrv_open();
            }
            else
            {
                rc = hrm_vc30x_hr_open();
            }
            return rc == 0 ? POSIX_OK : POSIX_ERR_IO;
        }

    case POSIX_HRM_IOCTL_STOP:
        return hrm_vc30x_stop() == 0 ? POSIX_OK : POSIX_ERR_IO;

    case POSIX_HRM_IOCTL_READ_FRAME:
        if (arg == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        {
            posix_hrm_frame_t *out = arg;
            hrm_vc30x_frame_t frame = {0};
            int rc = s_mode == HRM_VC30X_MODE_HRV ? hrm_vc30x_hrv_poll(&frame)
                                                   : hrm_vc30x_poll(&frame);
            if (rc != 0)
            {
                return POSIX_ERR_IO;
            }
            memset(out, 0, sizeof(*out));
            out->count = frame.count > POSIX_HRM_MAX_SAMPLES ? POSIX_HRM_MAX_SAMPLES
                                                              : frame.count;
            memcpy(out->samples, frame.ppg, out->count * sizeof(out->samples[0]));
            out->env = frame.env;
            out->ps = frame.ps;
            out->led_current = frame.led_current;
            out->worn = frame.worn;
            out->wear_changed = frame.wear_changed;
            return POSIX_OK;
        }

    case POSIX_HRM_IOCTL_READ_SPO2:
        if (arg == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        {
            posix_hrm_spo2_t *out = arg;
            return hrm_vc30x_read_spo2(&out->red, &out->ir) == 0 ? POSIX_OK : POSIX_ERR_IO;
        }

    case POSIX_HRM_IOCTL_GET_STATUS:
        if (arg == NULL)
        {
            return POSIX_ERR_INVAL;
        }
        {
            const hrm_vc30x_status_t *status = hrm_vc30x_status();
            posix_hrm_status_t *out = arg;
            memset(out, 0, sizeof(*out));
            out->chip_id = status->chip_id;
            out->version = status->version;
            out->status = status->status;
            out->event = status->event;
            out->ps = status->ps;
            out->env = status->env;
            out->link_ok = status->link_ok;
            out->hr_ok = status->hr_ok;
            out->spo2_ok = status->spo2_ok;
            out->hr_raw = status->hr_raw;
            out->spo2_red = status->spo2_red;
            out->spo2_ir = status->spo2_ir;
            out->last_error = status->last_error;
            return POSIX_OK;
        }

    default:
        return POSIX_ERR_NOSUPP;
    }
}

static void *hrm_posix_open(void *drv, const char *path)
{
    (void)drv;
    (void)path;
    hrm_sensor_enable();
    return &s_status;
}

static int hrm_posix_close(void *drv, void *file)
{
    (void)drv;
    (void)file;
    (void)hrm_vc30x_stop();
    return POSIX_OK;
}

static posix_ssize_t hrm_posix_read(void *drv, void *file, void *buf, size_t count)
{
    (void)drv;
    (void)file;
    (void)buf;
    (void)count;
    return POSIX_ERR_NOSUPP;
}

static posix_ssize_t hrm_posix_write(void *drv, void *file, const void *buf, size_t count)
{
    (void)drv;
    (void)file;
    (void)buf;
    (void)count;
    return POSIX_ERR_NOSUPP;
}

static const posix_driver_ops_t s_hrm_ops =
{
    .open = hrm_posix_open,
    .close = hrm_posix_close,
    .read = hrm_posix_read,
    .write = hrm_posix_write,
    .ioctl = hrm_posix_ioctl,
};

static int hrm_posix_init(void)
{
    return posix_device_register("/dev/hrm0", &s_hrm_ops, NULL);
}
POSIX_INIT_DEVICE_EXPORT(hrm_posix_init);
