#include "hmi_l2_handlers.h"
#include "hmi_l2_cmd_ota.h"
#include "hmi_l2_cmd_settings.h"
#include "hmi_l2_cmd_bind.h"
#include "hmi_l2_cmd_notify.h"
#include "hmi_l2_cmd_sport.h"
#include "hmi_l2_cmd_factory.h"
#include "hmi_l2_cmd_control.h"
#include "hmi_l2_cmd_log.h"
#include "hmi_l2_cmd_xfer.h"
#include "hmi_l2_cmd_conn_param.h"
#include "hmi_l2_cmd_remote.h"

void hmi_l2_handlers_register(void)
{
    hmi_l2_ota_register();
    hmi_l2_settings_register();
    hmi_l2_bind_register();
    hmi_l2_notify_register();
    hmi_l2_sport_register();
    hmi_l2_factory_register();
    hmi_l2_control_register();
    hmi_l2_log_register();
    hmi_l2_xfer_register();
    hmi_l2_conn_param_register();
    hmi_l2_remote_register();
}
