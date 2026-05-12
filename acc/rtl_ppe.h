/**
*********************************************************************************************************
*               Copyright(c) 2023, Realtek Semiconductor Corporation. All rights reserved.
*********************************************************************************************************
* \file     rtl_ppe.h
* \brief    This file provides all the PPE_DISP firmware functions.
* \details
* \author   astor zhang
* \date     2025-08-05
* \version  v1.0
*********************************************************************************************************
*/

/*============================================================================*
 *               Define to prevent recursive inclusion
 *============================================================================*/
#ifndef RTL_PPE_DISP_H
#define RTL_PPE_DISP_H

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================*
 *                        Header Files
 *============================================================================*/
#include "rtl_ppe_reg.h"

/*============================================================================*
 *                         PPE_DISP Registers Memory Map
 *============================================================================*/
typedef struct
{
    __IO uint32_t REG_GLB_STATUS;                       /* offset 0x00*/
    __IO uint32_t REG_LYR_ENABLE;                       /* offset 0x04*/
    __IO uint32_t REG_LD_CFG;                           /* offset 0x08*/
    __IO uint32_t REG_LL_CFG;                           /* offset 0x0C*/
    __IO uint32_t REG_LLP;                              /* offset 0x10*/
    __I  uint32_t REG_RESERVED1;                        /* offset 0x14*/
    __IO uint32_t REG_SECURE;                           /* offset 0x18*/
    __IO uint32_t REG_LINE_OVER_INDEX;                  /* offset 0x1C*/
    __I  uint32_t REG_RELEASE_DATE;                     /* offset 0x20*/
    __I  uint32_t REG_RTL_VER;                          /* offset 0x24*/
    __I  uint32_t REG_RESERVED2[6];                     /* offset 0x28-3C*/
    __I  uint32_t REG_INTR_STATUS;                      /* offset 0x40*/
    __I  uint32_t REG_INTR_RAW;                         /* offset 0x44*/
    __O  uint32_t REG_INTR_CLR;                         /* offset 0x48*/
    __IO uint32_t REG_INTR_MASK;                        /* offset 0x4C*/
    __I  uint32_t REG_BUS_ERR_DETAIL;                   /* offset 0x50*/
    __I  uint32_t REG_RESERVED3;                     /* offset 0x54*/
    __IO uint32_t PPE_DISP_XOR_CTRL                      ;  /*!< XOR CONTROL REGISTER,  Address offset:0x058 */
    __I  uint32_t REG_RESERVED4;                     /* offset 0x5C*/
} PPE_DISP_Typedef;

typedef struct
{
    __IO uint32_t REG_LYR0_ADDR;                /* offset 0x60*/
    __I  uint32_t REG_RESERVED1;                /* offset 0x64*/
    __IO uint32_t REG_CANVAS_SIZE;              /* offset 0x68*/
    __IO uint32_t REG_LYR0_PIC_CFG;             /* offset 0x6C*/
    __IO uint32_t REG_BACKGROUND;               /* offset 0x70*/
    __IO uint32_t REG_LYR0_BUS_CFG;             /* offset 0x74*/
    __IO uint32_t REG_LYR0_HS_CFG;              /* offset 0x78*/
    __IO uint32_t REG_BLOCK_SIZE;               /* offset 0x7C*/
} PPE_DISP_ResultLayer_Typedef;


typedef struct
{
    __IO uint32_t REG_LYRx_ADDR;                                /* offset 0x00 + X * 0x80*/
    __IO uint32_t REG_RESERVED1;                                /* offset 0x04 + X * 0x80*/
    __IO uint32_t REG_LYRx_PIC_SIZE;                            /* offset 0x08 + X * 0x80*/
    __IO uint32_t REG_LYRx_PIC_CFG;                             /* offset 0x0C + X * 0x80*/
    __IO uint32_t REG_LYRx_FIXED_COLOR;                         /* offset 0x10 + X * 0x80*/
    __IO uint32_t REG_LYRx_BUS_CFG;                             /* offset 0x14 + X * 0x80*/
    __IO uint32_t REG_LYRx_HS_CFG;                              /* offset 0x18 + X * 0x80*/
    __IO uint32_t REG_LYRx_WIN_MIN;                             /* offset 0x1C + X * 0x80*/
    __IO uint32_t REG_LYRx_WIN_MAX;                             /* offset 0x20 + X * 0x80*/
    __IO uint32_t REG_LYRx_KEY_MIN;                             /* offset 0x24 + X * 0x80*/
    __IO uint32_t REG_LYRx_KEY_MAX;                             /* offset 0x28 + X * 0x80*/
    __IO uint32_t REG_LYRx_TRANS_MATRIX_E11;                    /* offset 0x2C + X * 0x80*/
    __IO uint32_t REG_LYRx_TRANS_MATRIX_E12;                    /* offset 0x30 + X * 0x80*/
    __IO uint32_t REG_LYRx_TRANS_MATRIX_E13;                    /* offset 0x34 + X * 0x80*/
    __IO uint32_t REG_LYRx_TRANS_MATRIX_E21;                    /* offset 0x38 + X * 0x80*/
    __IO uint32_t REG_LYRx_TRANS_MATRIX_E22;                    /* offset 0x3C + X * 0x80*/
    __IO uint32_t REG_LYRx_TRANS_MATRIX_E23;                    /* offset 0x40 + X * 0x80*/
    __I uint32_t REG_LYRx_TRANS_MATRIX_E31;                    /* offset 0x44 + X * 0x80*/
    __I uint32_t REG_LYRx_TRANS_MATRIX_E32;                    /* offset 0x48 + X * 0x80*/
    __I uint32_t REG_LYRx_TRANS_MATRIX_E33;                    /* offset 0x4C + X * 0x80*/
    __IO uint32_t REG_RESERVED2[12];                            /* offset (0x50-0x7C) + X * 0x80*/
} PPE_DISP_Input_Layer_Typedef;

/*============================================================================*
 *                         PPE_DISP Declaration
 *============================================================================*/
#define PPE_DISP_REG_BASE                              0x40126000UL
#define PPE_DISP_Result_Layer_BASE                     (PPE_DISP_REG_BASE + 0x60)
#define PPE_DISP_Input_Layer1_BASE                     (PPE_DISP_REG_BASE + 1 * 0x80)
#define PPE_DISP_Input_Layer2_BASE                     (PPE_DISP_REG_BASE + 2 * 0x80)
#define PPE_DISP_Input_Layer3_BASE                     (PPE_DISP_REG_BASE + 3 * 0x80)
#define PPE_DISP_Input_Layer4_BASE                     (PPE_DISP_REG_BASE + 4 * 0x80)

#define PPE_DISP                                           ((PPE_DISP_Typedef *)PPE_DISP_REG_BASE)
#define PPE_DISP_ResultLayer                               ((PPE_DISP_ResultLayer_Typedef *)PPE_DISP_Result_Layer_BASE)
#define PPE_DISP_InputLayer1                               ((PPE_DISP_Input_Layer_Typedef *)PPE_DISP_Input_Layer1_BASE)
#define PPE_DISP_InputLayer2                               ((PPE_DISP_Input_Layer_Typedef *)PPE_DISP_Input_Layer2_BASE)
#define PPE_DISP_InputLayer3                               ((PPE_DISP_Input_Layer_Typedef *)PPE_DISP_Input_Layer3_BASE)
#define PPE_DISP_InputLayer4                               ((PPE_DISP_Input_Layer_Typedef *)PPE_DISP_Input_Layer4_BASE)

#define PPE_DISP_MAX_INPUTLAYER                          0x4
#define PPE_DISP_LINKLIST_GLB_REG_NUM                    3UL
#define PPE_DISP_LINKLIST_RESULT_REG_NUM                 4UL
#define PPE_DISP_LINKLIST_INPUT_REG_NUM                  17UL
#define PPE_DISP_LLI_MAX                                 (PPE_DISP_LINKLIST_GLB_REG_NUM + PPE_DISP_LINKLIST_RESULT_REG_NUM + PPE_DISP_LINKLIST_INPUT_REG_NUM * PPE_DISP_MAX_INPUTLAYER)


/** \defgroup 8773E_PPE_DISP       PPE_DISP
  * \brief
  * \{
  */

/*============================================================================*
 *                         Constants
 *============================================================================*/
/** \defgroup PPE_DISP_Exported_Constants PPE_DISP Exported Constants
  * \brief
  * \{
  */

/**
 * \cond        private
 * \brief       Constants for internal use
 * \defgroup    PPE_DISP_INTERNAL_USE   PPE_DISP constants for internal use
 * \{
 */
typedef enum
{
  DISABLE = 0,
  ENABLE = 1,
}FunctionalState;

typedef enum
{
    PPE_DISP_INPUT_1 = 0x1,
    PPE_DISP_INPUT_2 = 0x2,
    PPE_DISP_INPUT_3 = 0x3,
    PPE_DISP_INPUT_4 = 0x4,
} PPE_DISP_INPUT_LAYER_INDEX;

typedef enum
{
    PPE_DISP_NON_SECURE_MODE = 0X0,
    PPE_DISP_SECURE_MODE         = 0x1,
} PPE_DISP_SECURE;

typedef enum
{
    PPE_DISP_AWBURST_FIXED     = 0X0,
    PPE_DISP_AWBURST_INC         = 0X1,
} PPE_DISP_AWBURST;

typedef enum
{
    PPE_DISP_HW_HS_DISABLE     = 0X0,
    PPE_DISP_HW_HS_ENABLE         = 0X1,
} PPE_DISP_HW_HS;

typedef enum
{
    PPE_DISP_HW_HS_ACTIVE_HIGH     = 0X0,
    PPE_DISP_HW_HS_ACTIVE_LOW         = 0X1,
} PPE_DISP_HW_HS_POL;

typedef enum
{
    PPE_DISP_LAYER_SRC_CONST             = 0X0,
    PPE_DISP_LAYER_SRC_FROM_DMA         = 0X1,
} PPE_DISP_PIXEL_SOURCE;

typedef enum
{
    PPE_DISP_DMA_HW_HANDSHAKE,
    PPE_DISP_DMA_SW_HANDSHAKE,
} PPE_DISP_DMA_HANDSHAKE;

typedef enum
{
    PPE_DISP_MSIZE_1,
    PPE_DISP_MSIZE_2,
    PPE_DISP_MSIZE_4,
    PPE_DISP_MSIZE_8,
    PPE_DISP_MSIZE_16,
    PPE_DISP_MSIZE_32,
    PPE_DISP_MSIZE_64,
    PPE_DISP_MSIZE_128,
    PPE_DISP_MSIZE_256,
    PPE_DISP_MSIZE_512,
    PPE_DISP_MSIZE_1024,
} PPE_DISP_MSIZE_LOG;

typedef enum
{
    PPE_DISP_HS_IDU_Tx,
} PPE_DISP_HW_HANDSHAKE_INDEX;

typedef enum
{
    PPE_DISP_MAX_AXLEN_0,
    PPE_DISP_MAX_AXLEN_1,
    PPE_DISP_MAX_AXLEN_3,
    PPE_DISP_MAX_AXLEN_7,
    PPE_DISP_MAX_AXLEN_15,
    PPE_DISP_MAX_AXLEN_31,
    PPE_DISP_MAX_AXLEN_63,
    PPE_DISP_MAX_AXLEN_127,
} PPE_DISP_MAX_AXLEN;

typedef enum
{
    PPV2_READ_MATRIX_1X1,
    PPV2_READ_MATRIX_2X2,
} PPE_DISP_READ_MATRIX_SIZE;

typedef enum
{
    PPE_DISP_ALL_OVER_INT = 0x0,
    PPE_DISP_FRAME_OVER_INT,
    PPE_DISP_LOAD_OVER_INT,
    PPE_DISP_LINE_OVER_INT,
    PPE_DISP_SUSPEND_IACTIVE_INT,
    PPE_DISP_SECURE_ERROR_INT,
    PPE_DISP_BUS_ERROR_INT = 0x7,
    PPE_DISP_DIV0_ERR_INT,
} PPE_DISP_INTERRUPT;


typedef enum
{
    PPE_DISP_DISABLE = 0x0,
    PPE_DISP_ENABLE     = 0x1,
    PPE_DISP_SUSPEND_ALL_INA = 0x2,
    PPE_DISP_SUSPEND = 0x3,
} PPE_DISP_RUN_STATE;


typedef struct
{
    uint32_t LYR_ENABLE;
    uint32_t LL_CFG;
    uint32_t LLP;
} PPE_DISP_LLI_GLB;

typedef struct
{
    uint32_t LYR0_ADDR;
    uint32_t CANVAS_SIZE;
    uint32_t LYR0_PIC_CFG;
    uint32_t BACKGROUND;
} PPE_DISP_LLI_RESULT_LAYER;
/**
 * \}
 * \endcond
 */

/** \defgroup PPE_DISP_COLOR_KEY_MODE PPE_DISP Color Key Mode
 * \{
 * \ingroup  PPE_DISP_Exported_Constants
 */
typedef enum
{
    PPE_DISP_COLOR_KEY_DISABLE     = 0X0,        /*!< Color key disabled. */
    PPE_DISP_COLOR_KEY_INSIDE         = 0X2,     /*!< Color key inside mode, minimum <= key_value <= maximum. */
    PPE_DISP_COLOR_KEY_OUTSIDE   = 0X3,            /*!< Color key outside mode, key_value <= minimum or key_value > maximum. */
} PPE_DISP_COLOR_KEY_MODE;
/** End of PPE_DISP_COLOR_KEY_MODE
  * \}
  */

/** \defgroup PPE_DISP_PIXEL_FORMAT PPE_DISP Pixel Format
 * \{
 * \ingroup  PPE_DISP_Exported_Constants
 */
typedef enum
{
    PPE_DISP_ABGR8888 = 0x0,     /*!< ABGR8888: A(bit 31:24) B(bit 23:16) G(bit 15:8) R(bit 7:0) */
    PPE_DISP_ARGB8888,           /*!< ARGB8888: A(bit 31:24) R(bit 23:16) G(bit 15:8) B(bit 7:0) */
    PPE_DISP_XBGR8888,           /*!< XBGR8888: X(bit 31:24) B(bit 23:16) G(bit 15:8) R(bit 7:0) */
    PPE_DISP_XRGB8888,           /*!< XRGB8888: X(bit 31:24) R(bit 23:16) G(bit 15:8) B(bit 7:0) */
    PPE_DISP_BGRA8888,           /*!< BGRA8888: B(bit 31:24) G(bit 23:16) R(bit 15:8) A(bit 7:0) */
    PPE_DISP_RGBA8888,           /*!< RGBA8888: R(bit 31:24) G(bit 23:16) B(bit 15:8) A(bit 7:0) */
    PPE_DISP_BGRX8888,           /*!< BGRX8888: B(bit 31:24) G(bit 23:16) R(bit 15:8) X(bit 7:0) */
    PPE_DISP_RGBX8888,           /*!< RGBX8888: R(bit 31:24) G(bit 23:16) B(bit 15:8) X(bit 7:0) */
    PPE_DISP_ABGR4444,           /*!< ABGR4444: A(bit 15:12) B(bit 11:8) G(bit 7:4) R(bit 3:0) */
    PPE_DISP_ARGB4444,           /*!< ARGB4444: A(bit 15:12) R(bit 11:8) G(bit 7:4) B(bit 3:0) */
    PPE_DISP_XBGR4444,           /*!< XBGR4444: X(bit 15:12) B(bit 11:8) G(bit 7:4) R(bit 3:0) */
    PPE_DISP_XRGB4444,           /*!< XRGB4444: X(bit 15:12) R(bit 11:8) G(bit 7:4) B(bit 3:0) */
    PPE_DISP_BGRA4444,           /*!< BGRA4444: B(bit 15:12) G(bit 11:8) R(bit 7:4) A(bit 3:0) */
    PPE_DISP_RGBA4444,           /*!< RGBA4444: R(bit 15:12) G(bit 11:8) B(bit 7:4) A(bit 3:0) */
    PPE_DISP_BGRX4444,           /*!< BGRX4444: B(bit 15:12) G(bit 11:8) R(bit 7:4) X(bit 3:0) */
    PPE_DISP_RGBX4444,           /*!< RGBX4444: R(bit 15:12) G(bit 11:8) B(bit 7:4) X(bit 3:0) */
    PPE_DISP_ABGR2222,           /*!< ABGR2222: A(bit 7:6) B(bit 5:4) G(bit 3:2) R(bit 1:0) */
    PPE_DISP_ARGB2222,           /*!< ARGB2222: A(bit 7:6) R(bit 5:4) G(bit 3:2) B(bit 1:0) */
    PPE_DISP_XBGR2222,           /*!< XBGR2222: X(bit 7:6) B(bit 5:4) G(bit 3:2) R(bit 1:0) */
    PPE_DISP_XRGB2222,           /*!< XRGB2222: X(bit 7:6) R(bit 5:4) G(bit 3:2) B(bit 1:0) */
    PPE_DISP_BGRA2222,           /*!< BGRA2222: B(bit 7:6) G(bit 5:4) R(bit 3:2) A(bit 1:0) */
    PPE_DISP_RGBA2222,           /*!< RGBA2222: R(bit 7:6) G(bit 5:4) B(bit 3:2) A(bit 1:0) */
    PPE_DISP_BGRX2222,           /*!< BGRX2222: B(bit 7:6) G(bit 5:4) R(bit 3:2) X(bit 1:0) */
    PPE_DISP_RGBX2222,           /*!< RGBX2222: R(bit 7:6) G(bit 5:4) B(bit 3:2) X(bit 1:0) */
    PPE_DISP_ABGR8565,           /*!< ABGR8565: A(bit 23:16) B(bit 15:11) G(bit 10:5) R(bit 4:0) */
    PPE_DISP_ARGB8565,           /*!< ARGB8565: A(bit 23:16) R(bit 15:11) G(bit 10:5) B(bit 4:0) */
    PPE_DISP_XBGR8565,           /*!< XBGR8565: X(bit 23:16) B(bit 15:11) G(bit 10:5) R(bit 4:0) */
    PPE_DISP_XRGB8565,           /*!< XRGB8565: X(bit 23:16) R(bit 15:11) G(bit 10:5) B(bit 4:0) */
    PPE_DISP_BGRA5658,           /*!< BGRA5658: B(bit 23:19) G(bit 18:13) R(bit 12:8) A(bit 7:0) */
    PPE_DISP_RGBA5658,           /*!< RGBA5658: R(bit 23:19) G(bit 18:13) B(bit 12:8) A(bit 7:0) */
    PPE_DISP_BGRX5658,           /*!< BGRX5658: B(bit 23:19) G(bit 18:13) R(bit 12:8) X(bit 7:0) */
    PPE_DISP_RGBX5658,           /*!< RGBX5658: R(bit 23:19) G(bit 18:13) B(bit 12:8) X(bit 7:0) */
    PPE_DISP_ABGR1555,           /*!< ABGR1555: A(bit 15) B(bit 14:10) G(bit 9:5) R(bit 4:0) */
    PPE_DISP_ARGB1555,           /*!< ARGB1555: A(bit 15) R(bit 14:10) G(bit 9:5) B(bit 4:0) */
    PPE_DISP_XBGR1555,           /*!< XBGR1555: X(bit 15) B(bit 14:10) G(bit 9:5) R(bit 4:0) */
    PPE_DISP_XRGB1555,           /*!< XRGB1555: X(bit 15) R(bit 14:10) G(bit 9:5) B(bit 4:0) */
    PPE_DISP_BGRA5551,           /*!< BGRA5551: B(bit 15:11) G(bit 10:6) R(bit 5:1) A(bit 0) */
    PPE_DISP_RGBA5551,           /*!< RGBA5551: R(bit 15:11) G(bit 10:6) B(bit 5:1) A(bit 0) */
    PPE_DISP_BGRX5551,           /*!< BGRX5551: B(bit 15:11) G(bit 10:6) R(bit 5:1) X(bit 0) */
    PPE_DISP_RGBX5551,           /*!< RGBX5551: R(bit 15:11) G(bit 10:6) B(bit 5:1) X(bit 0) */
    PPE_DISP_BGR888,             /*!< BGR888: B(bit 23:16) G(bit 15:8) R(bit 7:0) */
    PPE_DISP_RGB888,             /*!< RGB888: R(bit 23:16) G(bit 15:8) B(bit 7:0) */
    PPE_DISP_BGR565,             /*!< BGR565: B(bit 15:11) G(bit 10:5) R(bit 4:0) */
    PPE_DISP_RGB565,             /*!< RGB565: R(bit 15:11) G(bit 10:5) B(bit 4:0) */
    PPE_DISP_A8,                 /*!< A8: A(bit 7:0) */
    PPE_DISP_X8,                 /*!< X8: X(bit 7:0) */
    PPE_DISP_ABGR8666 = 0x32,    /*!< ABGR8666: A(bit 31:24) B(bit 23:18) G(bit 15:10) R(bit 8:2) */
    PPE_DISP_ARGB8666,           /*!< ARGB8666: A(bit 31:24) R(bit 23:18) G(bit 15:10) B(bit 8:2) */
    PPE_DISP_XBGR8666,           /*!< XBGR8666: X(bit 31:24) B(bit 23:18) G(bit 15:10) R(bit 8:2) */
    PPE_DISP_XRGB8666,           /*!< XRGB8666: X(bit 31:24) R(bit 23:18) G(bit 15:10) B(bit 8:2) */
    PPE_DISP_BGRA6668,           /*!< BGRA6668: B(bit 31:26) G(bit 23:18) R(bit 15:10) A(bit 7:0) */
    PPE_DISP_RGBA6668,           /*!< RGBA6668: R(bit 31:26) G(bit 23:18) B(bit 15:10) A(bit 7:0) */
    PPE_DISP_BGRX6668,           /*!< BGRX6668: B(bit 31:26) G(bit 23:18) R(bit 15:10) X(bit 7:0) */
    PPE_DISP_RGBX6668,           /*!< RGBX6668: R(bit 31:26) G(bit 23:18) B(bit 15:10) X(bit 7:0) */
    PPE_DISP_FORMAT_NOT_SUPPORT = 0xFF, /*!< Other color formats are not supported */
} PPE_DISP_PIXEL_FORMAT;
/** End of PPE_DISP_PIXEL_FORMAT
  * \}
  */

/** \defgroup PPE_DISP_BLEND_MODE PPE_DISP Blend Mode
  * \{
  * \ingroup  PPE_DISP_Exported_Constants
  */
typedef enum
{
    PPE_DISP_BYPASS_MODE,          /*!< Blend foreground color only. */
    PPE_DISP_TRANSPARENT_MODE,     /*!< Blend foreground color with alpha only. */
    PPE_DISP_SRC_OVER_MODE,        /*!< Blend foreground onto background. */
    PPE_DISP_CONST_MASK_MODE,      /*!< Blend constant color onto background. */
} PPE_DISP_BLEND_MODE;
/** End of PPE_DISP_BLEND_MODE
  * \}
  */

/** \defgroup PPE_DISP_ERR PPE_DISP Error Code
  * \{
  * \ingroup  PPE_DISP_Exported_Constants
  */
typedef enum
{
    PPE_DISP_SUCCESS = 0x0,       /*!< PPE_DISP configure and run successfully. */
    PPE_DISP_ERR_NULL_TARGET,     /*!< PPE_DISP target is null. */
    PPE_DISP_ERR_NULL_SOURCE,     /*!< PPE_DISP source is null. */
    PPE_DISP_ERR_INVALID_MATRIX,  /*!< Input matrix is invalid. */
    PPE_DISP_ERR_INVALID_RANGE,   /*!< Blend range is invalid. */
} PPE_DISP_err;
/** End of PPE_DISP_ERR
  * \}
  */

/** End of PPE_DISP_Exported_Constants
  * \}
  */

/*============================================================================*
 *                         Structures
 *============================================================================*/
/** \defgroup PPE_DISP_Exported_Types PPE_DISP Exported Types
  * \brief
  * \{
  */

/**
 * \cond        private
 * \brief       Structures for internal use
 * \defgroup    PPE_DISP_INTERNAL_STRUCTURE   PPE_DISP structures for internal use
 * \{
 */
typedef struct
{
    FunctionalState         SetValid_AutoClear;
    uint32_t                LLP;
    PPE_DISP_SECURE            Secure_En;
} PPE_DISP_Init_Typedef;

typedef struct
{
    uint32_t                            Layer_Address;
    uint32_t                            Canvas_Height;
    uint32_t                            Canvas_Width;
    uint32_t                            Line_Length;
    PPE_DISP_PIXEL_FORMAT                  Color_Format;
    uint32_t                            BackGround;
    PPE_DISP_AWBURST                       LayerBus_Inc;
    PPE_DISP_MAX_AXLEN                     Max_Axlen;
    FunctionalState                     MultiFrame_Reload_En;
    FunctionalState                     MultiFrame_LLP_En;
    PPE_DISP_HW_HS                         Layer_HW_Handshake_En;
    PPE_DISP_HW_HANDSHAKE_INDEX            Layer_HW_Handshake_Index;
    PPE_DISP_HW_HS_POL                     Layer_HW_Handshake_Polarity;
    PPE_DISP_MSIZE_LOG                     Layer_HW_Handshake_MsizeLog;
    uint32_t                          block_width;
    uint32_t                          block_height;
} PPE_DISP_ResultLayer_Init_Typedef;

typedef struct
{
    uint32_t                            Layer_Address;
    uint32_t                            Pic_Height;
    uint32_t                            Pic_Width;
    uint32_t                            Line_Length;
    PPE_DISP_PIXEL_SOURCE                  Pixel_Source;
    PPE_DISP_PIXEL_FORMAT                  Pixel_Color_Format;
    PPE_DISP_READ_MATRIX_SIZE              Read_Matrix_Size;
    uint32_t                            Const_Pixel;
    PPE_DISP_COLOR_KEY_MODE                Color_Key_Mode;
    uint32_t                            Color_Key_Min;
    uint32_t                            Color_Key_Max;
    FunctionalState                     MultiFrame_Reload_En;
    FunctionalState                     MultiFrame_LLP_En;
    PPE_DISP_AWBURST                       LayerBus_Inc;
    PPE_DISP_MAX_AXLEN                     Max_Axlen;
    PPE_DISP_HW_HS                         Layer_HW_Handshake_En;
    PPE_DISP_HW_HANDSHAKE_INDEX            Layer_HW_Handshake_Index;
    PPE_DISP_HW_HS_POL                     Layer_HW_Handshake_Polarity;
    PPE_DISP_MSIZE_LOG                     Layer_HW_Handshake_MsizeLog;
    uint16_t                            Layer_Window_Xmin;
    uint16_t                            Layer_Window_Xmax;
    uint16_t                            Layer_Window_Ymin;
    uint16_t                            Layer_Window_Ymax;
    uint32_t                            Transfer_Matrix_E11;
    uint32_t                            Transfer_Matrix_E12;
    uint32_t                            Transfer_Matrix_E13;
    uint32_t                            Transfer_Matrix_E21;
    uint32_t                            Transfer_Matrix_E22;
    uint32_t                            Transfer_Matrix_E23;
    uint32_t                            Transfer_Matrix_E31;
    uint32_t                            Transfer_Matrix_E32;
    uint32_t                            Transfer_Matrix_E33;
} PPE_DISP_InputLayer_Init_Typedef;

typedef struct
{
    uint32_t LYRx_ADDR;
    uint32_t LYRx_PIC_SIZE;
    uint32_t LYRx_PIC_CFG;
    uint32_t LYRx_FIXED_COLOR;
    uint32_t LYRx_WIN_MIN;
    uint32_t LYRx_WIN_MAX;
    uint32_t LYRx_KEY_MIN;
    uint32_t LYRx_KEY_MAX;
    uint32_t LYRx_TRANS_MATRIX_E11;
    uint32_t LYRx_TRANS_MATRIX_E12;
    uint32_t LYRx_TRANS_MATRIX_E13;
    uint32_t LYRx_TRANS_MATRIX_E21;
    uint32_t LYRx_TRANS_MATRIX_E22;
    uint32_t LYRx_TRANS_MATRIX_E23;
    uint32_t LYRx_TRANS_MATRIX_E31;
    uint32_t LYRx_TRANS_MATRIX_E32;
    uint32_t LYRx_TRANS_MATRIX_E33;
} PPE_DISP_LLI_INPUT_LAYER;
/**
 * \}
 * \endcond
 */

/** \defgroup PPE_DISP_RECT PPE_DISP Rectangle
  * \{
  * \ingroup  PPE_DISP_Exported_Types
  */
typedef struct
{
    int x;         /*!< The x coordinate of the rectangle left-top corner. */
    int y;         /*!< The y coordinate of the rectangle left-top corner. */
    uint32_t w;    /*!< The width of the rectangle. */
    uint32_t h;    /*!< The height of the rectangle. */
} ppe_rect_t;
/** End of PPE_DISP_RECT
  * \}
  */

/** \defgroup PPE_DISP_MATRIX PPE_DISP Matrix
  * \{
  * \ingroup  PPE_DISP_Exported_Types
  */
typedef struct
{
    float m[3][3];    /*! The 3x3 matrix, in [row][column] order. */
} ppe_matrix_t;
/** End of PPE_DISP_MATRIX
  * \}
  */

/** \defgroup PPE_DISP_POINT PPE_DISP Point
  * \{
  * \ingroup  PPE_DISP_Exported_Types
  */
typedef struct
{
    int x;        /*!< The x coordinate of the point. */
    int y;        /*!< The y coordinate of the point. */
}
ppe_point_t;
/** End of PPE_DISP_POINT
  * \}
  */

/** \defgroup PPE_DISP_POINTS PPE_DISP Points of Polygon
  * \{
  * \ingroup  PPE_DISP_Exported_Types
  */
typedef ppe_point_t ppe_point4_t[4]; /* Four 2D Point that form a polygon */
/** End of PPE_DISP_POINTS
  * \}
  */

/** \defgroup PPE_DISP_BUFFER PPE_DISP Layer Buffer
  * \{
  * \ingroup  PPE_DISP_Exported_Types
  */
typedef struct
{
    uint32_t address;     /* Address of buffer memory. */
    uint16_t width;       /* Width of buffer in pixel. */
    uint16_t height;      /* Height of buffer in pixel. */
    uint16_t stride;      /* Stride of buffer. */
    uint32_t const_color; /* Constant color for blending. */
    uint16_t win_x_min;   /* Horizontal minimum coordinate of buffer. */
    uint16_t win_x_max;   /* Horizontal Maximum coordinate of buffer. */
    uint16_t win_y_min;   /* Vertical minimun coordinate of buffer. */
    uint16_t win_y_max;   /* Vertical Maximum coordinateof buffer. */
    uint32_t color_key_max; /* Maximum value of color key in ABGR8888 format. */
    uint32_t color_key_min; /* Minimum value of color key in ABGR8888 format. */
    PPE_DISP_COLOR_KEY_MODE color_key_enable; /* Color Key Mode. */
    PPE_DISP_PIXEL_FORMAT format; /* Pixel format of buffer. */
    uint8_t opacity;       /* Opacity of buffer. */
    bool high_quality;    /* Anti-Aliasing with bilinear interpoaltion. */
} ppe_buffer_t;
/** End of PPE_DISP_BUFFER
  * \}
  */

/** End of PPE_DISP_Exported_Types
  * \}
  */


/*============================================================================*
 *                         Functions
 *============================================================================*/
/** \defgroup PPE_DISP_Exported_Functions PPE_DISP Exported Functions
  * \brief
  * \{
  */

/**
 * \cond        private
 * \brief       Functions for internal use
 * \defgroup    PPE_DISP_INTERNAL_FUNCTION   PPE_DISP functions for internal use
 * \{
 */
void PPE_DISP_CLK_ENABLE(FunctionalState NewState);

void PPE_DISP_Init(PPE_DISP_Init_Typedef *PPE_DISP_Init_Struct);

void PPE_DISP_ResultLayer_Init(PPE_DISP_ResultLayer_Init_Typedef *PPE_DISP_ResultLyaer_Init_Struct);

void PPE_DISP_InputLayer_Init(PPE_DISP_INPUT_LAYER_INDEX intput_layer_index,
                         PPE_DISP_InputLayer_Init_Typedef *PPE_DISP_InputLayer_Init_Struct);

void PPE_DISP_StructInit(PPE_DISP_Init_Typedef *PPE_DISP_Init_Struct);

void HONEYGUI_PPE_DISP_ResultLayer_StructInit(PPE_DISP_ResultLayer_Init_Typedef *PPE_DISP_ResultLyaer_Init_Struct);

void HONEYGUI_PPE_DISP_InputLayer_StructInit(PPE_DISP_INPUT_LAYER_INDEX intput_layer_index,
                               PPE_DISP_InputLayer_Init_Typedef *PPE_DISP_InputLayer_Init_Struct);

void PPE_DISP_InputLayer_enable(PPE_DISP_INPUT_LAYER_INDEX intput_layer_index, FunctionalState NewState);

PPE_DISP_err PPE_DISP_Blend_Handshake(ppe_buffer_t *dst, ppe_buffer_t *src, ppe_rect_t *rect);

void HONEYGUI_PPE_DISP_Cmd(FunctionalState NewState);

FunctionalState PPE_DISP_Get_Interrupt_Status(PPE_DISP_INTERRUPT PPE_DISP_int);

FunctionalState PPE_DISP_Get_Raw_Interrupt_Status(PPE_DISP_INTERRUPT PPE_DISP_int);

void PPE_DISP_Clear_Interrupt(PPE_DISP_INTERRUPT PPE_DISP_int);

void PPE_DISP_Mask_Interrupt(PPE_DISP_INTERRUPT PPE_DISP_int, FunctionalState NewState);

void PPE_DISP_Mask_All_Interrupt(FunctionalState NewState);

void ppe_perspective(float px, float py, ppe_matrix_t *matrix);

PPE_DISP_err PPE_DISP_Blend_Multi(ppe_buffer_t *dst, ppe_buffer_t *src_1,
                        ppe_buffer_t *src_2, ppe_buffer_t *src_3, PPE_DISP_BLEND_MODE mode);
/**
 * \}
 * \endcond
 */

/**
 * \brief  Blend source image onto target buffer.
 * \param[in] src: Source image buffer to be blended.
 * \param[in] dst: Target image buffer to be blended onto.
 * \param[in] matrix: Transformation matrix of source image.
 * \param[in] blend_mode: Blend mode from @ref PPE_DISP_BLEND_MODE to be used.
 * \return Operation result.
 * \retval PPE_DISP_SUCCESS  Operation success.
 * \retval Others       Operation failure, cause refers to @ref PPE_DISP_ERR .
 *
 * <b>Example usage</b>
 * \code{.c}
    void test_code(void){
        RCC_PeriphClockCmd(APBPeriph_PPE_DISP, APBPeriph_PPE_DISP_CLOCK, ENABLE);
        ppe_buffer_t image, buffer;
        image.width = 25;
        image.height = 25;
        image.stride = 32;
        image.format = PPE_DISP_RGB888;
        image.address = (uint32_t)address1;
        image.color_key_enable = PPE_DISP_COLOR_KEY_DISABLE;
        image.opacity = 255; //opaque
        image.high_quality = true; //anti-aliasing with bilinear interpolation
        image.win_x_min = 0;
        image.win_x_max = 25;
        image.win_y_min = 0;
        image.win_y_max = 25;

        buffer.format = PPE_DISP_RGB565;
        buffer.address = (uint32_t)address2;
        buffer.width = 64;
        buffer.height = 64;
        buffer.stride = 64;
        buffer.win_x_min = 0;
        buffer.win_x_max = 64;
        buffer.win_y_min = 0;
        buffer.win_y_max = 64;
        ppe_matrix_t matrix;
        ppe_get_identity(&matrix);
        PPE_DISP_Blit(&buffer, &image, &matrix, PPE_DISP_SRC_OVER_MODE);
        PPE_DISP_Finish(); //wait for PPE_DISP to finish blending
    }
 * \endcode
 */
PPE_DISP_err PPE_DISP_Blit(ppe_buffer_t *dst, ppe_buffer_t *src, ppe_matrix_t *matrix,
                 PPE_DISP_BLEND_MODE mode);

/**
 * \brief  Blend source image onto target buffer with inverse matrix, this function usually has higher performance than @ref PPE_DISP_Blit.
 * \param[in] src: Source image buffer to be blended.
 * \param[in] dst: Target image buffer to be blended onto.
 * \param[in] inv: Inverse transformation matrix of source image.
 * \param[in] rect: Rectangle of blending area.
 * \param[in] blend_mode: Blend mode from @ref PPE_DISP_BLEND_MODE to be used.
 * \return Operation result.
 * \retval PPE_DISP_SUCCESS  Operation success.
 * \retval Others       Operation failure, cause refers to @ref PPE_DISP_ERR .
 *
 * <b>Example usage</b>
 * \code{.c}
    void test_code(void){
        RCC_PeriphClockCmd(APBPeriph_PPE_DISP, APBPeriph_PPE_DISP_CLOCK, ENABLE);
        ppe_buffer_t image, buffer;
        image.width = 25;
        image.height = 25;
        image.stride = 32;
        image.format = PPE_DISP_RGB888;
        image.address = (uint32_t)address1;
        image.color_key_enable = PPE_DISP_COLOR_KEY_DISABLE;
        image.opacity = 255; //opaque
        image.high_quality = true; //anti-aliasing with bilinear interpolation
        image.win_x_min = 0;
        image.win_x_max = 25;
        image.win_y_min = 0;
        image.win_y_max = 25;

        buffer.format = PPE_DISP_RGB565;
        buffer.address = (uint32_t)address2;
        buffer.width = 64;
        buffer.height = 64;
        buffer.stride = 64;
        buffer.win_x_min = 0;
        buffer.win_x_max = 64;
        buffer.win_y_min = 0;
        buffer.win_y_max = 64;
        ppe_matrix_t matrix;
        ppe_get_identity(&matrix);
        ppe_rect_t src_rect = {.x = 0, .y = 0, .w = 25, .h = 25};
        ppe_rect_t dst_rect = {0}
        ppe_get_area(&dst_rect, &src_rect, &matrix);
        PPE_DISP_Blit_Inverse(&buffer, &image, &matrix, &dst_rect, PPE_DISP_SRC_OVER_MODE);
        PPE_DISP_Finish(); //wait for PPE_DISP to finish blending
    }
 * \endcode
 */
PPE_DISP_err PPE_DISP_Blit_Inverse(ppe_buffer_t *dst, ppe_buffer_t *src, ppe_matrix_t *inverse,
                         ppe_rect_t *rect, PPE_DISP_BLEND_MODE mode);

/**
 * \brief  Wait for PPE_DISP to finish blending.
 *
 * <b>Example usage</b>
 * \code{.c}
    void test_code(void){
        //PPE_DISP initialization
        PPE_DISP_Finish();
    }
 * \endcode
*/
void PPE_DISP_Finish(void);

/**
 * \brief  Get pixel size of specified format.
 * \param[in] format: Pixel format of PPE_DISP from @ref PPE_DISP_PIXEL_FORMAT .
 * \return Pixel size in bytes.
 *
 * <b>Example usage</b>
 * \code{.c}
    void test_code(void){
        uint8_t pixel_size = PPE_DISP_Get_Pixel_Size(PPE_DISP_RGB565);
    }
 * \endcode
 */
uint8_t PPE_DISP_Get_Pixel_Size(PPE_DISP_PIXEL_FORMAT format);

/**
 * \brief  Initialize the input matrix as identity matrix.
 * \param[in] matrix: Pointer to matrix to be initialized.
 *
 * <b>Example usage</b>
 * \code{.c}
    void test_code(void){
        ppe_matrix_t matrix;
        ppe_get_identity(&matrix);
    }
 * \endcode
 */
void ppe_get_identity(ppe_matrix_t *matrix);

/**
 * \brief  Do translation on input matrix.
 * \param[in] x: Horizontal coordinate to be translated.
 * \param[in] y: Vertical coordinate to be translated.
 * \param[in] matrix: Pointer to matrix to be transformed.
 *
 * <b>Example usage</b>
 * \code{.c}
    void test_code(void){
        ppe_matrix_t matrix;
        ppe_get_identity(&matrix);
        ppe_translate(10, 10, &matrix);
    }
 * \endcode
 */
void ppe_translate(float x, float y, ppe_matrix_t *matrix);

/**
 * \brief  Do scale transformation on input matrix.
 * \param[in] scale_x: Horizontal scale ratio.
 * \param[in] scale_y: Vertical scale ratio.
 * \param[in] matrix: Pointer to matrix to be transformed.
 *
 * <b>Example usage</b>
 * \code{.c}
    void test_code(void){
        ppe_matrix_t matrix;
        ppe_get_identity(&matrix);
        ppe_scale(0.5, 0.5, &matrix);
    }
 * \endcode
 */
void ppe_scale(float scale_x, float scale_y, ppe_matrix_t *matrix);

/**
 * \brief  Do rotation on input matrix.
 * \param[in] degrees: Angle to be rotated.
 * \param[in] matrix: Pointer to matrix to be rotated.
 *
 * <b>Example usage</b>
 * \code{.c}
    void test_code(void){
        ppe_matrix_t matrix;
        ppe_get_identity(&matrix);
        ppe_rotate(45, &matrix);
    }
 * \endcode
 */
void ppe_rotate(float degrees, ppe_matrix_t *matrix);

/**
 * \brief  Do skew transformation on input matrix.
 * \param[in] skew_x: Horizontal skew angle.
 * \param[in] skew_y: Vertical skew angle.
 * \param[in] matrix: Pointer to matrix to be skewed.
 *
 * <b>Example usage</b>
 * \code{.c}
    void test_code(void){
        ppe_matrix_t matrix;
        ppe_get_identity(&matrix);
        ppe_skew(45, 45, &matrix);
    }
 * \endcode
 */
void ppe_skew(float skew_x, float skew_y, ppe_matrix_t *matrix);

/**
 * \brief  Do reflect transformation on input matrix.
 * \param[in] horizontal: Horizontal reflection.
 * \param[in] vertical: Vertical reflection.
 * \param[in] matrix: Pointer to matrix to be skewed.
 *
 * <b>Example usage</b>
 * \code{.c}
    void test_code(void){
        ppe_matrix_t matrix;
        ppe_get_identity(&matrix);
        ppe_reflect(true, false, &matrix); //reflect horizontally
    }
 * \endcode
 */
void ppe_reflect(bool horizontal, bool vertical, ppe_matrix_t *matrix);


/**
 * \brief  Get matrix from 4 corners of image and output region.
 * \param[in] src: Points of 4 corners of input image.
 * \param[in] dst: Points of 4 corners of output region.
 * \param[in] mat: Pointer to the matrix storing result.
 * \return Operation result.
 * \retval 0          Successfully find the matrix.
 * \retval -1         Input parameter is invalid.
 *
 * <b>Example usage</b>
 * \code{.c}
    void test_code(void){
        ppe_matrix_t matrix;
        ppe_point4_t src = {{0,0},{100,0},{0,100},{100,100}};
        ppe_point4_t dst = {{-10,-10},{150,27},{-5,80},{130,99}};
        ppe_get_transform_matrix(src, dst, &matrix);
    }
 * \endcode
 */
int  ppe_get_transform_matrix(ppe_point4_t src, ppe_point4_t dst, ppe_matrix_t *mat);

/**
 * \brief  Inverse the input matrix.
 * \param[in] skew_x: Pointer to the input matrix.
 *
 * <b>Example usage</b>
 * \code{.c}
    void test_code(void){
        ppe_matrix_t matrix;
        ppe_get_identity(&matrix);
        ppe_skew(45, 45, &matrix);
        ppe_matrix_inverse(&matrix);
    }
 * \endcode
 */
void ppe_matrix_inverse(ppe_matrix_t *matrix);

/**
 * \brief  Multiply 2 matrices.
 * \param[in] matrix: Pointer to one of the input matrix, this matrix stores result.
 * \param[in] mult: Pointer to the other input matrix.
 *
 * <b>Example usage</b>
 * \code{.c}
    void test_code(void){
        ppe_matrix_t matrix, mult;
        ppe_get_identity(&matrix);
        ppe_skew(45, 45, &matrix);
        ppe_get_identity(&mult);
        ppe_translate(100, 100, &mult);
        ppe_mat_multiply(&matrix, &mult);
    }
 * \endcode
 */
void ppe_mat_multiply(ppe_matrix_t *matrix, ppe_matrix_t *mult);

/**
 * \brief  Find the output region generated by input region and input matrix.
 * \param[in] result_rect: Points of output region.
 * \param[in] source_rect: Points of input region.
 * \param[in] matrix: Pointer to the input matrix.
 * \param[in] buffer: Pointer to a PPE_DISP buffer that stores boundary information.
 * \return Operation result.
 * \retval true       Successfully find the output region.
 * \retval false      Input region or input matrix is not valid.
 *
 * <b>Example usage</b>
 * \code{.c}
    void test_code(void){
        ppe_matrix_t matrix;
        ppe_point4_t src = {{0,0},{100,0},{0,100},{100,100}};
        ppe_matrix_identity(&matrix);
        ppe_skew(45, 45, &matrix);
        ppe_get_area(&result_rect, &src, &matrix, NULL);
    }
 * \endcode
 */
bool ppe_get_area(ppe_rect_t *result_rect, ppe_rect_t *source_rect, ppe_matrix_t *matrix,
                  ppe_buffer_t *buffer);

/**
 * \brief  Check if input matrix has shape transform.
 * \param[in] matrix: Pointer to the input matrix.
 * \return Operation result.
 * \retval true       Input matrix is transformed.
 * \retval false      Input matrix is not transformed.
 *
 * <b>Example usage</b>
 * \code{.c}
    void test_code(void){
        //generate a matrix
        if(!ppe_is_identity_or_translate(&matrix)){
            //handle simple case
        }
    }
 * \endcode
 */
bool ppe_is_identity_or_translate(ppe_matrix_t *matrix);

/**
 * \brief  Mask the buffer with certain color in ABGR8888 format.
 * \param[in] dst: Target buffer to be cleared.
 * \param[in] color: Specified color in ABGR8888 format \ref PPE_DISP_ABGR8888
 * \param[in] rect: Rectangle region of masking area.
 * \return Operation result.
 * \retval PPE_DISP_SUCCESS   Operation success.
 * \retval Others        Operation failure, cause refers to @ref PPE_DISP_ERR .
 *
 * <b>Example usage</b>
 * \code{.c}
    void test_code(void){
        RCC_PeriphClockCmd(APBPeriph_PPE_DISP, APBPeriph_PPE_DISP_CLOCK, ENABLE);
        ppe_buffer_t buffer;

        buffer.format = PPE_DISP_RGB565;
        buffer.address = (uint32_t)PIC_OUTPUT_LAYER;
        buffer.width = 64;
        buffer.height = 64;
        buffer.stride = 64;
        buffer.win_x_min = 0;
        buffer.win_x_max = 64;
        buffer.win_y_min = 0;
        buffer.win_y_max = 64;
        ppe_rect_t rect = {0, 0, 64, 64};
        PPE_DISP_Mask(&buffer, PPE_DISP_ABGR8888(0, 0, 0, 0), &rect);
        PPE_DISP_Finish(); //wait for PPE_DISP to finish blending
    }
 * \endcode
 */
PPE_DISP_err PPE_DISP_Mask(ppe_buffer_t *dst, uint32_t color, ppe_rect_t *rect);

bool ppe_matrix_is_complex(ppe_matrix_t *mat);
/** End of PPE_DISP_Exported_Functions
  * \}
  */

/** End of PPE_DISP
  * \}
  */

#ifdef __cplusplus
}
#endif

#endif /* RTL_PPE_DISP_H */

/******************* (C) COPYRIGHT 2023 Realtek Semiconductor Corporation *****END OF FILE****/

