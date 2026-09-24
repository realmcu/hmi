#include "app_power.h"
#include "app_model.h"
#include <charger_api.h>

static bool charger_state_is_charging(T_CHARGER_STATE state)
{
    return state == STATE_CHARGER_START ||
           state == STATE_CHARGER_PRE_CHARGE ||
           state == STATE_CHARGER_FAST_CHARGE ||
           state == STATE_CHARGER_FINISH;
}

static void power_refresh(void)
{
    app_model_update_power(charger_api_get_state_of_charge(),
                           charger_state_is_charging(charger_api_get_charger_state()));
}

static void on_state_of_charge(uint8_t percent)
{
    app_model_update_power(percent,
                           charger_state_is_charging(charger_api_get_charger_state()));
}

static void on_charger_state(T_CHARGER_STATE state)
{
    app_model_update_power(charger_api_get_state_of_charge(),
                           charger_state_is_charging(state));
}

static int power_init(void)
{
    power_refresh();
    charger_api_reg_state_of_charge_callback(on_state_of_charge);
    charger_api_reg_charger_state_callback(on_charger_state);
    return 0;
}

const app_module_t app_power_module =
{
    .name  = "power",
    .init  = power_init,
};
