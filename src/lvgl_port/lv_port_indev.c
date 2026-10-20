/**
 * @file lv_port_indev_templ.c
 *
 */

/*Copy this file as "lv_port_indev.c" and set this value to "1" to enable content*/
#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_indev.h"
#include "drv_touch.h"
/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void touchpad_init(void);
static void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data);
static bool touchpad_is_pressed(void);
static void touchpad_get_xy(lv_coord_t *x, lv_coord_t *y);

/**********************
 *  STATIC VARIABLES
 **********************/
lv_indev_t *indev_touchpad;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_port_indev_init(void)
{
    /**
     * Here you will find example implementation of input devices supported by LittelvGL:
     *  - Touchpad
     *  - Mouse (with cursor support)
     *  - Keypad (supports GUI usage only with key)
     *  - Encoder (supports GUI usage only with: left, right, push)
     *  - Button (external buttons to press points on the screen)
     *
     *  The `..._read()` function are only examples.
     *  You should shape them according to your hardware
     */

    /*------------------
     * Touchpad
     * -----------------*/

    /*Initialize your touchpad if you have*/
    touchpad_init();

    /*Register a touchpad input device*/
    indev_touchpad = lv_indev_create();
    lv_indev_set_type(indev_touchpad, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev_touchpad, touchpad_read);


}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/*------------------
 * Touchpad
 * -----------------*/

static uint16_t touch_x = 0;
static uint16_t touch_y = 0;
static bool touch_pressing = 0;


/*Initialize your touchpad*/
static void touchpad_init(void)
{
    /*Your code comes here*/
}

/*Will be called by the library to read the touchpad*/
static void touchpad_read(lv_indev_t *indev_drv, lv_indev_data_t *data)
{
    static lv_coord_t last_x = 0;
    static lv_coord_t last_y = 0;

    /* rt touch read port, report released on i2c fail, or the last state is kept forever */
    if (drv_touch_read(&touch_x, &touch_y, &touch_pressing) == false)
    {
        touch_pressing = false;
    }

    /*Save the pressed coordinates and the state*/
    if (touchpad_is_pressed())
    {
        touchpad_get_xy(&last_x, &last_y);
        /* cst816d driver maps raw x to (385 - x), keep the point on the screen */
        lv_display_t *disp = lv_indev_get_display(indev_drv);
        int32_t hor_max = lv_display_get_horizontal_resolution(disp) - 1;
        int32_t ver_max = lv_display_get_vertical_resolution(disp) - 1;
        last_x = LV_CLAMP(0, last_x, hor_max);
        last_y = LV_CLAMP(0, last_y, ver_max);
        data->state = LV_INDEV_STATE_PRESSED;
    }
    else
    {
        data->state = LV_INDEV_STATE_RELEASED;
    }

    /*Set the last pressed coordinates*/
    data->point.x = last_x;
    data->point.y = last_y;
}
/*Return true is the touchpad is pressed*/
// static lv_coord_t touch_x;
// static lv_coord_t touch_y;
static bool touchpad_is_pressed(void)
{
    /*Your code comes here*/
    return touch_pressing;

}

/*Get the x and y coordinates if the touchpad is pressed*/
static void touchpad_get_xy(lv_coord_t *x, lv_coord_t *y)
{
    /*Your code comes here*/
    (*x) = touch_x;
    (*y) = touch_y;
}

#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
