/**
*********************************************************************************************************
*               Copyright(c) 2023, Realtek Semiconductor Corporation. All rights reserved.
**********************************************************************************************************
* \file     rtl_PPE_DISP.c
* \brief    This file provides all the the PPE_DISP 2.0 firmware functions.
* \details
* \author   astor zhang
* \date     2024-11-12
* \version  v1.1
*********************************************************************************************************
*/

/*============================================================================*
 *                        Header Files
 *============================================================================*/
#include "rtl_ppe.h"
#include "string.h"
//#include "os_sync.h"
#include "math.h"

#ifndef BIT0
#define BIT0    0x0001
#endif
#ifndef BIT1
#define BIT1    0x0002
#endif
#ifndef BIT2
#define BIT2    0x0004
#endif
#ifndef BIT3
#define BIT3    0x0008
#endif
#ifndef BIT4
#define BIT4    0x0010
#endif

#include "ameba_soc.h"

/*============================================================================*
 *                          Private Macros
 *============================================================================*/
#define USE_PPE_DISP_MAT     1
#define ABS(x)               (((x) < 0)    ? -(x) :  (x))
#define EPS                  1.1920929e-7f

/*============================================================================*
 *                          Private Variables
 *============================================================================*/
typedef struct
{
    float p[3];
} ppe_pox_t;

/*============================================================================*
 *                          Private Functions
 *============================================================================*/
static bool inv_matrix2complement(ppe_matrix_t *matrix, uint32_t *comp);

/*============================================================================*
 *                           Public Functions
 *============================================================================*/
void ppe_get_identity(ppe_matrix_t *matrix);
#if USE_PPE_DISP_MAT
void ppe_translate(float x, float y, ppe_matrix_t *matrix);
void ppe_scale(float scale_x, float scale_y, ppe_matrix_t *matrix);
void ppe_rotate(float degrees, ppe_matrix_t *matrix);
void ppe_perspective(float px, float py, ppe_matrix_t *matrix);
int  ppe_get_transform_matrix(ppe_point4_t src, ppe_point4_t dst, ppe_matrix_t *mat);
void ppe_matrix_inverse(ppe_matrix_t *matrix);
void ppe_mat_multiply(ppe_matrix_t *matrix, ppe_matrix_t *mult);
#endif

/*enable PPE_DISP FSM*/
void HONEYGUI_PPE_DISP_Cmd(FunctionalState NewState)
{
    PPE_DISP_REG_GLB_STATUS_TypeDef ppe_reg_glb_status_0x00 = {.d32 = PPE_DISP->REG_GLB_STATUS};

    if (NewState == DISABLE)
    {
        ppe_reg_glb_status_0x00.b.run_state = 0x0;
    }
    else
    {
        ppe_reg_glb_status_0x00.b.run_state = 0x1;
    }

    PPE_DISP->REG_GLB_STATUS = ppe_reg_glb_status_0x00.d32;
}

void PPE_DISP_Init(PPE_DISP_Init_Typedef *PPE_DISP_Init_Struct)
{
    HONEYGUI_PPE_DISP_Cmd(DISABLE);

    PPE_DISP_REG_LD_CFG_TypeDef ppe_reg_ld_cfg_0x08  = {.d32 = PPE_DISP->REG_LD_CFG};
    ppe_reg_ld_cfg_0x08.b.auto_clr          = PPE_DISP_Init_Struct->SetValid_AutoClear;
    PPE_DISP->REG_LD_CFG                         = ppe_reg_ld_cfg_0x08.d32;

    PPE_DISP_REG_LLP_TypeDef ppe_reg_llp_0x10        = {.d32 = PPE_DISP->REG_LLP};
    ppe_reg_llp_0x10.b.llp                  = PPE_DISP_Init_Struct->LLP >> 2;
    PPE_DISP->REG_LLP                            = ppe_reg_llp_0x10.d32;

    PPE_DISP_REG_SECURE_TypeDef  ppe_reg_secure_0x18 = {.d32 = PPE_DISP->REG_SECURE};
    ppe_reg_secure_0x18.b.secure            = PPE_DISP_Init_Struct->Secure_En;
    PPE_DISP->REG_SECURE                         = ppe_reg_secure_0x18.d32;
}

void PPE_DISP_ResultLayer_Init(PPE_DISP_ResultLayer_Init_Typedef *PPE_DISP_ResultLyaer_Init_Struct)
{
    PPE_DISP->PPE_DISP_XOR_CTRL &= 0xFFFFFFFE;

    PPE_DISP_REG_LYR0_ADDR_TypeDef ppe_reg_lyr0_addr_0x60 = {.d32 = PPE_DISP_ResultLayer->REG_LYR0_ADDR};
    ppe_reg_lyr0_addr_0x60.b.addr                = PPE_DISP_ResultLyaer_Init_Struct->Layer_Address;
    PPE_DISP_ResultLayer->REG_LYR0_ADDR               = ppe_reg_lyr0_addr_0x60.d32;

    PPE_DISP_REG_CANVAS_SIZE_TypeDef ppe_reg_canvas_size_0x68 = {.d32 = PPE_DISP_ResultLayer->REG_CANVAS_SIZE};
    ppe_reg_canvas_size_0x68.b.canvas_width          = PPE_DISP_ResultLyaer_Init_Struct->Canvas_Width;
    ppe_reg_canvas_size_0x68.b.canvas_height         = PPE_DISP_ResultLyaer_Init_Struct->Canvas_Height;
    PPE_DISP_ResultLayer->REG_CANVAS_SIZE                 = ppe_reg_canvas_size_0x68.d32;

    PPE_DISP_REG_LYR0_PIC_CFG_TypeDef  ppe_reg_lyr0_pic_cfg_0x6C = {.d32 = PPE_DISP_ResultLayer->REG_LYR0_PIC_CFG};
    ppe_reg_lyr0_pic_cfg_0x6C.b.format                  = PPE_DISP_ResultLyaer_Init_Struct->Color_Format;
    ppe_reg_lyr0_pic_cfg_0x6C.b.line_length             = PPE_DISP_ResultLyaer_Init_Struct->Line_Length;
    ppe_reg_lyr0_pic_cfg_0x6C.b.background_blend        = ENABLE;
    PPE_DISP_ResultLayer->REG_LYR0_PIC_CFG                   = ppe_reg_lyr0_pic_cfg_0x6C.d32;

    PPE_DISP_REG_BACKGROUND_TypeDef ppe_reg_background_0x70 = {.d32 = PPE_DISP_ResultLayer->REG_BACKGROUND};
    ppe_reg_background_0x70.b.background           = PPE_DISP_ResultLyaer_Init_Struct->BackGround;
    PPE_DISP_ResultLayer->REG_BACKGROUND                = ppe_reg_background_0x70.d32;

    PPE_DISP_REG_LYR0_BUS_CFG_TypeDef ppe_reg_lyr0_bus_cfg_0x74 = {.d32 = PPE_DISP_ResultLayer->REG_LYR0_BUS_CFG};
    ppe_reg_lyr0_bus_cfg_0x74.b.incr                   = PPE_DISP_ResultLyaer_Init_Struct->LayerBus_Inc;
    PPE_DISP_ResultLayer->REG_LYR0_BUS_CFG                  = ppe_reg_lyr0_bus_cfg_0x74.d32;

    PPE_DISP_REG_LYR0_HS_CFG_TypeDef ppe_reg_lyr0_hs_cfg_0x78 = {.d32 = PPE_DISP_ResultLayer->REG_LYR0_HS_CFG};
    ppe_reg_lyr0_hs_cfg_0x78.b.hs_en                 =
        PPE_DISP_ResultLyaer_Init_Struct->Layer_HW_Handshake_En;
    ppe_reg_lyr0_hs_cfg_0x78.b.hw_index              =
        PPE_DISP_ResultLyaer_Init_Struct->Layer_HW_Handshake_Index;
    ppe_reg_lyr0_hs_cfg_0x78.b.polar                 =
        PPE_DISP_ResultLyaer_Init_Struct->Layer_HW_Handshake_Polarity;
    ppe_reg_lyr0_hs_cfg_0x78.b.msize_log             =
        PPE_DISP_ResultLyaer_Init_Struct->Layer_HW_Handshake_MsizeLog;
    PPE_DISP_ResultLayer->REG_LYR0_HS_CFG                 = ppe_reg_lyr0_hs_cfg_0x78.d32;

    PPE_DISP_REG_LD_CFG_TypeDef ppe_reg_ld_cfg_0x08  = {.d32 = PPE_DISP->REG_LD_CFG};
    ppe_reg_ld_cfg_0x08.b.reload_en        &= ~BIT0;
    ppe_reg_ld_cfg_0x08.b.reload_en        |= PPE_DISP_ResultLyaer_Init_Struct->MultiFrame_Reload_En;
    PPE_DISP->REG_LD_CFG                         =    ppe_reg_ld_cfg_0x08.d32;

    PPE_DISP_REG_LL_CFG_TypeDef ppe_reg_ll_cfg_0x0c = {.d32 = PPE_DISP->REG_LL_CFG};
    ppe_reg_ll_cfg_0x0c.b.ll_en           &= ~BIT0;
    ppe_reg_ll_cfg_0x0c.b.ll_en           |= PPE_DISP_ResultLyaer_Init_Struct->MultiFrame_LLP_En;
    PPE_DISP->REG_LL_CFG                        = ppe_reg_ll_cfg_0x0c.d32;

    PPE_DISP_REG_BLOCK_SIZE_TypeDef ppe_reg_blk_0x7c = {.d32 = PPE_DISP_ResultLayer->REG_BLOCK_SIZE};
    ppe_reg_blk_0x7c.b.width = PPE_DISP_ResultLyaer_Init_Struct->block_width;
    ppe_reg_blk_0x7c.b.height = PPE_DISP_ResultLyaer_Init_Struct->block_height;
    PPE_DISP_ResultLayer->REG_BLOCK_SIZE = ppe_reg_blk_0x7c.d32;
}
#include "gui_api_os.h"
void PPE_DISP_InputLayer_Init(PPE_DISP_INPUT_LAYER_INDEX intput_layer_index, \
                         PPE_DISP_InputLayer_Init_Typedef *PPE_DISP_InputLayer_Init_Struct)
{
    PPE_DISP_Input_Layer_Typedef *input_layer;
    switch (intput_layer_index)
    {
    case PPE_DISP_INPUT_1:
        {
            input_layer = PPE_DISP_InputLayer1;
            break;
        }
    case PPE_DISP_INPUT_2:
        {
            input_layer = PPE_DISP_InputLayer2;
            break;
        }
    case PPE_DISP_INPUT_3:
        {
            input_layer = PPE_DISP_InputLayer3;
            break;
        }
    case PPE_DISP_INPUT_4:
        {
            input_layer = PPE_DISP_InputLayer4;
            break;
        }
    }

    PPE_DISP_REG_LYRx_ADDR_TypeDef ppe_reg_lyrx_addr_t = {.d32 = input_layer->REG_LYRx_ADDR};
    ppe_reg_lyrx_addr_t.b.addr                = PPE_DISP_InputLayer_Init_Struct->Layer_Address;
    input_layer->REG_LYRx_ADDR                  = ppe_reg_lyrx_addr_t.d32;

    PPE_DISP_REG_LYRx_PIC_SIZE_TypeDef ppe_reg_lyrx_pic_size_t = {.d32 = input_layer->REG_LYRx_PIC_SIZE};
    ppe_reg_lyrx_pic_size_t.b.pic_width               = PPE_DISP_InputLayer_Init_Struct->Pic_Width;
    ppe_reg_lyrx_pic_size_t.b.pic_height              = PPE_DISP_InputLayer_Init_Struct->Pic_Height;
    input_layer->REG_LYRx_PIC_SIZE                      = ppe_reg_lyrx_pic_size_t.d32;

    PPE_DISP_REG_LYRx_PIC_CFG_TypeDef ppe_reg_lyrx_pic_cfg_t     = {.d32 = input_layer->REG_LYRx_PIC_CFG};
    ppe_reg_lyrx_pic_cfg_t.b.line_length                = PPE_DISP_InputLayer_Init_Struct->Line_Length;
    ppe_reg_lyrx_pic_cfg_t.b.color_key_mode             =
        PPE_DISP_InputLayer_Init_Struct->Color_Key_Mode;
    ppe_reg_lyrx_pic_cfg_t.b.pic_src                    = PPE_DISP_InputLayer_Init_Struct->Pixel_Source;
    ppe_reg_lyrx_pic_cfg_t.b.format                     =
        PPE_DISP_InputLayer_Init_Struct->Pixel_Color_Format;
    ppe_reg_lyrx_pic_cfg_t.b.input_lyr_read_matrix_size =
        PPE_DISP_InputLayer_Init_Struct->Read_Matrix_Size;
    input_layer->REG_LYRx_PIC_CFG                         = ppe_reg_lyrx_pic_cfg_t.d32;

    PPE_DISP_REG_LYRx_FIXED_COLOR_TypeDef ppe_reg_lyrx_fixed_color_t = {.d32 = input_layer->REG_LYRx_FIXED_COLOR};
    ppe_reg_lyrx_fixed_color_t.b.const_pixel                =
        PPE_DISP_InputLayer_Init_Struct->Const_Pixel;
    input_layer->REG_LYRx_FIXED_COLOR                         = ppe_reg_lyrx_fixed_color_t.d32;

    PPE_DISP_REG_LYRx_BUS_CFG_TypeDef ppe_reg_lyrx_bus_cfg_t = {.d32 = input_layer->REG_LYRx_BUS_CFG};
    ppe_reg_lyrx_bus_cfg_t.b.incr                   = PPE_DISP_InputLayer_Init_Struct->LayerBus_Inc;
    if (input_layer->REG_LYRx_ADDR <= 0x10000000 || input_layer->REG_LYRx_ADDR >= 0x60000000)
    {
        ppe_reg_lyrx_bus_cfg_t.b.arsize = 0x2;
        ppe_reg_lyrx_bus_cfg_t.b.arcache = 0x6;
        ppe_reg_lyrx_bus_cfg_t.b.max_arlen_log = 0x0;
    }
    else
    {
        ppe_reg_lyrx_bus_cfg_t.b.arsize = 0x2;
        ppe_reg_lyrx_bus_cfg_t.b.arcache = 0x0;
        ppe_reg_lyrx_bus_cfg_t.b.max_arlen_log = 0x7;
    }
    input_layer->REG_LYRx_BUS_CFG                     = ppe_reg_lyrx_bus_cfg_t.d32;

    PPE_DISP_REG_LYRx_HS_CFG_TypeDef ppe_reg_lyrx_hs_cfg_t = {.d32 = input_layer->REG_LYRx_HS_CFG};
    ppe_reg_lyrx_hs_cfg_t.b.hs_en                 =
        PPE_DISP_InputLayer_Init_Struct->Layer_HW_Handshake_En;
    ppe_reg_lyrx_hs_cfg_t.b.hw_index              =
        PPE_DISP_InputLayer_Init_Struct->Layer_HW_Handshake_Index;
    ppe_reg_lyrx_hs_cfg_t.b.msize_log             =
        PPE_DISP_InputLayer_Init_Struct->Layer_HW_Handshake_MsizeLog;
    ppe_reg_lyrx_hs_cfg_t.b.polar                 =
        PPE_DISP_InputLayer_Init_Struct->Layer_HW_Handshake_Polarity;
    input_layer->REG_LYRx_HS_CFG                    = ppe_reg_lyrx_hs_cfg_t.d32;

    PPE_DISP_REG_LYRx_WIN_MIN_TypeDef ppe_reg_lyrx_win_min_t = {.d32 = input_layer->REG_LYRx_WIN_MIN};
    ppe_reg_lyrx_win_min_t.b.win_x_min              = PPE_DISP_InputLayer_Init_Struct->Layer_Window_Xmin;
    ppe_reg_lyrx_win_min_t.b.win_y_min              = PPE_DISP_InputLayer_Init_Struct->Layer_Window_Ymin;
    input_layer->REG_LYRx_WIN_MIN                     = ppe_reg_lyrx_win_min_t.d32;

    PPE_DISP_REG_LYRx_WIN_MAX_TypeDef ppe_reg_lyrx_win_max_t = {.d32 = input_layer->REG_LYRx_WIN_MAX};
    ppe_reg_lyrx_win_max_t.b.win_x_max              = PPE_DISP_InputLayer_Init_Struct->Layer_Window_Xmax;
    ppe_reg_lyrx_win_max_t.b.win_y_max              = PPE_DISP_InputLayer_Init_Struct->Layer_Window_Ymax;
    input_layer->REG_LYRx_WIN_MAX                     = ppe_reg_lyrx_win_max_t.d32;

    PPE_DISP_REG_LYRx_KEY_MIN_TypeDef ppe_reg_lyrx_key_min_t = {.d32 = input_layer->REG_LYRx_KEY_MIN};
    ppe_reg_lyrx_key_min_t.b.color_key_min          = PPE_DISP_InputLayer_Init_Struct->Color_Key_Min;
    input_layer->REG_LYRx_KEY_MIN                     = ppe_reg_lyrx_key_min_t.d32;

    PPE_DISP_REG_LYRx_KEY_MAX_TypeDef ppe_reg_lyrx_key_max_t = {.d32 = input_layer->REG_LYRx_KEY_MAX};
    ppe_reg_lyrx_key_max_t.b.color_key_max          = PPE_DISP_InputLayer_Init_Struct->Color_Key_Max;
    input_layer->REG_LYRx_KEY_MAX                     = ppe_reg_lyrx_key_max_t.d32;

    input_layer->REG_LYRx_TRANS_MATRIX_E11            =
        PPE_DISP_InputLayer_Init_Struct->Transfer_Matrix_E11;
    input_layer->REG_LYRx_TRANS_MATRIX_E12            =
        PPE_DISP_InputLayer_Init_Struct->Transfer_Matrix_E12;
    input_layer->REG_LYRx_TRANS_MATRIX_E13            =
        PPE_DISP_InputLayer_Init_Struct->Transfer_Matrix_E13;
    input_layer->REG_LYRx_TRANS_MATRIX_E21            =
        PPE_DISP_InputLayer_Init_Struct->Transfer_Matrix_E21;
    input_layer->REG_LYRx_TRANS_MATRIX_E22            =
        PPE_DISP_InputLayer_Init_Struct->Transfer_Matrix_E22;
    input_layer->REG_LYRx_TRANS_MATRIX_E23            =
        PPE_DISP_InputLayer_Init_Struct->Transfer_Matrix_E23;

    PPE_DISP_REG_LD_CFG_TypeDef ppe_reg_ld_cfg_0x08 = {.d32 = PPE_DISP->REG_LD_CFG};
    ppe_reg_ld_cfg_0x08.b.reload_en       &= ~BIT(intput_layer_index);
    ppe_reg_ld_cfg_0x08.b.reload_en       |= (PPE_DISP_InputLayer_Init_Struct->MultiFrame_Reload_En <<
                                              intput_layer_index);
    PPE_DISP->REG_LD_CFG                        = ppe_reg_ld_cfg_0x08.d32;

    PPE_DISP_REG_LL_CFG_TypeDef ppe_reg_ll_cfg_0x0c = {.d32 = PPE_DISP->REG_LL_CFG};
    ppe_reg_ll_cfg_0x0c.b.ll_en           &= ~BIT(intput_layer_index);
    ppe_reg_ll_cfg_0x0c.b.ll_en           |= (PPE_DISP_InputLayer_Init_Struct->MultiFrame_LLP_En <<
                                              intput_layer_index);
    PPE_DISP->REG_LL_CFG                        = ppe_reg_ll_cfg_0x0c.d32;
}




void PPE_DISP_StructInit(PPE_DISP_Init_Typedef *PPE_DISP_Init_Struct)
{
    /*read register*/
    PPE_DISP_REG_LD_CFG_TypeDef             ppe_reg_ld_cfg_0x08             = {.d32 = PPE_DISP->REG_LD_CFG};
    PPE_DISP_REG_LLP_TypeDef                 ppe_reg_llp_0x10                     = {.d32 = PPE_DISP->REG_LLP};
    PPE_DISP_REG_SECURE_TypeDef             ppe_reg_secure_0x18             = {.d32 = PPE_DISP->REG_SECURE};

    /*initialization*/
    PPE_DISP_Init_Struct->SetValid_AutoClear = (FunctionalState)ppe_reg_ld_cfg_0x08.b.auto_clr;

    PPE_DISP_Init_Struct->LLP                                 = (ppe_reg_llp_0x10.b.llp) << 2;

    PPE_DISP_Init_Struct->Secure_En                    = (PPE_DISP_SECURE)ppe_reg_secure_0x18.b.secure;

}
void HONEYGUI_PPE_DISP_ResultLayer_StructInit(PPE_DISP_ResultLayer_Init_Typedef *PPE_DISP_ResultLyaer_Init_Struct)
{
    PPE_DISP_REG_LYR0_ADDR_TypeDef       ppe_reg_lyr0_addr_0x60            = {.d32 = PPE_DISP_ResultLayer->REG_LYR0_ADDR};
    PPE_DISP_REG_CANVAS_SIZE_TypeDef     ppe_reg_canvas_size_0x68          = {.d32 = PPE_DISP_ResultLayer->REG_CANVAS_SIZE};
    PPE_DISP_REG_LYR0_PIC_CFG_TypeDef    ppe_reg_lyr0_pic_cfg_0x6C         = {.d32 = PPE_DISP_ResultLayer->REG_LYR0_PIC_CFG};
    PPE_DISP_REG_BACKGROUND_TypeDef      ppe_reg_background_0x70           = {.d32 = PPE_DISP_ResultLayer->REG_BACKGROUND};
    PPE_DISP_REG_LYR0_BUS_CFG_TypeDef    ppe_reg_lyr0_bus_cfg_0x74         = {.d32 = PPE_DISP_ResultLayer->REG_LYR0_BUS_CFG};
    PPE_DISP_REG_LYR0_HS_CFG_TypeDef     ppe_reg_lyr0_hs_cfg_0x78          = {.d32 = PPE_DISP_ResultLayer->REG_LYR0_HS_CFG};

    PPE_DISP_REG_LD_CFG_TypeDef          ppe_reg_ld_cfg_0x08               = {.d32 = PPE_DISP->REG_LD_CFG};
    PPE_DISP_REG_LL_CFG_TypeDef          ppe_reg_ll_cfg_0x0c               = {.d32 = PPE_DISP->REG_LL_CFG};

    PPE_DISP_ResultLyaer_Init_Struct->Layer_Address                    = ppe_reg_lyr0_addr_0x60.b.addr;
    PPE_DISP_ResultLyaer_Init_Struct->Canvas_Width                     =
        ppe_reg_canvas_size_0x68.b.canvas_width;
    PPE_DISP_ResultLyaer_Init_Struct->Canvas_Height                    =
        ppe_reg_canvas_size_0x68.b.canvas_height;
    PPE_DISP_ResultLyaer_Init_Struct->Color_Format                     =
        (PPE_DISP_PIXEL_FORMAT)ppe_reg_lyr0_pic_cfg_0x6C.b.format;
    PPE_DISP_ResultLyaer_Init_Struct->Line_Length                      =
        ppe_reg_lyr0_pic_cfg_0x6C.b.line_length;
    PPE_DISP_ResultLyaer_Init_Struct->BackGround                       =
        ppe_reg_background_0x70.b.background;
    PPE_DISP_ResultLyaer_Init_Struct->LayerBus_Inc                     =
        (PPE_DISP_AWBURST)ppe_reg_lyr0_bus_cfg_0x74.b.incr;
    PPE_DISP_ResultLyaer_Init_Struct->MultiFrame_Reload_En             = (FunctionalState)
                                                                    ppe_reg_ld_cfg_0x08.b.reload_en;
    PPE_DISP_ResultLyaer_Init_Struct->MultiFrame_LLP_En                = (FunctionalState)
                                                                    ppe_reg_ll_cfg_0x0c.b.ll_en;
    PPE_DISP_ResultLyaer_Init_Struct->Layer_HW_Handshake_En            =
        (PPE_DISP_HW_HS)ppe_reg_lyr0_hs_cfg_0x78.b.hs_en;
    PPE_DISP_ResultLyaer_Init_Struct->Layer_HW_Handshake_Index         =
        (PPE_DISP_HW_HANDSHAKE_INDEX)ppe_reg_lyr0_hs_cfg_0x78.b.hw_index;
    PPE_DISP_ResultLyaer_Init_Struct->Layer_HW_Handshake_Polarity      =
        (PPE_DISP_HW_HS_POL)ppe_reg_lyr0_hs_cfg_0x78.b.polar;
    PPE_DISP_ResultLyaer_Init_Struct->Layer_HW_Handshake_MsizeLog      =
        (PPE_DISP_MSIZE_LOG)ppe_reg_lyr0_hs_cfg_0x78.b.msize_log;
}

void HONEYGUI_PPE_DISP_InputLayer_StructInit(PPE_DISP_INPUT_LAYER_INDEX intput_layer_index, \
                               PPE_DISP_InputLayer_Init_Typedef *PPE_DISP_InputLayer_Init_Struct)
{
    PPE_DISP_Input_Layer_Typedef *input_layer;
    switch (intput_layer_index)
    {
    case PPE_DISP_INPUT_1:
        {
            input_layer = PPE_DISP_InputLayer1;
            break;
        }
    case PPE_DISP_INPUT_2:
        {
            input_layer = PPE_DISP_InputLayer2;
            break;
        }
    case PPE_DISP_INPUT_3:
        {
            input_layer = PPE_DISP_InputLayer3;
            break;
        }
    case PPE_DISP_INPUT_4:
        {
            input_layer = PPE_DISP_InputLayer4;
            break;
        }
    }

    PPE_DISP_REG_LYRx_ADDR_TypeDef                ppe_reg_lyrx_addr_t                =    {.d32 = input_layer->REG_LYRx_ADDR};
    PPE_DISP_REG_LYRx_PIC_SIZE_TypeDef            ppe_reg_lyrx_pic_size_t            =    {.d32 = input_layer->REG_LYRx_PIC_SIZE};
    PPE_DISP_REG_LYRx_PIC_CFG_TypeDef             ppe_reg_lyrx_pic_cfg_t             =    {.d32 = input_layer->REG_LYRx_PIC_CFG};
    PPE_DISP_REG_LYRx_FIXED_COLOR_TypeDef         ppe_reg_lyrx_fixed_color_t         =    {.d32 = input_layer->REG_LYRx_FIXED_COLOR};
    PPE_DISP_REG_LYRx_BUS_CFG_TypeDef             ppe_reg_lyrx_bus_cfg_t             =    {.d32 = input_layer->REG_LYRx_BUS_CFG};
    PPE_DISP_REG_LYRx_HS_CFG_TypeDef              ppe_reg_lyrx_hs_cfg_t              =    {.d32 = input_layer->REG_LYRx_HS_CFG};
    PPE_DISP_REG_LYRx_WIN_MIN_TypeDef             ppe_reg_lyrx_win_min_t             =    {.d32 = input_layer->REG_LYRx_WIN_MIN};
    PPE_DISP_REG_LYRx_WIN_MAX_TypeDef             ppe_reg_lyrx_win_max_t             =    {.d32 = input_layer->REG_LYRx_WIN_MAX};
    PPE_DISP_REG_LYRx_KEY_MIN_TypeDef             ppe_reg_lyrx_key_min_t             =    {.d32 = input_layer->REG_LYRx_KEY_MIN};
    PPE_DISP_REG_LYRx_KEY_MAX_TypeDef             ppe_reg_lyrx_key_max_t             =    {.d32 = input_layer->REG_LYRx_KEY_MAX};
    PPE_DISP_REG_LYRx_TRANS_MATRIX_E11_TypeDef    ppe_reg_lyrx_trans_matrix_e11_t    =    {.d32 = input_layer->REG_LYRx_TRANS_MATRIX_E11};
    PPE_DISP_REG_LYRx_TRANS_MATRIX_E12_TypeDef    ppe_reg_lyrx_trans_matrix_e12_t    =    {.d32 = input_layer->REG_LYRx_TRANS_MATRIX_E12};
    PPE_DISP_REG_LYRx_TRANS_MATRIX_E13_TypeDef    ppe_reg_lyrx_trans_matrix_e13_t    =    {.d32 = input_layer->REG_LYRx_TRANS_MATRIX_E13};
    PPE_DISP_REG_LYRx_TRANS_MATRIX_E21_TypeDef    ppe_reg_lyrx_trans_matrix_e21_t    =    {.d32 = input_layer->REG_LYRx_TRANS_MATRIX_E21};
    PPE_DISP_REG_LYRx_TRANS_MATRIX_E22_TypeDef    ppe_reg_lyrx_trans_matrix_e22_t    =    {.d32 = input_layer->REG_LYRx_TRANS_MATRIX_E22};
    PPE_DISP_REG_LYRx_TRANS_MATRIX_E23_TypeDef    ppe_reg_lyrx_trans_matrix_e23_t    =    {.d32 = input_layer->REG_LYRx_TRANS_MATRIX_E23};

    PPE_DISP_REG_LD_CFG_TypeDef ppe_reg_ld_cfg_0x08 = {.d32 = PPE_DISP->REG_LD_CFG};
    PPE_DISP_REG_LL_CFG_TypeDef ppe_reg_ll_cfg_0x0c = {.d32 = PPE_DISP->REG_LL_CFG};

    /*initialization*/
    PPE_DISP_InputLayer_Init_Struct->Layer_Address         =    ppe_reg_lyrx_addr_t.b.addr;

    PPE_DISP_InputLayer_Init_Struct->Pic_Width             =    ppe_reg_lyrx_pic_size_t.b.pic_width;
    PPE_DISP_InputLayer_Init_Struct->Pic_Height            =    ppe_reg_lyrx_pic_size_t.b.pic_height;

    PPE_DISP_InputLayer_Init_Struct->Line_Length           =    ppe_reg_lyrx_pic_cfg_t.b.line_length;
    PPE_DISP_InputLayer_Init_Struct->Color_Key_Mode        = (PPE_DISP_COLOR_KEY_MODE)
                                                        ppe_reg_lyrx_pic_cfg_t.b.color_key_mode;
    PPE_DISP_InputLayer_Init_Struct->Pixel_Source          = (PPE_DISP_PIXEL_SOURCE)
                                                        ppe_reg_lyrx_pic_cfg_t.b.pic_src;
    PPE_DISP_InputLayer_Init_Struct->Pixel_Color_Format    = (PPE_DISP_PIXEL_FORMAT)
                                                        ppe_reg_lyrx_pic_cfg_t.b.format;
    PPE_DISP_InputLayer_Init_Struct->Read_Matrix_Size      =
        (PPE_DISP_READ_MATRIX_SIZE)ppe_reg_lyrx_pic_cfg_t.b.input_lyr_read_matrix_size;

    PPE_DISP_InputLayer_Init_Struct->Const_Pixel           =    ppe_reg_lyrx_fixed_color_t.b.const_pixel;

    PPE_DISP_InputLayer_Init_Struct->LayerBus_Inc          = (PPE_DISP_AWBURST)
                                                        ppe_reg_lyrx_bus_cfg_t.b.incr;

    PPE_DISP_InputLayer_Init_Struct->Layer_HW_Handshake_En = (PPE_DISP_HW_HS)ppe_reg_lyrx_hs_cfg_t.b.hs_en;
    PPE_DISP_InputLayer_Init_Struct->Layer_HW_Handshake_Index      = (PPE_DISP_HW_HANDSHAKE_INDEX)
                                                                ppe_reg_lyrx_hs_cfg_t.b.hw_index;
    PPE_DISP_InputLayer_Init_Struct->Layer_HW_Handshake_MsizeLog   =
        (PPE_DISP_MSIZE_LOG)ppe_reg_lyrx_hs_cfg_t.b.msize_log;
    PPE_DISP_InputLayer_Init_Struct->Max_Axlen                     = (PPE_DISP_MAX_AXLEN)
                                                                ppe_reg_lyrx_hs_cfg_t.b.msize_log;
    PPE_DISP_InputLayer_Init_Struct->Layer_HW_Handshake_Polarity   = (PPE_DISP_HW_HS_POL)
                                                                ppe_reg_lyrx_hs_cfg_t.b.polar;

    PPE_DISP_InputLayer_Init_Struct->Layer_Window_Xmin             =
        ppe_reg_lyrx_win_min_t.b.win_x_min;
    PPE_DISP_InputLayer_Init_Struct->Layer_Window_Ymin             =
        ppe_reg_lyrx_win_min_t.b.win_y_min;

    PPE_DISP_InputLayer_Init_Struct->Layer_Window_Xmax             =
        ppe_reg_lyrx_win_max_t.b.win_x_max;
    PPE_DISP_InputLayer_Init_Struct->Layer_Window_Ymax             =
        ppe_reg_lyrx_win_max_t.b.win_y_max;

    PPE_DISP_InputLayer_Init_Struct->Color_Key_Min =    ppe_reg_lyrx_key_min_t.b.color_key_min;
    PPE_DISP_InputLayer_Init_Struct->Color_Key_Max =    ppe_reg_lyrx_key_max_t.b.color_key_max;




    PPE_DISP_InputLayer_Init_Struct->Transfer_Matrix_E11 = ppe_reg_lyrx_trans_matrix_e11_t.b.matrix_e11;
    PPE_DISP_InputLayer_Init_Struct->Transfer_Matrix_E12 = ppe_reg_lyrx_trans_matrix_e12_t.b.matrix_e12;
    PPE_DISP_InputLayer_Init_Struct->Transfer_Matrix_E13 = ppe_reg_lyrx_trans_matrix_e13_t.b.matrix_e13;
    PPE_DISP_InputLayer_Init_Struct->Transfer_Matrix_E21 = ppe_reg_lyrx_trans_matrix_e21_t.b.matrix_e21;
    PPE_DISP_InputLayer_Init_Struct->Transfer_Matrix_E22 = ppe_reg_lyrx_trans_matrix_e22_t.b.matrix_e22;
    PPE_DISP_InputLayer_Init_Struct->Transfer_Matrix_E23 = ppe_reg_lyrx_trans_matrix_e23_t.b.matrix_e23;


    PPE_DISP_InputLayer_Init_Struct->MultiFrame_Reload_En = (FunctionalState)((
                                                                             ppe_reg_ld_cfg_0x08.b.reload_en & BIT(
                                                                                 intput_layer_index)) >> intput_layer_index);
    PPE_DISP_InputLayer_Init_Struct->MultiFrame_LLP_En    = (FunctionalState)((
                                                                             ppe_reg_ll_cfg_0x0c.b.ll_en & BIT(
                                                                                 intput_layer_index)) >> intput_layer_index);
}

void PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_LAYER_INDEX intput_layer_index, FunctionalState NewState)
{
    PPE_DISP_REG_LYR_ENABLE_TypeDef     ppe_reg_lyr_enable_0x04     = {.d32 = PPE_DISP->REG_LYR_ENABLE};
    uint32_t input_index = (uint32_t)intput_layer_index;

    if (NewState != DISABLE)
    {
        ppe_reg_lyr_enable_0x04.b.input_lyr_en |= BIT(input_index - PPE_DISP_INPUT_1);
    }
    else
    {
        ppe_reg_lyr_enable_0x04.b.input_lyr_en &= ~BIT(input_index - PPE_DISP_INPUT_1);
    }

    PPE_DISP->REG_LYR_ENABLE =  ppe_reg_lyr_enable_0x04.d32;
}

FunctionalState PPE_DISP_Get_Interrupt_Status(PPE_DISP_INTERRUPT PPE_DISP_INT)
{
    if (PPE_DISP->REG_INTR_STATUS & BIT(PPE_DISP_INT))
    {
        return ENABLE;
    }
    else
    {
        return DISABLE;
    }
}

FunctionalState PPE_DISP_Get_Raw_Interrupt_Status(PPE_DISP_INTERRUPT PPE_DISP_INT)
{
    if (PPE_DISP->REG_INTR_RAW & BIT(PPE_DISP_INT))
    {
        return ENABLE;
    }
    else
    {
        return DISABLE;
    }
}
void PPE_DISP_Clear_Interrupt(PPE_DISP_INTERRUPT PPE_DISP_INT)
{
    PPE_DISP->REG_INTR_CLR |= BIT(PPE_DISP_INT);
}

void PPE_DISP_Mask_Interrupt(PPE_DISP_INTERRUPT PPE_DISP_INT, FunctionalState NewState)
{
    if (NewState != ENABLE)
    {
        PPE_DISP->REG_INTR_MASK |= BIT(PPE_DISP_INT);
    }
    else
    {
        PPE_DISP->REG_INTR_MASK &= ~BIT(PPE_DISP_INT);
    }
}

void PPE_DISP_Mask_All_Interrupt(FunctionalState NewState)
{
    if (NewState != ENABLE)
    {
        PPE_DISP->REG_INTR_MASK |= (BIT(PPE_DISP_ALL_OVER_INT) \
                               | BIT(PPE_DISP_FRAME_OVER_INT) \
                               | BIT(PPE_DISP_LOAD_OVER_INT) \
                               | BIT(PPE_DISP_LINE_OVER_INT) \
                               | BIT(PPE_DISP_SUSPEND_IACTIVE_INT) \
                               | BIT(PPE_DISP_SECURE_ERROR_INT) \
                               | BIT(PPE_DISP_BUS_ERROR_INT) \
                               | BIT(PPE_DISP_DIV0_ERR_INT) \
                              );
    }
    else
    {
        PPE_DISP->REG_INTR_MASK &= ~(BIT(PPE_DISP_ALL_OVER_INT) \
                                | BIT(PPE_DISP_FRAME_OVER_INT) \
                                | BIT(PPE_DISP_LOAD_OVER_INT) \
                                | BIT(PPE_DISP_LINE_OVER_INT) \
                                | BIT(PPE_DISP_SUSPEND_IACTIVE_INT) \
                                | BIT(PPE_DISP_SECURE_ERROR_INT) \
                                | BIT(PPE_DISP_BUS_ERROR_INT) \
                                | BIT(PPE_DISP_DIV0_ERR_INT) \
                               );
    }
}

uint8_t PPE_DISP_Get_Pixel_Size(PPE_DISP_PIXEL_FORMAT format)
{
    if (format <= PPE_DISP_RGBX8888)
    {
        return 4;
    }
    else if (format <= PPE_DISP_RGBX4444)
    {
        return 2;
    }
    else if (format <= PPE_DISP_RGBX2222)
    {
        return 1;
    }
    else if (format <= PPE_DISP_RGBX5658)
    {
        return 3;
    }
    else if (format <= PPE_DISP_RGBX5551)
    {
        return 2;
    }
    else if (format <= PPE_DISP_RGB888)
    {
        return 3;
    }
    else if (format <= PPE_DISP_RGB565)
    {
        return 2;
    }
    else if (format <= PPE_DISP_X8)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

void PPE_DISP_Finish(void)
{
    while (((PPE_DISP_REG_GLB_STATUS_TypeDef)PPE_DISP->REG_GLB_STATUS).b.run_state);
}

PPE_DISP_err PPE_DISP_Blit(ppe_buffer_t *target, ppe_buffer_t *image, ppe_matrix_t *matrix,
                 PPE_DISP_BLEND_MODE mode)
{
    if (image->opacity == 0)
    {
        return PPE_DISP_SUCCESS;
    }
    if (target->address == (uint32_t)NULL)
    {
        return PPE_DISP_ERR_NULL_TARGET;
    }
    if (image->address == (uint32_t)NULL)
    {
        return PPE_DISP_ERR_NULL_SOURCE;
    }
    if ((image->win_x_max <= image->win_x_min) || (image->win_y_max <= image->win_y_min))
    {
        return PPE_DISP_ERR_INVALID_RANGE;
    }

    if (matrix == NULL)
    {
        return PPE_DISP_ERR_INVALID_MATRIX;
    }
    ppe_matrix_t inv_matrix;
    memcpy(&inv_matrix, matrix, sizeof(ppe_matrix_t));

    uint32_t comp[9];
    ppe_matrix_inverse(&inv_matrix);
    

    ppe_rect_t rect = {.x = 0, .y = 0, .w = target->width, .h = target->height};
    ppe_rect_t src_rect = {.x = 0, .y = 0, .w = image->width, .h = image->height};
    ppe_get_area(&rect, &src_rect, matrix, target);
    if ((rect.x + rect.w <= 0) || (rect.y + rect.h <= 0)
        || (rect.x >= target->width) || (rect.y >= target->height))
    {
        return PPE_DISP_SUCCESS;
    }
    if (rect.x < 0)
    {
        rect.w = rect.w + rect.x;
        rect.x = 0;
    }
    if (rect.y < 0)
    {
        rect.h = rect.h + rect.y;
        rect.y = 0;
    }
    if (rect.x + rect.w >= target->width)
    {
        rect.w = target->width - rect.x;
    }
    if (rect.y + rect.h >= target->height)
    {
        rect.h = target->height - rect.y;
    }
    if (rect.x != 0 || rect.y != 0)
    {
        ppe_translate(rect.x, rect.y, &inv_matrix);
    }
    if (!inv_matrix2complement(&inv_matrix, comp))
    {
        return PPE_DISP_SUCCESS;
    }
    uint32_t color = ((image->opacity << 24) | 0x000000);

    //PPE_DISP_CLK_ENABLE(ENABLE);

    /*PPE_DISP Global initialization*/
    PPE_DISP_Init_Typedef PPE_DISP_Global;
    PPE_DISP_StructInit(&PPE_DISP_Global);
    PPE_DISP_Global.SetValid_AutoClear = ENABLE;
    PPE_DISP_Global.LLP = 0x0;
    PPE_DISP_Global.Secure_En = PPE_DISP_NON_SECURE_MODE;
    PPE_DISP_Init(&PPE_DISP_Global);

    /*input layer 2 initilization*/
    PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_2, ENABLE);           // logic enable of layer
    PPE_DISP_InputLayer_Init_Typedef PPE_DISP_input_layer2_init;
    HONEYGUI_PPE_DISP_InputLayer_StructInit(PPE_DISP_INPUT_2, &PPE_DISP_input_layer2_init);
    PPE_DISP_input_layer2_init.Layer_Address                   = (uint32_t)image->address;
    PPE_DISP_input_layer2_init.Pic_Height                      = image->height;
    PPE_DISP_input_layer2_init.Pic_Width                       = image->width;
    PPE_DISP_input_layer2_init.Line_Length                     = image->stride *
                                                            PPE_DISP_Get_Pixel_Size(image->format);
    PPE_DISP_input_layer2_init.Pixel_Source                    = PPE_DISP_LAYER_SRC_FROM_DMA;
    PPE_DISP_input_layer2_init.Pixel_Color_Format              = image->format;
    PPE_DISP_input_layer2_init.Const_Pixel                     = color;
    PPE_DISP_input_layer2_init.Color_Key_Mode                  = image->color_key_enable;
    if (image->high_quality)
    {
        PPE_DISP_input_layer2_init.Read_Matrix_Size                = PPV2_READ_MATRIX_2X2;
    }
    else
    {
        PPE_DISP_input_layer2_init.Read_Matrix_Size                = PPV2_READ_MATRIX_1X1;
    }
    if (image->color_key_enable >= PPE_DISP_COLOR_KEY_INSIDE)
    {
        PPE_DISP_input_layer2_init.Color_Key_Min               = image->color_key_min;
        PPE_DISP_input_layer2_init.Color_Key_Max               = image->color_key_max;
    }
    else
    {
        PPE_DISP_input_layer2_init.Color_Key_Min               = 0x000000;
        PPE_DISP_input_layer2_init.Color_Key_Max               = 0xFFFFFF;
    }

    PPE_DISP_input_layer2_init.MultiFrame_Reload_En            = DISABLE;
    PPE_DISP_input_layer2_init.MultiFrame_LLP_En               = DISABLE;
    PPE_DISP_input_layer2_init.LayerBus_Inc                    = PPE_DISP_AWBURST_INC;
    PPE_DISP_input_layer2_init.Layer_HW_Handshake_En           = PPE_DISP_HW_HS_DISABLE;
    PPE_DISP_input_layer2_init.Layer_HW_Handshake_Index        = PPE_DISP_HS_IDU_Tx;
    PPE_DISP_input_layer2_init.Layer_HW_Handshake_Polarity     = PPE_DISP_HW_HS_ACTIVE_HIGH;
    PPE_DISP_input_layer2_init.Layer_HW_Handshake_MsizeLog     = PPE_DISP_MSIZE_16;
    PPE_DISP_input_layer2_init.Layer_Window_Xmin               = 0;
    PPE_DISP_input_layer2_init.Layer_Window_Xmax               = rect.w;
    PPE_DISP_input_layer2_init.Layer_Window_Ymin               = 0;
    PPE_DISP_input_layer2_init.Layer_Window_Ymax               = rect.h;

    uint32_t ct = 0;
    PPE_DISP_input_layer2_init.Transfer_Matrix_E11             = comp[ct++];
    PPE_DISP_input_layer2_init.Transfer_Matrix_E12             = comp[ct++];
    PPE_DISP_input_layer2_init.Transfer_Matrix_E13             = comp[ct++];
    PPE_DISP_input_layer2_init.Transfer_Matrix_E21             = comp[ct++];
    PPE_DISP_input_layer2_init.Transfer_Matrix_E22             = comp[ct++];
    PPE_DISP_input_layer2_init.Transfer_Matrix_E23             = comp[ct++];

    PPE_DISP_InputLayer_Init(PPE_DISP_INPUT_2, &PPE_DISP_input_layer2_init);

    if (mode == PPE_DISP_SRC_OVER_MODE)
    {
        /*input layer 1 initilization*/
        PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_1, ENABLE);           // logic enable of layer
        PPE_DISP_InputLayer_Init_Typedef PPE_DISP_input_layer1_init;
        HONEYGUI_PPE_DISP_InputLayer_StructInit(PPE_DISP_INPUT_1, &PPE_DISP_input_layer1_init);
        PPE_DISP_input_layer1_init.Layer_Address                   = target->address +
                                                                (rect.y * target->stride + rect.x) * PPE_DISP_Get_Pixel_Size(target->format);
        PPE_DISP_input_layer1_init.Pic_Height                      = (uint32_t)rect.h;
        PPE_DISP_input_layer1_init.Pic_Width                       = (uint32_t)rect.w;
        PPE_DISP_input_layer1_init.Line_Length                     = target->stride * PPE_DISP_Get_Pixel_Size(
                                                                    target->format);
        PPE_DISP_input_layer1_init.Pixel_Source                    = PPE_DISP_LAYER_SRC_FROM_DMA;
        PPE_DISP_input_layer1_init.Pixel_Color_Format              = target->format;
        PPE_DISP_input_layer1_init.Const_Pixel                     = 0xFFFFFFFF;
        PPE_DISP_input_layer1_init.MultiFrame_Reload_En            = DISABLE;
        PPE_DISP_input_layer1_init.MultiFrame_LLP_En               = DISABLE;
        PPE_DISP_input_layer1_init.Read_Matrix_Size                = PPV2_READ_MATRIX_1X1;
        PPE_DISP_input_layer1_init.LayerBus_Inc                    = PPE_DISP_AWBURST_INC;
        PPE_DISP_input_layer1_init.Layer_HW_Handshake_En           = PPE_DISP_HW_HS_DISABLE;
        PPE_DISP_input_layer1_init.Layer_HW_Handshake_Index        = PPE_DISP_HS_IDU_Tx;
        PPE_DISP_input_layer1_init.Layer_HW_Handshake_Polarity     = PPE_DISP_HW_HS_ACTIVE_HIGH;
        PPE_DISP_input_layer1_init.Layer_HW_Handshake_MsizeLog     = PPE_DISP_MSIZE_16;
        PPE_DISP_input_layer1_init.Layer_Window_Xmin               = 0;
        PPE_DISP_input_layer1_init.Layer_Window_Xmax               = rect.w;
        PPE_DISP_input_layer1_init.Layer_Window_Ymin               = 0;
        PPE_DISP_input_layer1_init.Layer_Window_Ymax               = rect.h;

        PPE_DISP_input_layer1_init.Transfer_Matrix_E11             = 0x10000;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E12             = 0;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E13             = 0;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E21             = 0;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E22             = 0x10000;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E23             = 0;

        PPE_DISP_InputLayer_Init(PPE_DISP_INPUT_1, &PPE_DISP_input_layer1_init);
    }
    else if (mode == PPE_DISP_BYPASS_MODE)
    {
        PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_1, ENABLE);           // logic enable of layer
        PPE_DISP_InputLayer_Init_Typedef PPE_DISP_input_layer1_init;
        HONEYGUI_PPE_DISP_InputLayer_StructInit(PPE_DISP_INPUT_1, &PPE_DISP_input_layer1_init);
        PPE_DISP_input_layer1_init.Layer_Address                   = (uint32_t)NULL;
        PPE_DISP_input_layer1_init.Pic_Height                      = (uint32_t)rect.h;
        PPE_DISP_input_layer1_init.Pic_Width                       = (uint32_t)rect.w;
        PPE_DISP_input_layer1_init.Line_Length                     = target->stride * PPE_DISP_Get_Pixel_Size(
                                                                    target->format);
        PPE_DISP_input_layer1_init.Pixel_Source                    = PPE_DISP_LAYER_SRC_CONST;
        PPE_DISP_input_layer1_init.Pixel_Color_Format              = target->format;
        PPE_DISP_input_layer1_init.Const_Pixel                     = 0x0;
        PPE_DISP_input_layer1_init.MultiFrame_Reload_En            = DISABLE;
        PPE_DISP_input_layer1_init.MultiFrame_LLP_En               = DISABLE;
        PPE_DISP_input_layer1_init.Read_Matrix_Size                = PPV2_READ_MATRIX_1X1;
        PPE_DISP_input_layer1_init.LayerBus_Inc                    = PPE_DISP_AWBURST_INC;
        PPE_DISP_input_layer1_init.Layer_HW_Handshake_En           = PPE_DISP_HW_HS_DISABLE;
        PPE_DISP_input_layer1_init.Layer_HW_Handshake_Index        = PPE_DISP_HS_IDU_Tx;
        PPE_DISP_input_layer1_init.Layer_HW_Handshake_Polarity     = PPE_DISP_HW_HS_ACTIVE_HIGH;
        PPE_DISP_input_layer1_init.Layer_HW_Handshake_MsizeLog     = PPE_DISP_MSIZE_16;
        PPE_DISP_input_layer1_init.Layer_Window_Xmin               = 0;
        PPE_DISP_input_layer1_init.Layer_Window_Xmax               = rect.w;
        PPE_DISP_input_layer1_init.Layer_Window_Ymin               = 0;
        PPE_DISP_input_layer1_init.Layer_Window_Ymax               = rect.h;

        PPE_DISP_input_layer1_init.Transfer_Matrix_E11             = 0x10000;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E12             = 0;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E13             = 0;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E21             = 0;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E22             = 0x10000;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E23             = 0;

        PPE_DISP_InputLayer_Init(PPE_DISP_INPUT_1, &PPE_DISP_input_layer1_init);
    }
    else
    {
        PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_1, DISABLE);
    }

    PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_3, DISABLE);
    PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_4, DISABLE);

    PPE_DISP_ResultLayer_Init_Typedef PPE_DISP_ResultLayer0_Init;
    HONEYGUI_PPE_DISP_ResultLayer_StructInit(&PPE_DISP_ResultLayer0_Init);
    PPE_DISP_ResultLayer0_Init.Layer_Address                   = target->address +
                                                            (rect.y * target->stride + rect.x) * PPE_DISP_Get_Pixel_Size(target->format);
    PPE_DISP_ResultLayer0_Init.Canvas_Height                   = rect.h;
    PPE_DISP_ResultLayer0_Init.Canvas_Width                    = rect.w;
    PPE_DISP_ResultLayer0_Init.Line_Length                     = target->stride * PPE_DISP_Get_Pixel_Size(
                                                                target->format);
    PPE_DISP_ResultLayer0_Init.Color_Format                    = target->format;
    PPE_DISP_ResultLayer0_Init.BackGround                      = target->const_color;
    PPE_DISP_ResultLayer0_Init.LayerBus_Inc                    = PPE_DISP_AWBURST_INC;
    PPE_DISP_ResultLayer0_Init.MultiFrame_Reload_En            = DISABLE;
    PPE_DISP_ResultLayer0_Init.MultiFrame_LLP_En               = DISABLE;
    PPE_DISP_ResultLayer_Init(&PPE_DISP_ResultLayer0_Init);

    HONEYGUI_PPE_DISP_Cmd(ENABLE);
    while (((PPE_DISP_REG_GLB_STATUS_TypeDef)PPE_DISP->REG_GLB_STATUS).b.run_state);
    return PPE_DISP_SUCCESS;
}

PPE_DISP_err PPE_DISP_Blit_Inverse(ppe_buffer_t *dst, ppe_buffer_t *src, ppe_matrix_t *inverse,
                         ppe_rect_t *rect, PPE_DISP_BLEND_MODE mode)
{
    if (dst->address == (uint32_t)NULL)
    {
        return PPE_DISP_ERR_NULL_TARGET;
    }
    if (src->address == (uint32_t)NULL && mode != PPE_DISP_CONST_MASK_MODE)
    {
        return PPE_DISP_ERR_NULL_SOURCE;
    }
    if ((src->win_x_max <= src->win_x_min) || (src->win_y_max <= src->win_y_min))
    {
        return PPE_DISP_ERR_INVALID_RANGE;
    }
    if (rect->h == 0 || rect->w == 0)
    {
        return PPE_DISP_SUCCESS;
    }
    if (inverse == NULL)
    {
        return PPE_DISP_ERR_INVALID_MATRIX;
    }
    ppe_matrix_t matrix;
    ppe_get_identity(&matrix);
    memcpy(&matrix, inverse, sizeof(ppe_matrix_t));
    ppe_translate(rect->x, rect->y, &matrix);
    bool is_identity = ppe_is_identity_or_translate(&matrix);
    uint32_t comp[9];
    if (!inv_matrix2complement(&matrix, comp))
    {
        return PPE_DISP_SUCCESS;
    }
  

    if (src->opacity == 0)
    {
        return PPE_DISP_SUCCESS;
    }

    uint32_t color = ((src->opacity << 24) | 0x000000);

    //PPE_DISP_CLK_ENABLE(ENABLE);


    /*PPE_DISP Global initialization*/
    PPE_DISP_Init_Typedef PPE_DISP_Global;
    PPE_DISP_StructInit(&PPE_DISP_Global);
    PPE_DISP_Global.SetValid_AutoClear = ENABLE;
    PPE_DISP_Global.LLP = 0x0;
    PPE_DISP_Global.Secure_En = PPE_DISP_NON_SECURE_MODE;
    PPE_DISP_Init(&PPE_DISP_Global);

    /*input layer 2 initilization*/
    PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_2, ENABLE);  
    PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_1, ENABLE);           // logic enable of layer
    PPE_DISP_InputLayer_Init_Typedef PPE_DISP_input_layer2_init;
    HONEYGUI_PPE_DISP_InputLayer_StructInit(PPE_DISP_INPUT_2, &PPE_DISP_input_layer2_init);
    PPE_DISP_input_layer2_init.Layer_Address                   = (uint32_t)src->address;
    PPE_DISP_input_layer2_init.Pic_Height                      = (uint32_t)src->height;
    PPE_DISP_input_layer2_init.Pic_Width                       = (uint32_t)src->width;
    PPE_DISP_input_layer2_init.Line_Length                     = src->stride *
                                                            PPE_DISP_Get_Pixel_Size(src->format);
    if (mode != PPE_DISP_CONST_MASK_MODE)
    {
        PPE_DISP_input_layer2_init.Pixel_Source                = PPE_DISP_LAYER_SRC_FROM_DMA;
        PPE_DISP_input_layer2_init.Const_Pixel                 = color;
    }
    else
    {
        PPE_DISP_input_layer2_init.Pixel_Source                = PPE_DISP_LAYER_SRC_CONST;
        PPE_DISP_input_layer2_init.Const_Pixel                 = src->const_color;
    }
    PPE_DISP_input_layer2_init.Pixel_Color_Format              = src->format;
    PPE_DISP_input_layer2_init.Color_Key_Mode                  = src->color_key_enable;
    if (src->high_quality)
    {
        PPE_DISP_input_layer2_init.Read_Matrix_Size                = PPV2_READ_MATRIX_2X2;
    }
    else
    {
        PPE_DISP_input_layer2_init.Read_Matrix_Size                = PPV2_READ_MATRIX_1X1;
    }
    if (src->color_key_enable >= PPE_DISP_COLOR_KEY_INSIDE)
    {
        PPE_DISP_input_layer2_init.Color_Key_Min               = src->color_key_min;
        PPE_DISP_input_layer2_init.Color_Key_Max               = src->color_key_max;
    }
    else
    {
        PPE_DISP_input_layer2_init.Color_Key_Min               = 0x000000;
        PPE_DISP_input_layer2_init.Color_Key_Max               = 0xFFFFFF;
    }

    PPE_DISP_input_layer2_init.MultiFrame_Reload_En            = DISABLE;
    PPE_DISP_input_layer2_init.MultiFrame_LLP_En               = DISABLE;
    PPE_DISP_input_layer2_init.LayerBus_Inc                    = PPE_DISP_AWBURST_INC;
    PPE_DISP_input_layer2_init.Layer_HW_Handshake_En           = PPE_DISP_HW_HS_DISABLE;
    PPE_DISP_input_layer2_init.Layer_HW_Handshake_Index        = PPE_DISP_HS_IDU_Tx;
    PPE_DISP_input_layer2_init.Layer_HW_Handshake_Polarity     = PPE_DISP_HW_HS_ACTIVE_HIGH;
    PPE_DISP_input_layer2_init.Layer_HW_Handshake_MsizeLog     = PPE_DISP_MSIZE_16;
    PPE_DISP_input_layer2_init.Layer_Window_Xmin               = src->win_x_min;
    PPE_DISP_input_layer2_init.Layer_Window_Xmax               = src->win_x_max;
    PPE_DISP_input_layer2_init.Layer_Window_Ymin               = src->win_y_min;
    PPE_DISP_input_layer2_init.Layer_Window_Ymax               = src->win_y_max;

    uint32_t ct = 0;
    PPE_DISP_input_layer2_init.Transfer_Matrix_E11             = comp[ct++];
    PPE_DISP_input_layer2_init.Transfer_Matrix_E12             = comp[ct++];
    PPE_DISP_input_layer2_init.Transfer_Matrix_E13             = comp[ct++];
    PPE_DISP_input_layer2_init.Transfer_Matrix_E21             = comp[ct++];
    PPE_DISP_input_layer2_init.Transfer_Matrix_E22             = comp[ct++];
    PPE_DISP_input_layer2_init.Transfer_Matrix_E23             = comp[ct++];
    PPE_DISP_InputLayer_Init(PPE_DISP_INPUT_2, &PPE_DISP_input_layer2_init);

    if (mode == PPE_DISP_SRC_OVER_MODE || !is_identity ||
        (mode == PPE_DISP_CONST_MASK_MODE && (src->const_color >> 24) < 0xFF))
    {
        /*input layer 1 initilization*/
        PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_1, ENABLE);           // logic enable of layer
        PPE_DISP_InputLayer_Init_Typedef PPE_DISP_input_layer1_init;
        HONEYGUI_PPE_DISP_InputLayer_StructInit(PPE_DISP_INPUT_1, &PPE_DISP_input_layer1_init);
        PPE_DISP_input_layer1_init.Layer_Address                   = dst->address +
                                                                (rect->y * dst->stride + rect->x) * PPE_DISP_Get_Pixel_Size(dst->format);
        PPE_DISP_input_layer1_init.Pic_Height                      = (uint32_t)rect->h;
        PPE_DISP_input_layer1_init.Pic_Width                       = (uint32_t)rect->w;
        PPE_DISP_input_layer1_init.Line_Length                     = dst->stride * PPE_DISP_Get_Pixel_Size(
                                                                    dst->format);
        PPE_DISP_input_layer1_init.Pixel_Source                    = PPE_DISP_LAYER_SRC_FROM_DMA;
        PPE_DISP_input_layer1_init.Pixel_Color_Format              = dst->format;
        PPE_DISP_input_layer1_init.Const_Pixel                     = 0xFFFFFFFF;
        PPE_DISP_input_layer1_init.MultiFrame_Reload_En            = DISABLE;
        PPE_DISP_input_layer1_init.MultiFrame_LLP_En               = DISABLE;
        PPE_DISP_input_layer1_init.Read_Matrix_Size                = PPV2_READ_MATRIX_1X1;
        PPE_DISP_input_layer1_init.LayerBus_Inc                    = PPE_DISP_AWBURST_INC;
        PPE_DISP_input_layer1_init.Layer_HW_Handshake_En           = PPE_DISP_HW_HS_DISABLE;
        PPE_DISP_input_layer1_init.Layer_HW_Handshake_Index        = PPE_DISP_HS_IDU_Tx;
        PPE_DISP_input_layer1_init.Layer_HW_Handshake_Polarity     = PPE_DISP_HW_HS_ACTIVE_HIGH;
        PPE_DISP_input_layer1_init.Layer_HW_Handshake_MsizeLog     = PPE_DISP_MSIZE_16;
        PPE_DISP_input_layer1_init.Layer_Window_Xmin               = 0;
        PPE_DISP_input_layer1_init.Layer_Window_Xmax               = dst->width;
        PPE_DISP_input_layer1_init.Layer_Window_Ymin               = 0;
        PPE_DISP_input_layer1_init.Layer_Window_Ymax               = dst->height;

        PPE_DISP_input_layer1_init.Transfer_Matrix_E11             = 16;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E12             = 0;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E13             = 0;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E21             = 0;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E22             = 16;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E23             = 0;

        PPE_DISP_InputLayer_Init(PPE_DISP_INPUT_1, &PPE_DISP_input_layer1_init);
    }
    else if (mode == PPE_DISP_BYPASS_MODE || (mode == PPE_DISP_CONST_MASK_MODE &&
                                         is_identity && (src->const_color >> 24) == 0xFF))
    {
        PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_1, ENABLE);           // logic enable of layer
        PPE_DISP_InputLayer_Init_Typedef PPE_DISP_input_layer1_init;
        HONEYGUI_PPE_DISP_InputLayer_StructInit(PPE_DISP_INPUT_1, &PPE_DISP_input_layer1_init);
        PPE_DISP_input_layer1_init.Layer_Address                   = (uint32_t)NULL;
        PPE_DISP_input_layer1_init.Pic_Height                      = (uint32_t)rect->h;
        PPE_DISP_input_layer1_init.Pic_Width                       = (uint32_t)rect->w;
        PPE_DISP_input_layer1_init.Line_Length                     = dst->stride * PPE_DISP_Get_Pixel_Size(
                                                                    dst->format);
        PPE_DISP_input_layer1_init.Pixel_Source                    = PPE_DISP_LAYER_SRC_CONST;
        PPE_DISP_input_layer1_init.Pixel_Color_Format              = dst->format;
        PPE_DISP_input_layer1_init.Const_Pixel                     = 0x0;
        PPE_DISP_input_layer1_init.MultiFrame_Reload_En            = DISABLE;
        PPE_DISP_input_layer1_init.MultiFrame_LLP_En               = DISABLE;
        PPE_DISP_input_layer1_init.Read_Matrix_Size                = PPV2_READ_MATRIX_1X1;
        PPE_DISP_input_layer1_init.LayerBus_Inc                    = PPE_DISP_AWBURST_INC;
        PPE_DISP_input_layer1_init.Layer_HW_Handshake_En           = PPE_DISP_HW_HS_DISABLE;
        PPE_DISP_input_layer1_init.Layer_HW_Handshake_Index        = PPE_DISP_HS_IDU_Tx;
        PPE_DISP_input_layer1_init.Layer_HW_Handshake_Polarity     = PPE_DISP_HW_HS_ACTIVE_HIGH;
        PPE_DISP_input_layer1_init.Layer_HW_Handshake_MsizeLog     = PPE_DISP_MSIZE_16;
        PPE_DISP_input_layer1_init.Layer_Window_Xmin               = 0;
        PPE_DISP_input_layer1_init.Layer_Window_Xmax               = dst->width;
        PPE_DISP_input_layer1_init.Layer_Window_Ymin               = 0;
        PPE_DISP_input_layer1_init.Layer_Window_Ymax               = dst->height;

        PPE_DISP_input_layer1_init.Transfer_Matrix_E11             = 16;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E12             = 0;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E13             = 0;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E21             = 0;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E22             = 16;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E23             = 0;
        PPE_DISP_InputLayer_Init(PPE_DISP_INPUT_1, &PPE_DISP_input_layer1_init);
    }
    else
    {
        PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_1, DISABLE);
    }

    PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_3, DISABLE);
    PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_4, DISABLE);

    PPE_DISP_ResultLayer_Init_Typedef PPE_DISP_ResultLayer0_Init;
    HONEYGUI_PPE_DISP_ResultLayer_StructInit(&PPE_DISP_ResultLayer0_Init);
    PPE_DISP_ResultLayer0_Init.Layer_Address                   = dst->address +
                                                            (rect->y * dst->stride + rect->x) * PPE_DISP_Get_Pixel_Size(dst->format);
    PPE_DISP_ResultLayer0_Init.Canvas_Height                   = rect->h;
    PPE_DISP_ResultLayer0_Init.Canvas_Width                    = rect->w;
    PPE_DISP_ResultLayer0_Init.Line_Length                     = dst->stride * PPE_DISP_Get_Pixel_Size(
                                                                dst->format);
    PPE_DISP_ResultLayer0_Init.Color_Format                    = dst->format;
    PPE_DISP_ResultLayer0_Init.BackGround                      = dst->const_color;
    PPE_DISP_ResultLayer0_Init.LayerBus_Inc                    = PPE_DISP_AWBURST_INC;
    PPE_DISP_ResultLayer0_Init.MultiFrame_Reload_En            = DISABLE;
    PPE_DISP_ResultLayer0_Init.MultiFrame_LLP_En               = DISABLE;
if (PPE_DISP_Get_Pixel_Size(src->format) == 2)
    {
        PPE_DISP_ResultLayer0_Init.block_width = 128;
        PPE_DISP_ResultLayer0_Init.block_height = 2;
    }
    else if (PPE_DISP_Get_Pixel_Size(src->format) == 3)
    {
        PPE_DISP_ResultLayer0_Init.block_width = 64;
        PPE_DISP_ResultLayer0_Init.block_height = 1;
    }
    else if (PPE_DISP_Get_Pixel_Size(src->format) == 4)
    {
        PPE_DISP_ResultLayer0_Init.block_width = 64;
        PPE_DISP_ResultLayer0_Init.block_height = 1;
    }
    else if (PPE_DISP_Get_Pixel_Size(src->format) <= 1)
    {
        PPE_DISP_ResultLayer0_Init.block_width = 128;
        PPE_DISP_ResultLayer0_Init.block_height = 2;
    }
    if (PPE_DISP_ResultLayer0_Init.block_width > PPE_DISP_ResultLayer0_Init.Canvas_Width)
    {
        PPE_DISP_ResultLayer0_Init.block_width = PPE_DISP_ResultLayer0_Init.Canvas_Width;
    }
    if (PPE_DISP_ResultLayer0_Init.block_height > PPE_DISP_ResultLayer0_Init.Canvas_Height)
    {
        PPE_DISP_ResultLayer0_Init.block_height = PPE_DISP_ResultLayer0_Init.Canvas_Height;
    }
    PPE_DISP_ResultLayer_Init(&PPE_DISP_ResultLayer0_Init);
    // uint32_t* reg_addr = (uint32_t*)PPE_DISP_Input_Layer1_BASE;
    // for(uint32_t i = 0; i < sizeof(PPE_DISP_Input_Layer_Typedef) / 4; i++)
    // {
    //     gui_log("layer 1 0x%02x: 0x%08x\n", i * 4, reg_addr[i]);
    // }
    // gui_log("==============================================\n");
    // reg_addr = (uint32_t*)PPE_DISP_Input_Layer2_BASE;
    // for(uint32_t i = 0; i < sizeof(PPE_DISP_Input_Layer_Typedef) / 4; i++)
    // {
    //     gui_log("layer 2 0x%02x: 0x%08x\n", i * 4, reg_addr[i]);
    // }
    // gui_log("==============================================\n");
    // reg_addr = (uint32_t*)PPE_DISP_Result_Layer_BASE;
    // for(uint32_t i = 0; i < sizeof(PPE_DISP_ResultLayer_Typedef) / 4; i++)
    // {
    //     gui_log("layer 0 0x%02x: 0x%08x\n", i * 4, reg_addr[i]);
    // }
    // gui_log("==============================================\n");
    // reg_addr = (uint32_t*)PPE_DISP;
    // for(uint32_t i = 0; i < sizeof(PPE_DISP_Typedef) / 4; i++)
    // {
    //     gui_log("global 0x%02x: 0x%08x\n", i * 4, reg_addr[i]);
    // }

    // uint32_t time1 = rtos_time_get_current_system_time_us();
    HONEYGUI_PPE_DISP_Cmd(ENABLE);
    while (((PPE_DISP_REG_GLB_STATUS_TypeDef)PPE_DISP->REG_GLB_STATUS).b.run_state);
    // uint32_t time2 = rtos_time_get_current_system_time_us();
    // gui_log("PPE blend time = %d us \n", time2 - time1);
    return PPE_DISP_SUCCESS;
}

PPE_DISP_err PPE_DISP_Mask(ppe_buffer_t *dst, uint32_t color, ppe_rect_t *rect)
{
    if (dst->address == (uint32_t)NULL)
    {
        return PPE_DISP_ERR_NULL_TARGET;
    }
    if (rect->h == 0 || rect->w == 0)
    {
        return PPE_DISP_SUCCESS;
    }

    //PPE_DISP_CLK_ENABLE(ENABLE);

    /*PPE_DISP Global initialization*/
    PPE_DISP_Init_Typedef PPE_DISP_Global;
    PPE_DISP_StructInit(&PPE_DISP_Global);
    PPE_DISP_Global.SetValid_AutoClear = ENABLE;
    PPE_DISP_Global.LLP = 0x0;
    PPE_DISP_Global.Secure_En = PPE_DISP_NON_SECURE_MODE;
    PPE_DISP_Init(&PPE_DISP_Global);

    uint32_t mask_color = color;

    /*input layer 2 initilization*/
    PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_2, ENABLE);           // logic enable of layer
    PPE_DISP_InputLayer_Init_Typedef PPE_DISP_input_layer2_init;
    HONEYGUI_PPE_DISP_InputLayer_StructInit(PPE_DISP_INPUT_2, &PPE_DISP_input_layer2_init);
    PPE_DISP_input_layer2_init.Layer_Address                   = (uint32_t)NULL;
    PPE_DISP_input_layer2_init.Pic_Height                      = rect->h;
    PPE_DISP_input_layer2_init.Pic_Width                       = rect->w;
    PPE_DISP_input_layer2_init.Line_Length                     = rect->w * 4;
    PPE_DISP_input_layer2_init.Pixel_Color_Format              = PPE_DISP_ARGB8888;
    PPE_DISP_input_layer2_init.Pixel_Source                    = PPE_DISP_LAYER_SRC_CONST;
    PPE_DISP_input_layer2_init.Const_Pixel                     = mask_color;
    PPE_DISP_input_layer2_init.Color_Key_Mode                  = PPE_DISP_COLOR_KEY_DISABLE;
    PPE_DISP_input_layer2_init.Read_Matrix_Size                = PPV2_READ_MATRIX_1X1;

    PPE_DISP_input_layer2_init.MultiFrame_Reload_En            = DISABLE;
    PPE_DISP_input_layer2_init.MultiFrame_LLP_En               = DISABLE;
    PPE_DISP_input_layer2_init.LayerBus_Inc                    = PPE_DISP_AWBURST_FIXED;
    PPE_DISP_input_layer2_init.Layer_HW_Handshake_En           = PPE_DISP_HW_HS_DISABLE;
    PPE_DISP_input_layer2_init.Layer_HW_Handshake_Index        = PPE_DISP_HS_IDU_Tx;
    PPE_DISP_input_layer2_init.Layer_HW_Handshake_Polarity     = PPE_DISP_HW_HS_ACTIVE_HIGH;
    PPE_DISP_input_layer2_init.Layer_HW_Handshake_MsizeLog     = PPE_DISP_MSIZE_16;
    PPE_DISP_input_layer2_init.Layer_Window_Xmin               = 0;
    PPE_DISP_input_layer2_init.Layer_Window_Xmax               = rect->w;
    PPE_DISP_input_layer2_init.Layer_Window_Ymin               = 0;
    PPE_DISP_input_layer2_init.Layer_Window_Ymax               = rect->h;

    PPE_DISP_input_layer2_init.Transfer_Matrix_E11             = 0x10;
    PPE_DISP_input_layer2_init.Transfer_Matrix_E12             = 0;
    PPE_DISP_input_layer2_init.Transfer_Matrix_E13             = 0;
    PPE_DISP_input_layer2_init.Transfer_Matrix_E21             = 0;
    PPE_DISP_input_layer2_init.Transfer_Matrix_E22             = 0x10;
    PPE_DISP_input_layer2_init.Transfer_Matrix_E23             = 0;
    PPE_DISP_InputLayer_Init(PPE_DISP_INPUT_2, &PPE_DISP_input_layer2_init);

    if ((mask_color & 0xFF000000) != 0xFF000000)
    {
        /*input layer 1 initilization*/
        PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_1, ENABLE);           // logic enable of layer
        PPE_DISP_InputLayer_Init_Typedef PPE_DISP_input_layer1_init;
        HONEYGUI_PPE_DISP_InputLayer_StructInit(PPE_DISP_INPUT_1, &PPE_DISP_input_layer1_init);
        PPE_DISP_input_layer1_init.Layer_Address                   = dst->address +
                                                                (rect->y * dst->stride + rect->x) * PPE_DISP_Get_Pixel_Size(dst->format);
        PPE_DISP_input_layer1_init.Pic_Height                      = (uint32_t)rect->h;
        PPE_DISP_input_layer1_init.Pic_Width                       = (uint32_t)rect->w;
        PPE_DISP_input_layer1_init.Line_Length                     = dst->stride * PPE_DISP_Get_Pixel_Size(
                                                                    dst->format);
        PPE_DISP_input_layer1_init.Pixel_Source                    = PPE_DISP_LAYER_SRC_FROM_DMA;
        PPE_DISP_input_layer1_init.Pixel_Color_Format              = dst->format;
        PPE_DISP_input_layer1_init.Const_Pixel                     = 0xFF000000;
        PPE_DISP_input_layer1_init.MultiFrame_Reload_En            = DISABLE;
        PPE_DISP_input_layer1_init.MultiFrame_LLP_En               = DISABLE;
        PPE_DISP_input_layer1_init.Read_Matrix_Size                = PPV2_READ_MATRIX_1X1;
        PPE_DISP_input_layer1_init.LayerBus_Inc                    = PPE_DISP_AWBURST_INC;
        PPE_DISP_input_layer1_init.Layer_HW_Handshake_En           = PPE_DISP_HW_HS_DISABLE;
        PPE_DISP_input_layer1_init.Layer_HW_Handshake_Index        = PPE_DISP_HS_IDU_Tx;
        PPE_DISP_input_layer1_init.Layer_HW_Handshake_Polarity     = PPE_DISP_HW_HS_ACTIVE_HIGH;
        PPE_DISP_input_layer1_init.Layer_HW_Handshake_MsizeLog     = PPE_DISP_MSIZE_16;
        PPE_DISP_input_layer1_init.Layer_Window_Xmin               = 0;
        PPE_DISP_input_layer1_init.Layer_Window_Xmax               = dst->width;
        PPE_DISP_input_layer1_init.Layer_Window_Ymin               = 0;
        PPE_DISP_input_layer1_init.Layer_Window_Ymax               = dst->height;

        PPE_DISP_input_layer1_init.Transfer_Matrix_E11             = 0x10;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E12             = 0;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E13             = 0;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E21             = 0;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E22             = 0x10;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E23             = 0;

        PPE_DISP_InputLayer_Init(PPE_DISP_INPUT_1, &PPE_DISP_input_layer1_init);
    }
    else
    {
        PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_1, DISABLE);           // logic enable of layer
    }

    PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_3, DISABLE);
    PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_4, DISABLE);

    PPE_DISP_ResultLayer_Init_Typedef PPE_DISP_ResultLayer0_Init;
    HONEYGUI_PPE_DISP_ResultLayer_StructInit(&PPE_DISP_ResultLayer0_Init);
    PPE_DISP_ResultLayer0_Init.Layer_Address                   = dst->address +
                                                            (rect->y * dst->stride + rect->x) * PPE_DISP_Get_Pixel_Size(dst->format);
    PPE_DISP_ResultLayer0_Init.Canvas_Height                   = rect->h;
    PPE_DISP_ResultLayer0_Init.Canvas_Width                    = rect->w;
    PPE_DISP_ResultLayer0_Init.Line_Length                     = dst->stride * PPE_DISP_Get_Pixel_Size(
                                                                dst->format);
    PPE_DISP_ResultLayer0_Init.Color_Format                    = dst->format;
    PPE_DISP_ResultLayer0_Init.BackGround                      = dst->const_color;
    PPE_DISP_ResultLayer0_Init.LayerBus_Inc                    = PPE_DISP_AWBURST_INC;
    PPE_DISP_ResultLayer0_Init.MultiFrame_Reload_En            = DISABLE;
    PPE_DISP_ResultLayer0_Init.MultiFrame_LLP_En               = DISABLE;
    PPE_DISP_ResultLayer0_Init.block_width = 128;
    PPE_DISP_ResultLayer0_Init.block_height = 1;
    if (PPE_DISP_ResultLayer0_Init.block_width > PPE_DISP_ResultLayer0_Init.Canvas_Width)
    {
        PPE_DISP_ResultLayer0_Init.block_width = PPE_DISP_ResultLayer0_Init.Canvas_Width;
    }
    
    PPE_DISP_ResultLayer_Init(&PPE_DISP_ResultLayer0_Init);

    HONEYGUI_PPE_DISP_Cmd(ENABLE);
    while (((PPE_DISP_REG_GLB_STATUS_TypeDef)PPE_DISP->REG_GLB_STATUS).b.run_state);
    return PPE_DISP_SUCCESS;
}

PPE_DISP_err PPE_DISP_Blend_Multi(ppe_buffer_t *dst, ppe_buffer_t *src_1,
                        ppe_buffer_t *src_2, ppe_buffer_t *src_3, PPE_DISP_BLEND_MODE mode)
{
    if (dst->address == (uint32_t)NULL)
    {
        return PPE_DISP_ERR_NULL_TARGET;
    }
    //PPE_DISP_CLK_ENABLE(ENABLE);
    /*PPE_DISP Global initialization*/
    PPE_DISP_Init_Typedef PPE_DISP_Global;
    PPE_DISP_StructInit(&PPE_DISP_Global);
    PPE_DISP_Global.SetValid_AutoClear = ENABLE;
    PPE_DISP_Global.LLP = 0x0;
    PPE_DISP_Global.Secure_En = PPE_DISP_NON_SECURE_MODE;
    PPE_DISP_Init(&PPE_DISP_Global);

    if (src_1 != NULL)
    {
        ppe_matrix_t matrix;
//        memcpy(&matrix, &src_1->inv_matrix, sizeof(ppe_matrix_t));

        uint32_t color = ((src_1->opacity << 24) | 0x000000);

        uint32_t comp[9];
        if (!inv_matrix2complement(&matrix, comp))
        {
            return PPE_DISP_ERR_INVALID_MATRIX;
        }

        /*input layer 2 initilization*/
        PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_2, ENABLE);           // logic enable of layer
        PPE_DISP_InputLayer_Init_Typedef PPE_DISP_input_layer2_init;
        HONEYGUI_PPE_DISP_InputLayer_StructInit(PPE_DISP_INPUT_2, &PPE_DISP_input_layer2_init);
        PPE_DISP_input_layer2_init.Layer_Address                   = (uint32_t)src_1->address;
        PPE_DISP_input_layer2_init.Pic_Height                      = (uint32_t)src_1->height;
        PPE_DISP_input_layer2_init.Pic_Width                       = (uint32_t)src_1->width;
        PPE_DISP_input_layer2_init.Line_Length                     = (uint32_t)src_1->stride *
                                                                PPE_DISP_Get_Pixel_Size(src_1->format);
        PPE_DISP_input_layer2_init.Pixel_Source                    = PPE_DISP_LAYER_SRC_FROM_DMA;
        PPE_DISP_input_layer2_init.Pixel_Color_Format              = src_1->format;
        PPE_DISP_input_layer2_init.Const_Pixel                     = color;
        PPE_DISP_input_layer2_init.Color_Key_Mode                  = src_1->color_key_enable;
        if (src_1->high_quality)
        {
            PPE_DISP_input_layer2_init.Read_Matrix_Size                = PPV2_READ_MATRIX_2X2;
        }
        else
        {
            PPE_DISP_input_layer2_init.Read_Matrix_Size                = PPV2_READ_MATRIX_1X1;
        }
        if (src_1->color_key_enable >= PPE_DISP_COLOR_KEY_INSIDE)
        {
            PPE_DISP_input_layer2_init.Color_Key_Min               = src_1->color_key_min;
            PPE_DISP_input_layer2_init.Color_Key_Max               = src_1->color_key_max;
        }
        else
        {
            PPE_DISP_input_layer2_init.Color_Key_Min               = 0x000000;
            PPE_DISP_input_layer2_init.Color_Key_Max               = 0xFFFFFF;
        }

        PPE_DISP_input_layer2_init.MultiFrame_Reload_En            = DISABLE;
        PPE_DISP_input_layer2_init.MultiFrame_LLP_En               = DISABLE;
        PPE_DISP_input_layer2_init.LayerBus_Inc                    = PPE_DISP_AWBURST_INC;
        PPE_DISP_input_layer2_init.Layer_HW_Handshake_En           = PPE_DISP_HW_HS_DISABLE;
        PPE_DISP_input_layer2_init.Layer_HW_Handshake_Index        = PPE_DISP_HS_IDU_Tx;
        PPE_DISP_input_layer2_init.Layer_HW_Handshake_Polarity     = PPE_DISP_HW_HS_ACTIVE_HIGH;
        PPE_DISP_input_layer2_init.Layer_HW_Handshake_MsizeLog     = PPE_DISP_MSIZE_1;
        PPE_DISP_input_layer2_init.Layer_Window_Xmin               = src_1->win_x_min;
        PPE_DISP_input_layer2_init.Layer_Window_Xmax               = src_1->win_x_max;
        PPE_DISP_input_layer2_init.Layer_Window_Ymin               = src_1->win_y_min;
        PPE_DISP_input_layer2_init.Layer_Window_Ymax               = src_1->win_y_max;

        uint32_t ct = 0;
        PPE_DISP_input_layer2_init.Transfer_Matrix_E11             = comp[ct++];
        PPE_DISP_input_layer2_init.Transfer_Matrix_E12             = comp[ct++];
        PPE_DISP_input_layer2_init.Transfer_Matrix_E13             = comp[ct++];
        PPE_DISP_input_layer2_init.Transfer_Matrix_E21             = comp[ct++];
        PPE_DISP_input_layer2_init.Transfer_Matrix_E22             = comp[ct++];
        PPE_DISP_input_layer2_init.Transfer_Matrix_E23             = comp[ct++];

        PPE_DISP_InputLayer_Init(PPE_DISP_INPUT_2, &PPE_DISP_input_layer2_init);
    }
    else
    {
        PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_2, DISABLE);
    }

    if (src_2 != NULL)
    {
        ppe_matrix_t matrix;
//        memcpy(&matrix, &src_2->inv_matrix, sizeof(ppe_matrix_t));

        uint32_t color = ((src_2->opacity << 24) | 0x000000);

        uint32_t comp[9];
        if (!inv_matrix2complement(&matrix, comp))
        {
            return PPE_DISP_ERR_INVALID_MATRIX;
        }

        /*input layer 2 initilization*/
        PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_3, ENABLE);           // logic enable of layer
        PPE_DISP_InputLayer_Init_Typedef PPE_DISP_input_layer2_init;
        HONEYGUI_PPE_DISP_InputLayer_StructInit(PPE_DISP_INPUT_3, &PPE_DISP_input_layer2_init);
        PPE_DISP_input_layer2_init.Layer_Address                   = (uint32_t)src_2->address;
        PPE_DISP_input_layer2_init.Pic_Height                      = (uint32_t)src_2->height;
        PPE_DISP_input_layer2_init.Pic_Width                       = (uint32_t)src_2->width;
        PPE_DISP_input_layer2_init.Line_Length                     = (uint32_t)src_2->stride *
                                                                PPE_DISP_Get_Pixel_Size(src_2->format);
        PPE_DISP_input_layer2_init.Pixel_Source                    = PPE_DISP_LAYER_SRC_FROM_DMA;
        PPE_DISP_input_layer2_init.Pixel_Color_Format              = src_2->format;
        PPE_DISP_input_layer2_init.Const_Pixel                     = color;
        PPE_DISP_input_layer2_init.Color_Key_Mode                  = src_2->color_key_enable;
        if (src_2->high_quality)
        {
            PPE_DISP_input_layer2_init.Read_Matrix_Size                = PPV2_READ_MATRIX_2X2;
        }
        else
        {
            PPE_DISP_input_layer2_init.Read_Matrix_Size                = PPV2_READ_MATRIX_1X1;
        }
        if (src_2->color_key_enable >= PPE_DISP_COLOR_KEY_INSIDE)
        {
            PPE_DISP_input_layer2_init.Color_Key_Min               = src_2->color_key_min;
            PPE_DISP_input_layer2_init.Color_Key_Max               = src_2->color_key_max;
        }
        else
        {
            PPE_DISP_input_layer2_init.Color_Key_Min               = 0x000000;
            PPE_DISP_input_layer2_init.Color_Key_Max               = 0xFFFFFF;
        }

        PPE_DISP_input_layer2_init.MultiFrame_Reload_En            = DISABLE;
        PPE_DISP_input_layer2_init.MultiFrame_LLP_En               = DISABLE;
        PPE_DISP_input_layer2_init.LayerBus_Inc                    = PPE_DISP_AWBURST_INC;
        PPE_DISP_input_layer2_init.Layer_HW_Handshake_En           = PPE_DISP_HW_HS_DISABLE;
        PPE_DISP_input_layer2_init.Layer_HW_Handshake_Index        = PPE_DISP_HS_IDU_Tx;
        PPE_DISP_input_layer2_init.Layer_HW_Handshake_Polarity     = PPE_DISP_HW_HS_ACTIVE_HIGH;
        PPE_DISP_input_layer2_init.Layer_HW_Handshake_MsizeLog     = PPE_DISP_MSIZE_1;
        PPE_DISP_input_layer2_init.Layer_Window_Xmin               = src_2->win_x_min;
        PPE_DISP_input_layer2_init.Layer_Window_Xmax               = src_2->win_x_max;
        PPE_DISP_input_layer2_init.Layer_Window_Ymin               = src_2->win_y_min;
        PPE_DISP_input_layer2_init.Layer_Window_Ymax               = src_2->win_y_max;

        uint32_t ct = 0;
        PPE_DISP_input_layer2_init.Transfer_Matrix_E11             = comp[ct++];
        PPE_DISP_input_layer2_init.Transfer_Matrix_E12             = comp[ct++];
        PPE_DISP_input_layer2_init.Transfer_Matrix_E13             = comp[ct++];
        PPE_DISP_input_layer2_init.Transfer_Matrix_E21             = comp[ct++];
        PPE_DISP_input_layer2_init.Transfer_Matrix_E22             = comp[ct++];
        PPE_DISP_input_layer2_init.Transfer_Matrix_E23             = comp[ct++];

        PPE_DISP_InputLayer_Init(PPE_DISP_INPUT_3, &PPE_DISP_input_layer2_init);
    }
    else
    {
        PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_3, DISABLE);
    }

    if (src_3 != NULL)
    {
        ppe_matrix_t matrix;
//        memcpy(&matrix, &src_3->inv_matrix, sizeof(ppe_matrix_t));

        uint32_t color = ((src_3->opacity << 24) | 0x000000);

        uint32_t comp[9];
        if (!inv_matrix2complement(&matrix, comp))
        {
            return PPE_DISP_ERR_INVALID_MATRIX;
        }

        /*input layer 2 initilization*/
        PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_4, ENABLE);           // logic enable of layer
        PPE_DISP_InputLayer_Init_Typedef PPE_DISP_input_layer2_init;
        HONEYGUI_PPE_DISP_InputLayer_StructInit(PPE_DISP_INPUT_4, &PPE_DISP_input_layer2_init);
        PPE_DISP_input_layer2_init.Layer_Address                   = (uint32_t)src_3->address;
        PPE_DISP_input_layer2_init.Pic_Height                      = (uint32_t)src_3->height;
        PPE_DISP_input_layer2_init.Pic_Width                       = (uint32_t)src_3->width;
        PPE_DISP_input_layer2_init.Line_Length                     = (uint32_t)src_3->stride *
                                                                PPE_DISP_Get_Pixel_Size(src_3->format);
        PPE_DISP_input_layer2_init.Pixel_Source                    = PPE_DISP_LAYER_SRC_FROM_DMA;
        PPE_DISP_input_layer2_init.Pixel_Color_Format              = src_3->format;
        PPE_DISP_input_layer2_init.Const_Pixel                     = color;
        PPE_DISP_input_layer2_init.Color_Key_Mode                  = src_3->color_key_enable;
        if (src_3->high_quality)
        {
            PPE_DISP_input_layer2_init.Read_Matrix_Size                = PPV2_READ_MATRIX_2X2;
        }
        else
        {
            PPE_DISP_input_layer2_init.Read_Matrix_Size                = PPV2_READ_MATRIX_1X1;
        }
        if (src_3->color_key_enable >= PPE_DISP_COLOR_KEY_INSIDE)
        {
            PPE_DISP_input_layer2_init.Color_Key_Min               = src_3->color_key_min;
            PPE_DISP_input_layer2_init.Color_Key_Max               = src_3->color_key_max;
        }
        else
        {
            PPE_DISP_input_layer2_init.Color_Key_Min               = 0x000000;
            PPE_DISP_input_layer2_init.Color_Key_Max               = 0xFFFFFF;
        }

        PPE_DISP_input_layer2_init.MultiFrame_Reload_En            = DISABLE;
        PPE_DISP_input_layer2_init.MultiFrame_LLP_En               = DISABLE;
        PPE_DISP_input_layer2_init.LayerBus_Inc                    = PPE_DISP_AWBURST_INC;
        PPE_DISP_input_layer2_init.Layer_HW_Handshake_En           = PPE_DISP_HW_HS_DISABLE;
        PPE_DISP_input_layer2_init.Layer_HW_Handshake_Index        = PPE_DISP_HS_IDU_Tx;
        PPE_DISP_input_layer2_init.Layer_HW_Handshake_Polarity     = PPE_DISP_HW_HS_ACTIVE_HIGH;
        PPE_DISP_input_layer2_init.Layer_HW_Handshake_MsizeLog     = PPE_DISP_MSIZE_1;
        PPE_DISP_input_layer2_init.Layer_Window_Xmin               = src_3->win_x_min;
        PPE_DISP_input_layer2_init.Layer_Window_Xmax               = src_3->win_x_max;
        PPE_DISP_input_layer2_init.Layer_Window_Ymin               = src_3->win_y_min;
        PPE_DISP_input_layer2_init.Layer_Window_Ymax               = src_3->win_y_max;

        uint32_t ct = 0;
        PPE_DISP_input_layer2_init.Transfer_Matrix_E11             = comp[ct++];
        PPE_DISP_input_layer2_init.Transfer_Matrix_E12             = comp[ct++];
        PPE_DISP_input_layer2_init.Transfer_Matrix_E13             = comp[ct++];
        PPE_DISP_input_layer2_init.Transfer_Matrix_E21             = comp[ct++];
        PPE_DISP_input_layer2_init.Transfer_Matrix_E22             = comp[ct++];
        PPE_DISP_input_layer2_init.Transfer_Matrix_E23             = comp[ct++];

        PPE_DISP_InputLayer_Init(PPE_DISP_INPUT_4, &PPE_DISP_input_layer2_init);
    }
    else
    {
        PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_4, DISABLE);
    }

    if (mode == PPE_DISP_SRC_OVER_MODE)
    {
        /*input layer 1 initilization*/
        PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_1, ENABLE);           // logic enable of layer
        PPE_DISP_InputLayer_Init_Typedef PPE_DISP_input_layer1_init;
        HONEYGUI_PPE_DISP_InputLayer_StructInit(PPE_DISP_INPUT_1, &PPE_DISP_input_layer1_init);
        PPE_DISP_input_layer1_init.Layer_Address                   = dst->address;
        PPE_DISP_input_layer1_init.Pic_Height                      = (uint32_t)dst->height;
        PPE_DISP_input_layer1_init.Pic_Width                       = (uint32_t)dst->width;
        PPE_DISP_input_layer1_init.Line_Length                     = dst->stride * PPE_DISP_Get_Pixel_Size(
                                                                    dst->format);
        PPE_DISP_input_layer1_init.Pixel_Source                    = PPE_DISP_LAYER_SRC_FROM_DMA;
        PPE_DISP_input_layer1_init.Pixel_Color_Format              = dst->format;
        PPE_DISP_input_layer1_init.Const_Pixel                     = 0xFFFFFFFF;
        PPE_DISP_input_layer1_init.MultiFrame_Reload_En            = DISABLE;
        PPE_DISP_input_layer1_init.MultiFrame_LLP_En               = DISABLE;
        PPE_DISP_input_layer1_init.Read_Matrix_Size                = PPV2_READ_MATRIX_1X1;
        PPE_DISP_input_layer1_init.LayerBus_Inc                    = PPE_DISP_AWBURST_INC;
        PPE_DISP_input_layer1_init.Layer_HW_Handshake_En           = PPE_DISP_HW_HS_DISABLE;
        PPE_DISP_input_layer1_init.Layer_HW_Handshake_Index        = PPE_DISP_HS_IDU_Tx;
        PPE_DISP_input_layer1_init.Layer_HW_Handshake_Polarity     = PPE_DISP_HW_HS_ACTIVE_HIGH;
        PPE_DISP_input_layer1_init.Layer_HW_Handshake_MsizeLog     = PPE_DISP_MSIZE_1;
        PPE_DISP_input_layer1_init.Layer_Window_Xmin               = 0;
        PPE_DISP_input_layer1_init.Layer_Window_Xmax               = dst->width;
        PPE_DISP_input_layer1_init.Layer_Window_Ymin               = 0;
        PPE_DISP_input_layer1_init.Layer_Window_Ymax               = dst->height;

        PPE_DISP_input_layer1_init.Transfer_Matrix_E11             = 0x10000;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E12             = 0;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E13             = 0;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E21             = 0;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E22             = 0x10000;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E23             = 0;

        PPE_DISP_InputLayer_Init(PPE_DISP_INPUT_1, &PPE_DISP_input_layer1_init);
    }
    else if (mode == PPE_DISP_TRANSPARENT_MODE)
    {
        PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_1, ENABLE);           // logic enable of layer
        PPE_DISP_InputLayer_Init_Typedef PPE_DISP_input_layer1_init;
        HONEYGUI_PPE_DISP_InputLayer_StructInit(PPE_DISP_INPUT_1, &PPE_DISP_input_layer1_init);
        PPE_DISP_input_layer1_init.Layer_Address                   = (uint32_t)NULL;
        PPE_DISP_input_layer1_init.Pic_Height                      = dst->height;
        PPE_DISP_input_layer1_init.Pic_Width                       = dst->width;
        PPE_DISP_input_layer1_init.Line_Length                     = dst->stride * PPE_DISP_Get_Pixel_Size(
                                                                    dst->format);
        PPE_DISP_input_layer1_init.Pixel_Source                    = PPE_DISP_LAYER_SRC_CONST;
        PPE_DISP_input_layer1_init.Pixel_Color_Format              = dst->format;
        PPE_DISP_input_layer1_init.Const_Pixel                     = 0x0;
        PPE_DISP_input_layer1_init.MultiFrame_Reload_En            = DISABLE;
        PPE_DISP_input_layer1_init.MultiFrame_LLP_En               = DISABLE;
        PPE_DISP_input_layer1_init.Read_Matrix_Size                = PPV2_READ_MATRIX_1X1;
        PPE_DISP_input_layer1_init.LayerBus_Inc                    = PPE_DISP_AWBURST_INC;
        PPE_DISP_input_layer1_init.Layer_HW_Handshake_En           = PPE_DISP_HW_HS_DISABLE;
        PPE_DISP_input_layer1_init.Layer_HW_Handshake_Index        = PPE_DISP_HS_IDU_Tx;
        PPE_DISP_input_layer1_init.Layer_HW_Handshake_Polarity     = PPE_DISP_HW_HS_ACTIVE_HIGH;
        PPE_DISP_input_layer1_init.Layer_HW_Handshake_MsizeLog     = PPE_DISP_MSIZE_1;
        PPE_DISP_input_layer1_init.Layer_Window_Xmin               = 0;
        PPE_DISP_input_layer1_init.Layer_Window_Xmax               = dst->width;
        PPE_DISP_input_layer1_init.Layer_Window_Ymin               = 0;
        PPE_DISP_input_layer1_init.Layer_Window_Ymax               = dst->height;

        PPE_DISP_input_layer1_init.Transfer_Matrix_E11             = 0x10000;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E12             = 0;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E13             = 0;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E21             = 0;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E22             = 0x10000;
        PPE_DISP_input_layer1_init.Transfer_Matrix_E23             = 0;

        PPE_DISP_InputLayer_Init(PPE_DISP_INPUT_1, &PPE_DISP_input_layer1_init);
    }
    else
    {
        PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_1, DISABLE);
    }

    PPE_DISP_ResultLayer_Init_Typedef PPE_DISP_ResultLayer0_Init;
    HONEYGUI_PPE_DISP_ResultLayer_StructInit(&PPE_DISP_ResultLayer0_Init);
    PPE_DISP_ResultLayer0_Init.Layer_Address                   = dst->address;
    PPE_DISP_ResultLayer0_Init.Canvas_Height                   = dst->height;
    PPE_DISP_ResultLayer0_Init.Canvas_Width                    = dst->width;
    PPE_DISP_ResultLayer0_Init.Line_Length                     = dst->stride * PPE_DISP_Get_Pixel_Size(
                                                                dst->format);
    PPE_DISP_ResultLayer0_Init.Color_Format                    = dst->format;
    PPE_DISP_ResultLayer0_Init.BackGround                      = dst->const_color;
    PPE_DISP_ResultLayer0_Init.LayerBus_Inc                    = PPE_DISP_AWBURST_INC;
    PPE_DISP_ResultLayer0_Init.MultiFrame_Reload_En            = DISABLE;
    PPE_DISP_ResultLayer0_Init.MultiFrame_LLP_En               = DISABLE;
    PPE_DISP_ResultLayer_Init(&PPE_DISP_ResultLayer0_Init);

    HONEYGUI_PPE_DISP_Cmd(ENABLE);
    while (((PPE_DISP_REG_GLB_STATUS_TypeDef)PPE_DISP->REG_GLB_STATUS).b.run_state);
    return PPE_DISP_SUCCESS;
}


void ppe_get_identity(ppe_matrix_t *matrix)
{
    /* Set identify matrix. */
    matrix->m[0][0] = 1.0f;
    matrix->m[0][1] = 0.0f;
    matrix->m[0][2] = 0.0f;
    matrix->m[1][0] = 0.0f;
    matrix->m[1][1] = 1.0f;
    matrix->m[1][2] = 0.0f;
    matrix->m[2][0] = 0.0f;
    matrix->m[2][1] = 0.0f;
    matrix->m[2][2] = 1.0f;
}

bool ppe_is_identity_or_translate(ppe_matrix_t *matrix)
{
    return (matrix->m[0][0] == 1.0f && \
            matrix->m[0][1] == 0.0f && \
            matrix->m[1][0] == 0.0f && \
            matrix->m[1][1] == 1.0f && \
            matrix->m[2][0] == 0.0f && \
            matrix->m[2][1] == 0.0f && \
            matrix->m[2][2] == 1.0f);
}

#if USE_PPE_DISP_MAT
static const int16_t sin_table[] =
{
    0,     572,   1144,  1715,  2286,  2856,  3425,  3993,  4560,  5126,  5690,  6252,  6813,  7371,  7927,  8481,
    9032,  9580,  10126, 10668, 11207, 11743, 12275, 12803, 13328, 13848, 14364, 14876, 15383, 15886, 16383, 16876,
    17364, 17846, 18323, 18794, 19260, 19720, 20173, 20621, 21062, 21497, 21925, 22347, 22762, 23170, 23571, 23964,
    24351, 24730, 25101, 25465, 25821, 26169, 26509, 26841, 27165, 27481, 27788, 28087, 28377, 28659, 28932, 29196,
    29451, 29697, 29934, 30162, 30381, 30591, 30791, 30982, 31163, 31335, 31498, 31650, 31794, 31927, 32051, 32165,
    32269, 32364, 32448, 32523, 32587, 32642, 32687, 32722, 32747, 32762, 32767
};

static int16_t ppe_fix_sin(int16_t angle)
{
    int16_t ret = 0;
    angle       = angle % 360;

    if (angle < 0) { angle = 360 + angle; }

    if (angle < 90)
    {
        ret = sin_table[angle];
    }
    else if (angle >= 90 && angle < 180)
    {
        angle = 180 - angle;
        ret   = sin_table[angle];
    }
    else if (angle >= 180 && angle < 270)
    {
        angle = angle - 180;
        ret   = -sin_table[angle];
    }
    else     /*angle >=270*/
    {
        angle = 360 - angle;
        ret   = -sin_table[angle];
    }

    return ret;
}

static inline int16_t ppe_fix_cos(int16_t angle)
{
    return ppe_fix_sin(angle + 90);
}


void ppe_mat_multiply(ppe_matrix_t *matrix, ppe_matrix_t *mult)
{
    float m00, m01, m02, m10, m11, m12, m20, m21, m22;
    m00 = matrix->m[0][0];
    m01 = matrix->m[0][1];
    m02 = matrix->m[0][2];

    m10 = matrix->m[1][0];
    m11 = matrix->m[1][1];
    m12 = matrix->m[1][2];

    m20 = matrix->m[2][0];
    m21 = matrix->m[2][1];
    m22 = matrix->m[2][2];

    float t00, t01, t02, t10, t11, t12, t20, t21, t22;
    t00 = mult->m[0][0];
    t01 = mult->m[0][1];
    t02 = mult->m[0][2];

    t10 = mult->m[1][0];
    t11 = mult->m[1][1];
    t12 = mult->m[1][2];

    t20 = mult->m[2][0];
    t21 = mult->m[2][1];
    t22 = mult->m[2][2];
    /* Compute matrix entry. */
    //row = 0;
    matrix->m[0][0] = (m00 * t00) + (m01 * t10) + (m02 * t20);
    matrix->m[0][1] = (m00 * t01) + (m01 * t11) + (m02 * t21);
    matrix->m[0][2] = (m00 * t02) + (m01 * t12) + (m02 * t22);
    //row = 1;
    matrix->m[1][0] = (m10 * t00) + (m11 * t10) + (m12 * t20);
    matrix->m[1][1] = (m10 * t01) + (m11 * t11) + (m12 * t21);
    matrix->m[1][2] = (m10 * t02) + (m11 * t12) + (m12 * t22);
    ///row = 2;
    matrix->m[2][0] = (m20 * t00) + (m21 * t10) + (m22 * t20);
    matrix->m[2][1] = (m20 * t01) + (m21 * t11) + (m22 * t21);
    matrix->m[2][2] = (m20 * t02) + (m21 * t12) + (m22 * t22);
}

void ppe_translate(float x, float y, ppe_matrix_t *matrix)
{
    /* Set translation matrix. */
    ppe_matrix_t t = { { {1.0f, 0.0f, x},
            {0.0f, 1.0f, y},
            {0.0f, 0.0f, 1.0f}
        }
    };

    /* Multiply with current matrix. */
    ppe_mat_multiply(matrix, &t);
}

void ppe_scale(float scale_x, float scale_y, ppe_matrix_t *matrix)
{
    /* Set scale matrix. */
    ppe_matrix_t s = { { {scale_x, 0.0f, 0.0f},
            {0.0f, scale_y, 0.0f},
            {0.0f, 0.0f, 1.0f}
        }
    };

    /* Multiply with current matrix. */
    ppe_mat_multiply(matrix, &s);
}

#ifndef M_PI
#define M_PI 3.1415926535898f
#endif
void ppe_rotate(float degrees, ppe_matrix_t *matrix)
{
    /* Convert degrees into radians. */
    int16_t angle = (int)degrees;

    /* Compuet cosine and sine values. */
    float cos_angle = ppe_fix_cos(angle) / 32767.0f;
    float sin_angle = ppe_fix_sin(angle) / 32767.0f;

    /* Set rotation matrix. */
    ppe_matrix_t r = { { {cos_angle, -sin_angle, 0.0f},
            {sin_angle, cos_angle, 0.0f},
            {0.0f, 0.0f, 1.0f}
        }
    };

    /* Multiply with current matrix. */
    ppe_mat_multiply(matrix, &r);
}

void ppe_skew(float skew_x, float skew_y, ppe_matrix_t *matrix)
{
    float rskew_x = skew_x / 180.0f * (float)M_PI;
    float rskew_y = skew_y / 180.0f * (float)M_PI;
    float tan_x = tanf(rskew_x);
    float tan_y = tanf(rskew_y);

    ppe_matrix_t s = {{
            {1.0f, tan_x, 0.0f},
            {tan_y, 1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f},
        }
    };

    ppe_mat_multiply(matrix, &s);
}

void ppe_reflect(bool horizontal, bool vertical, ppe_matrix_t *matrix)
{
    /* set prespective matrix */
    float f1 = 1, f2 = 1;
    bool change = false;
    if (horizontal)
    {
        f1 = -1.0f;
        change = true;
    }
    if (vertical)
    {
        f2 = -1.0f;
        change = true;
    }
    if (change)
    {
        ppe_matrix_t p = { { {f1, 0.0f, 0.0f},
                {0.0f, f2, 0.0f},
                {0, 0, 1.0f}
            }
        };
        /* Multiply with current matrix. */
        ppe_mat_multiply(matrix, &p);
    }
}

void ppe_perspective(float px, float py, ppe_matrix_t *matrix)
{
    /* set prespective matrix */
    ppe_matrix_t p = { { {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {px, py, 1.0f}
        }
    };
    /* Multiply with current matrix. */
    ppe_mat_multiply(matrix, &p);
}

static int swap(float *a, float *b)
{
    float temp;
    if (a == NULL || b == NULL)
    {
        return -1;
    }
    temp = *a;
    *a = *b;
    *b = temp;
    return 0;
}

int ppe_get_transform_matrix(ppe_point4_t src, ppe_point4_t dst,
                             ppe_matrix_t *mat)
{
    float a[8][8], b[9], A[64];
    int i, j, k, m = 8, n = 1;
    int astep = 8, bstep = 1;
    float d;

    if (src == NULL || dst == NULL || mat == NULL)
    {
        return  -1;
    }

    for (i = 0; i < 4; ++i)
    {
        a[i][0] = a[i + 4][3] = src[i].x;
        a[i][1] = a[i + 4][4] = src[i].y;
        a[i][2] = a[i + 4][5] = 1;
        a[i][3] = a[i][4] = a[i][5] =
                                a[i + 4][0] = a[i + 4][1] = a[i + 4][2] = 0;
        a[i][6] = -src[i].x * dst[i].x;
        a[i][7] = -src[i].y * dst[i].x;
        a[i + 4][6] = -src[i].x * dst[i].y;
        a[i + 4][7] = -src[i].y * dst[i].y;
        b[i] = dst[i].x;
        b[i + 4] = dst[i].y;
    }
    for (i = 0; i < 8; ++i)
    {
        for (j = 0; j < 8; ++j)
        {
            A[8 * i + j] = a[i][j];
        }
    }

    for (i = 0; i < m; i++)
    {
        k = i;
        for (j = i + 1; j < m; j++)
            if (ABS(A[j * astep + i]) > ABS(A[k * astep + i]))
            {
                k = j;
            }
        if (ABS(A[k * astep + i]) < EPS)
        {
            return -1;
        }
        if (k != i)
        {
            for (j = i; j < m; j++)
            {
                swap(&A[i * astep + j], &A[k * astep + j]);
            }
            for (j = 0; j < n; j++)
            {
                swap(&b[i * bstep + j], &b[k * bstep + j]);
            }
        }
        d = -1 / A[i * astep + i];
        for (j = i + 1; j < m; j++)
        {
            float alpha = A[j * astep + i] * d;
            for (k = i + 1; k < m; k++)
            {
                A[j * astep + k] += alpha * A[i * astep + k];
            }
            for (k = 0; k < n; k++)
            {
                b[j * bstep + k] += alpha * b[i * bstep + k];
            }
        }
    }

    for (i = m - 1; i >= 0; i--)
        for (j = 0; j < n; j++)
        {
            float s = b[i * bstep + j];
            for (k = i + 1; k < m; k++)
            {
                s -= A[i * astep + k] * b[k * bstep + j];
            }
            b[i * bstep + j] = s / A[i * astep + i];
        }

    b[8] = 1;

    for (i = 0; i < 3; ++i)
    {
        for (j = 0; j < 3; ++j)
        {
            mat->m[i][j] = b[i * 3 + j];
        }
    }
    return 0;
}

void ppe_matrix_inverse(ppe_matrix_t *matrix)
{
    float m00, m01, m02, m10, m11, m12, m20, m21, m22;
    m00 = matrix->m[0][0];
    m01 = matrix->m[0][1];
    m02 = matrix->m[0][2];

    m10 = matrix->m[1][0];
    m11 = matrix->m[1][1];
    m12 = matrix->m[1][2];

    m20 = matrix->m[2][0];
    m21 = matrix->m[2][1];
    m22 = matrix->m[2][2];

    float detal = m00 * m11 * m22 + \
                  m01 * m12 * m20 + \
                  m02 * m10 * m21 - \
                  m00 * m12 * m21 - \
                  m01 * m10 * m22 - \
                  m02 * m11 * m20;

    matrix->m[0][0] = (m11 * m22 - m12 * m21) / detal;
    matrix->m[1][0] = -(m22 * m10 - m12 * m20) / detal;
    matrix->m[2][0] = (m10 * m21 - m20 * m11) / detal;

    matrix->m[0][1] = -(m01 * m22 - m02 * m21) / detal;
    matrix->m[1][1] = (m22 * m00 - m20 * m02) / detal;
    matrix->m[2][1] = -(m00 * m21 - m20 * m01) / detal;

    matrix->m[0][2] = (m01 * m12 - m11 * m02) / detal;
    matrix->m[1][2] = -(m00 * m12 - m02 * m10) / detal;
    matrix->m[2][2] = (m00 * m11 - m01 * m10) / detal;
}
#endif


bool inv_matrix2complement(ppe_matrix_t *matrix, uint32_t *comp)
{
    float *tf = (float *)matrix->m;
    int16_t tr[9];
    uint16_t* p = (uint16_t*)tr;
    for (int i = 0; i < 9; i++)
    {
        tr[i] = tf[i] * 16;
        comp[i] = p[i];
    }
    //memcpy(comp, tr, 9 * sizeof(int32_t));
    return true;
}

static void pos_transfer(ppe_matrix_t *matrix, ppe_pox_t *pox)
{
    float m_row0, m_row1, m_row2;

    float a = pox->p[0];
    float b = pox->p[1];
    float c = pox->p[2];

    /* Process all rows. */
    m_row0 = matrix->m[0][0];
    m_row1 = matrix->m[0][1];
    m_row2 = matrix->m[0][2];
    pox->p[0] = (m_row0 * a) + (m_row1 * b) + (m_row2 * c);

    m_row0 = matrix->m[1][0];
    m_row1 = matrix->m[1][1];
    m_row2 = matrix->m[1][2];
    pox->p[1] = (m_row0 * a) + (m_row1 * b) + (m_row2 * c);

    m_row0 = matrix->m[2][0];
    m_row1 = matrix->m[2][1];
    m_row2 = matrix->m[2][2];
    pox->p[2] = (m_row0 * a) + (m_row1 * b) + (m_row2 * c);

    pox->p[0] = pox->p[0] / pox->p[2];
    pox->p[1] = pox->p[1] / pox->p[2];
    pox->p[2] = 1;
}

bool ppe_get_area(ppe_rect_t *result_rect, ppe_rect_t *source_rect, ppe_matrix_t *matrix,
                  ppe_buffer_t *buffer)
{
    ppe_pox_t pox = {};
    float x_min = 0.0f;
    float x_max = 0.0f;
    float y_min = 0.0f;
    float y_max = 0.0f;

    pox.p[0] = source_rect->x * 1.0f;
    pox.p[1] = source_rect->y * 1.0f;
    pox.p[2] = 1.0f;
    pos_transfer(matrix, &pox);
    x_min = pox.p[0];
    x_max = pox.p[0];
    y_min = pox.p[1];
    y_max = pox.p[1];

    pox.p[0] = (source_rect->x + source_rect->w - 1) * 1.0f;
    pox.p[1] = source_rect->y * 1.0f;
    pox.p[2] = 1.0f;
    pos_transfer(matrix, &pox);
    if (x_min > pox.p[0])
    {
        x_min = pox.p[0];
    }
    if (x_max < pox.p[0])
    {
        x_max = pox.p[0];
    }
    if (y_min > pox.p[1])
    {
        y_min = pox.p[1];
    }
    if (y_max < pox.p[1])
    {
        y_max = pox.p[1];
    }

    pox.p[0] = source_rect->x * 1.0f;
    pox.p[1] = (source_rect->y + source_rect->h - 1) * 1.0f;
    pox.p[2] = 1.0f;
    pos_transfer(matrix, &pox);
    if (x_min > pox.p[0])
    {
        x_min = pox.p[0];
    }
    if (x_max < pox.p[0])
    {
        x_max = pox.p[0];
    }
    if (y_min > pox.p[1])
    {
        y_min = pox.p[1];
    }
    if (y_max < pox.p[1])
    {
        y_max = pox.p[1];
    }

    pox.p[0] = (source_rect->x + source_rect->w - 1) * 1.0f;
    pox.p[1] = (source_rect->y + source_rect->h - 1) * 1.0f;
    pox.p[2] = 1.0f;
    pos_transfer(matrix, &pox);
    if (x_min > pox.p[0])
    {
        x_min = pox.p[0];
    }
    if (x_max < pox.p[0])
    {
        x_max = pox.p[0];
    }
    if (y_min > pox.p[1])
    {
        y_min = pox.p[1];
    }
    if (y_max < pox.p[1])
    {
        y_max = pox.p[1];
    }

    if (x_max <= -1 || x_min >= buffer->width * 1.0f || y_max <= -1 || y_min >= buffer->height * 1.0f)
    {
        result_rect->x = 0;
        result_rect->y = 0;
        result_rect->w = 0;
        result_rect->h = 0;
        return false;
    }
    if (x_min < 0)
    {
        x_min = 0;
    }
    else if (x_min > buffer->width - 1)
    {
        x_min = buffer->width - 1;
    }
    if (y_min < 0)
    {
        y_min = 0;
    }
    else if (y_min >= buffer->height)
    {
        y_min = buffer->height - 1;
    }
    result_rect->x = (int16_t)x_min;
    result_rect->y = (int16_t)y_min;

    if (x_max >= buffer->width)
    {
        x_max = buffer->width - 1;
    }
    else if (x_max < 0)
    {
        x_max = 0;
    }

    if (y_max >= buffer->height)
    {
        y_max = buffer->height - 1;
    }
    else if (y_max < 0)
    {
        y_max = 0;
    }

    result_rect->w = ceil(x_max) - result_rect->x + 1;
    result_rect->h = ceil(y_max) - result_rect->y + 1;

    if (result_rect->x + result_rect->w > buffer->width)
    {
        result_rect->w = buffer->width - result_rect->x;
    }
    if (result_rect->y + result_rect->h > buffer->height)
    {
        result_rect->h = buffer->height - result_rect->y;
    }
    if (isnan(y_min) || isnan(y_max) || isnan(x_min) || isnan(x_max) || result_rect->h == 0 ||
        result_rect->w == 0)
    {
        return false;
    }
    return true;
}

bool ppe_matrix_is_complex(ppe_matrix_t *mat)
{
    if (mat->m[0][0] == 1 && mat->m[1][1] == 1 && \
        mat->m[0][1] == 0 && mat->m[1][0] == 0 && \
        mat->m[2][0] == 0 && mat->m[2][1] == 0 && \
        mat->m[2][2] == 1)
    {
        return false;
    }
    return true;
}
/******************* (C) COPYRIGHT 2023 Realtek Semiconductor Corporation *****END OF FILE****/
