/**
 * @file    ebadge_port_antenna.c
 * @brief   P2_6 RF switch: BLE (HIGH) vs Wi-Fi (LOW).  See the header.
 */
#include "ebadge_port_antenna.h"

#include "rtl876x_pinmux.h"

#include "../ebadge_log.h"

/*----------------------------------------------------------------------------*
 *  The pin
 *
 *  Pad_Config in SW mode with the output driver enabled, which is how every other
 *  discrete control in this app drives a pad (see main.c's ADC_2 / VD33_WF_EN).
 *  Deliberately NOT a Zephyr gpio_dt_spec: the pad is not in any DT node, and
 *  adding one would mean an overlay edit in the wifi snippet for a pin that must
 *  work even when that snippet is absent -- a build without CONFIG_WIFI_8711 still
 *  has a BLE radio and still needs the antenna pointed at it.
 *
 *  THE LEVELS ARE THE BOARD'S, NOT A CONVENTION.  Only these two lines know which
 *  way the switch faces; everything else in this file and every caller speaks in
 *  owners.  A board that wires the switch the other way changes them and nothing
 *  else.
 *----------------------------------------------------------------------------*/
#define ANT_PIN            P2_6
#define ANT_LEVEL_BT       PAD_OUT_HIGH
#define ANT_LEVEL_WIFI     PAD_OUT_LOW

static ebadge_ant_owner_t s_owner  = EBADGE_ANT_OWNER_BT;
static uint8_t            s_claims;
static bool               s_inited;

/*----------------------------------------------------------------------------*
 *  THREADING.  Everything here runs on l2_task and there are no locks.
 *
 *  That is a property of the call sites, not luck: both sessions live on l2_task,
 *  and the port_* callbacks that could reach them from the SPI transport thread
 *  (on_tcp_data / on_tcp_close / the joined edge) all marshal through
 *  ebadge_task_post_call() before touching session state.  A claim or release
 *  issued straight from the transport thread would race the count against l2_task
 *  and could leave the switch pointing at a radio nobody is using -- so if a new
 *  call site ever appears off l2_task, it must post_call, not add a mutex.
 *----------------------------------------------------------------------------*/

/** The only place the pad is written.  Unconditional: re-asserting a level we
 *  believe is already set costs a register write and is the cheapest available
 *  defence against something else having touched the pad (gui_port_indev.c's
 *  power-off path does, on its way down). */
static void ant_drive(ebadge_ant_owner_t owner)
{
    Pad_Config(ANT_PIN, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_NONE, PAD_OUT_ENABLE,
               (owner == EBADGE_ANT_OWNER_WIFI) ? ANT_LEVEL_WIFI : ANT_LEVEL_BT);
    s_owner = owner;
}

void ebadge_port_antenna_init(void)
{
    s_claims = 0;
    ant_drive(EBADGE_ANT_OWNER_BT);
    s_inited = true;
    EBADGE_LOG("antenna: P2_6 configured, parked on BT (HIGH)");
}

void ebadge_port_antenna_claim_wifi(const char *why)
{
    if (!s_inited)
    {
        /* Not a silent self-init: reaching here means a session started before
         * ebadge_task_init() wired this up, and the pad state is then whatever the
         * bootloader left.  Configure it so the transfer has a chance, but say so --
         * the ordering bug is the thing worth fixing. */
        EBADGE_WARN("antenna: claim before init -- configuring pad now");
        ebadge_port_antenna_init();
    }

    if (s_claims < 0xFFu)
    {
        s_claims++;
    }

    if (s_owner == EBADGE_ANT_OWNER_WIFI)
    {
        /* Already borrowed.  Only reachable if the two sessions stop being mutually
         * exclusive, which is precisely the case the count exists for. */
        EBADGE_LOG2("antenna: already on WIFI, claims=%u (%s)",
                    (unsigned)s_claims, why ? why : "?");
        return;
    }

    ant_drive(EBADGE_ANT_OWNER_WIFI);
    /* Worth a line at INFO every time.  This is the moment BLE goes deaf, so when
     * a phone reports "the badge stopped responding" this timestamp is the first
     * thing to line the complaint up against. */
    EBADGE_LOG1("antenna: BT -> WIFI (%s); BLE is off air until release",
                why ? why : "?");
}

void ebadge_port_antenna_release_wifi(const char *why)
{
    if (s_claims == 0)
    {
        /* Nothing borrowed.  This is NORMAL and must stay quiet: both sessions
         * release from their single teardown choke point, and those run on the
         * early-reject paths too -- an offer refused for a wrong file_type tears
         * down without ever having claimed.  Making that warn would put a warning
         * on every rejected offer and teach the reader to ignore the line.
         *
         * Owner==WIFI with a zero count is the real imbalance, and that one is worth
         * shouting about: of the two ways to be wrong, "on BT with a stale count" is
         * recoverable while "on Wi-Fi with nobody left to release it" costs the
         * control plane until reboot.  Re-assert either way. */
        if (s_owner == EBADGE_ANT_OWNER_WIFI)
        {
            EBADGE_WARN1("antenna: on WIFI with no claim (%s) -- forcing BT",
                         why ? why : "?");
            ant_drive(EBADGE_ANT_OWNER_BT);
        }
        return;
    }

    s_claims--;
    if (s_claims > 0)
    {
        EBADGE_LOG2("antenna: still borrowed, claims=%u (%s)",
                    (unsigned)s_claims, why ? why : "?");
        return;
    }

    ant_drive(EBADGE_ANT_OWNER_BT);
    EBADGE_LOG1("antenna: WIFI -> BT (%s)", why ? why : "?");
}

void ebadge_port_antenna_force_bt(const char *why)
{
    bool was_wifi = (s_owner == EBADGE_ANT_OWNER_WIFI);

    s_claims = 0;
    ant_drive(EBADGE_ANT_OWNER_BT);

    if (was_wifi)
    {
        /* Only interesting when it actually took something back -- the BLE
         * disconnect hook calls this on every disconnect, and a line per
         * disconnect for a no-op would bury the case that matters. */
        EBADGE_WARN1("antenna: forced WIFI -> BT (%s)", why ? why : "?");
    }
}

ebadge_ant_owner_t ebadge_port_antenna_owner(void) { return s_owner;  }
uint8_t            ebadge_port_antenna_claims(void) { return s_claims; }
