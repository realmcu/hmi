#include "hmi_l2_cmd_xfer.h"
#include "hmi_l2.h"
#include "hmi_proto.h"
#include "proto_log.h"
#include <stdbool.h>


/* flash db */
#include "string.h"
#include "gui_message.h"
#include "flashdb.h"
#include "trace.h"
extern fdb_bf_t   app_get_bf(void);
extern bool      fdb_bf_exists(fdb_bf_t db, const char *key);


static bool     s_xfer_active   = false;
static uint8_t  s_xfer_type     = 0;
static uint32_t s_xfer_total    = 0;
static uint16_t s_xfer_chunk    = 0;
static uint16_t s_xfer_next_seq = 0;

/* FlashDB BigFile handle for the in-progress receive.  File-scope (not local to
 * on_cmd_xfer) so a link disconnect can abort a half-written file via
 * hmi_l2_xfer_reset(). */
static fdb_err_t     rc   = 0;
static fdb_bf_file_t file = NULL;

static void xfer_send(uint8_t key, const uint8_t *val, uint16_t val_len)
{
    uint8_t buf[2 + 3 + 8];
    uint16_t pos = 0;

    buf[pos++] = HMI_L2_CMD_FILE_XFER;
    buf[pos++] = 0x00u;
    buf[pos++] = key;
    buf[pos++] = (uint8_t)((val_len >> 8) & 0x01u);
    buf[pos++] = (uint8_t)(val_len & 0xFFu);
    for (uint16_t i = 0; i < val_len; i++)
    {
        buf[pos++] = val[i];
    }
    proto_send(buf, pos);
}

static void xfer_reset(void)
{
    s_xfer_active   = false;
    s_xfer_type     = 0;
    s_xfer_total    = 0;
    s_xfer_chunk    = 0;
    s_xfer_next_seq = 0;
}

static void on_cmd_xfer(const hmi_l2_kv_t *kvs, uint8_t n)
{
    static uint32_t id = 0;
    /* flash db file: `rc` and `file` are file-scope (see top) so the disconnect
     * hook can abort a half-written file. */
    static uint32_t     crc = 0;
    static char name[64];
    static uint32_t res_info[2];

    for (uint8_t i = 0; i < n; i++)
    {
        uint8_t        key = kvs[i].key;
        const uint8_t *val = kvs[i].val;
        uint16_t       vl  = kvs[i].val_len;

        switch (key)
        {
        case HMI_L2_XFER_BEGIN_REQ:
            {
                if (vl < 8)
                {
                    PROTO_LOG("L2 XFER BEGIN_REQ too short (%d)", vl);
                    break;
                }
                if (s_xfer_active)
                {
                    uint8_t rsp[3] = { HMI_L2_XFER_BEGIN_BUSY, 0x00u, 0x00u };
                    xfer_send(HMI_L2_XFER_BEGIN_RSP, rsp, sizeof(rsp));
                    break;
                }
                s_xfer_type  = val[0];
                s_xfer_total = ((uint32_t)val[1] << 24) | ((uint32_t)val[2] << 16)
                               | ((uint32_t)val[3] <<  8) | (uint32_t)val[4];
                s_xfer_chunk = ((uint16_t)val[5] << 8) | val[6];
                if (s_xfer_chunk == 0 || s_xfer_chunk > HMI_L2_XFER_CHUNK_MAX)
                {
                    s_xfer_chunk = HMI_L2_XFER_CHUNK_MAX;
                }
                s_xfer_next_seq = 0;
                s_xfer_active   = true;

                uint8_t fname_len = val[7];
                PROTO_LOG("L2 XFER BEGIN type=%d total=%lu chunk=%d fname_len=%d",
                          s_xfer_type, (unsigned long)s_xfer_total,
                          s_xfer_chunk, fname_len);

                do
                {
                    memset((void *)name, 0, sizeof(name));
                    sprintf(name, "bf_%u", id);
                    id++;
                    PROTO_LOG("fdb_bf_exists %s?", name);
                }
                while (fdb_bf_exists(app_get_bf(), name));

                PROTO_LOG("fdb_bf_create");
                rc = fdb_bf_create(app_get_bf(), name, s_xfer_total, &file);
                PROTO_LOG("fdb_bf_create done");
                if (rc != FDB_NO_ERR)
                {
                    APP_PRINT_ERROR2("[bf] create '%s' failed (%d)", name, (int)rc);
                }

                uint8_t rsp[3];
                rsp[0] = (rc == FDB_NO_ERR) ? HMI_L2_XFER_BEGIN_OK : HMI_L2_XFER_BEGIN_NO_SPACE;
                rsp[1] = (uint8_t)(s_xfer_chunk >> 8);
                rsp[2] = (uint8_t)(s_xfer_chunk & 0xFFu);
                xfer_send(HMI_L2_XFER_BEGIN_RSP, rsp, sizeof(rsp));
                break;
            }

        case HMI_L2_XFER_DATA:
            {
                PROTO_LOG("L2 XFER DATA active=%d vl=%d val[0..3]=%02x %02x %02x %02x next_seq=%d",
                          s_xfer_active, vl,
                          vl > 0 ? val[0] : 0xFF, vl > 1 ? val[1] : 0xFF,
                          vl > 2 ? val[2] : 0xFF, vl > 3 ? val[3] : 0xFF,
                          s_xfer_next_seq);
                if (!s_xfer_active || vl < 2)
                {
                    PROTO_LOG("L2 XFER DATA discard: active=%d vl=%d", s_xfer_active, vl);
                    break;
                }
                uint16_t seq = ((uint16_t)val[0] << 8) | val[1];
                if (seq != s_xfer_next_seq)
                {
                    PROTO_LOG("L2 XFER DATA seq=%d expected=%d, aborting", seq, s_xfer_next_seq);
                    uint8_t reason = HMI_L2_XFER_ABORT_ERROR;
                    xfer_send(HMI_L2_XFER_ABORT, &reason, 1);
                    xfer_reset();
                    break;
                }
                PROTO_LOG("L2 XFER DATA seq=%d data_len=%d", seq, vl - 2);
                /* TODO: write (val + 2, vl - 2) to storage */
                s_xfer_next_seq++;

                if (rc == 0)
                {
                    rc = fdb_bf_append(file, val + 2, vl - 2);
                    if (rc != FDB_NO_ERR)
                    {
                        APP_PRINT_ERROR1("[bf] append failed (%d)", (int)rc);
                        fdb_bf_abort(file);
                    }
                }
                break;
            }

        case HMI_L2_XFER_END_REQ:
            {
                if (!s_xfer_active || vl < 4)
                {
                    break;
                }
                uint32_t crc32 = ((uint32_t)val[0] << 24) | ((uint32_t)val[1] << 16)
                                 | ((uint32_t)val[2] <<  8) | (uint32_t)val[3];
                PROTO_LOG("L2 XFER END crc32=0x%08lx total_chunks=%d",
                          (unsigned long)crc32, s_xfer_next_seq);
                /* TODO: verify crc32 against received data */

                if (rc == 0)
                {
                    rc = fdb_bf_commit(file, 0);
                    if (rc != FDB_NO_ERR)
                    {
                        APP_PRINT_ERROR2("[bf] commit '%s' failed (%d)", name, (int)rc);
                    }
                    else
                    {
                        extern void ui_add_resource(uint32_t payload);
                        res_info[0] = 0;
                        res_info[1] = 0;

                        int grc = fdb_bf_get_addr(app_get_bf(), name, &res_info[0], &res_info[1]);
                        PROTO_LOG("[bf]  rc %d grc %d file %s 0x%x %d", rc, grc, name, res_info[0], res_info[1]);
                        ui_add_resource((uint32_t)res_info);
                    }
                }

                // l2 rsp send
                xfer_reset();
                uint8_t rsp[2] = { HMI_L2_XFER_END_OK, HMI_L2_XFER_ERR_NONE };
                xfer_send(HMI_L2_XFER_END_RSP, rsp, sizeof(rsp));
                break;
            }

        case HMI_L2_XFER_ABORT:
            {
                uint8_t reason = (vl > 0) ? val[0] : 0;
                PROTO_LOG("L2 XFER ABORT reason=%d", reason);
                xfer_reset();

                if (rc == 0 && file != NULL)
                {
                    fdb_bf_abort(file);
                    file = NULL;
                }
                break;
            }

        default:
            PROTO_LOG("L2 XFER unknown key=0x%02x", key);
            break;
        }
    }
}

void hmi_l2_xfer_reset(void)
{
    /* Called on link disconnect: abort a half-written file and clear session
     * state so the next transfer isn't rejected with BEGIN_BUSY.  Only abort
     * when a transfer was actually in progress (after a clean END, s_xfer_active
     * is already false and `file` may be a stale committed handle). */
    if (s_xfer_active && file != NULL)
    {
        PROTO_LOG("L2 XFER reset: link lost mid-transfer, aborting file");
        fdb_bf_abort(file);
    }
    file = NULL;
    rc   = 0;
    xfer_reset();
}

void hmi_l2_xfer_register(void)
{
    hmi_l2_register(HMI_L2_CMD_FILE_XFER, on_cmd_xfer);
}
