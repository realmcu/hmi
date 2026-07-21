/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * TFLite Micro hello_world 演示 —— Zephyr shell 命令
 *
 * 两段推理:
 *   1) float 模型  —— reference kernel (fully_connected.cc)
 *   2) int8 模型   —— CMSIS-NN MVE kernel (cmsis_nn/fully_connected.cc)
 *
 * int8 段需要手工量化输入 / 反量化输出, 通过 tensor 的 params.scale/zero_point.
 *
 * Shell 用法 (串口):
 *   tflm float    仅跑 float 推理
 *   tflm int8     仅跑 int8 (CMSIS-NN MVE) 推理
 *   tflm all      两个都跑, 对比输出
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/shell/shell.h>

#include "tensorflow/lite/core/c/common.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

/* 模型 C 数组 */
extern const unsigned char g_hello_world_float_model_data[];
extern const unsigned char g_hello_world_int8_model_data[];

namespace
{

constexpr int kTensorArenaSize = 3000;
/* 两次推理顺序执行, 共用同一块 arena, 省 3KB RAM */
alignas(16) uint8_t s_tensor_arena[kTensorArenaSize];

/* 打印小助手 —— 避免依赖 %f, 把 float 拆成整数打 */
void PrintFloat(const char *tag, float v)
{
    int   whole = (int)v;
    int   frac  = (int)((v >= 0 ? v : -v) * 1000) % 1000;
    const char *sign = (v < 0 && whole == 0) ? "-" : "";
    printk("%s=%s%d.%03d ", tag, sign, whole, frac);
}

TfLiteStatus RunFloatModel()
{
    printk("\n[tflm][float] start (reference kernel)\n");

    const tflite::Model *model = tflite::GetModel(g_hello_world_float_model_data);
    if (model->version() != TFLITE_SCHEMA_VERSION)
    {
        printk("[tflm][float] schema mismatch\n");
        return kTfLiteError;
    }

    tflite::MicroMutableOpResolver<1> resolver;
    resolver.AddFullyConnected();

    tflite::MicroInterpreter interp(model, resolver, s_tensor_arena, kTensorArenaSize);
    if (interp.AllocateTensors() != kTfLiteOk)
    {
        printk("[tflm][float] AllocateTensors failed\n");
        return kTfLiteError;
    }
    printk("[tflm][float] arena used = %u / %u bytes\n",
           (unsigned)interp.arena_used_bytes(), (unsigned)kTensorArenaSize);

    for (int i = 0; i < 10; i++)
    {
        float x = 6.2832f * (float)i / 10.0f;
        interp.input(0)->data.f[0] = x;
        if (interp.Invoke() != kTfLiteOk)
        {
            printk("[tflm][float] Invoke failed @ %d\n", i);
            return kTfLiteError;
        }
        float y = interp.output(0)->data.f[0];
        PrintFloat("x", x);
        PrintFloat("y", y);
        printk("\n");
    }
    printk("[tflm][float] done\n");
    return kTfLiteOk;
}

TfLiteStatus RunInt8Model()
{
    printk("\n[tflm][int8]  start (CMSIS-NN MVE kernel)\n");

    const tflite::Model *model = tflite::GetModel(g_hello_world_int8_model_data);
    if (model->version() != TFLITE_SCHEMA_VERSION)
    {
        printk("[tflm][int8] schema mismatch\n");
        return kTfLiteError;
    }

    tflite::MicroMutableOpResolver<1> resolver;
    resolver.AddFullyConnected();

    tflite::MicroInterpreter interp(model, resolver, s_tensor_arena, kTensorArenaSize);
    if (interp.AllocateTensors() != kTfLiteOk)
    {
        printk("[tflm][int8] AllocateTensors failed\n");
        return kTfLiteError;
    }
    printk("[tflm][int8]  arena used = %u / %u bytes\n",
           (unsigned)interp.arena_used_bytes(), (unsigned)kTensorArenaSize);

    /* int8 模型的量化参数从 tensor 上读, 而不是硬编码 */
    TfLiteTensor *input  = interp.input(0);
    TfLiteTensor *output = interp.output(0);
    const float in_scale   = input->params.scale;
    const int   in_zp      = input->params.zero_point;
    const float out_scale  = output->params.scale;
    const int   out_zp     = output->params.zero_point;

    printk("[tflm][int8]  input:  scale=%d.%06d zp=%d\n",
           (int)in_scale, (int)(in_scale * 1000000) % 1000000, in_zp);
    printk("[tflm][int8]  output: scale=%d.%06d zp=%d\n",
           (int)out_scale, (int)(out_scale * 1000000) % 1000000, out_zp);

    for (int i = 0; i < 10; i++)
    {
        float x = 6.2832f * (float)i / 10.0f;

        /* 量化: q = round(x / scale) + zp, 饱和到 [-128, 127] */
        int q = (int)(x / in_scale + 0.5f) + in_zp;
        if (q < -128) { q = -128; }
        if (q >  127) { q =  127; }
        input->data.int8[0] = (int8_t)q;

        if (interp.Invoke() != kTfLiteOk)
        {
            printk("[tflm][int8] Invoke failed @ %d\n", i);
            return kTfLiteError;
        }

        /* 反量化: y = (q - zp) * scale */
        int8_t out_q = output->data.int8[0];
        float  y     = (out_q - out_zp) * out_scale;

        PrintFloat("x", x);
        PrintFloat("y", y);
        printk("(q=%d)\n", out_q);
    }
    printk("[tflm][int8]  done\n");
    return kTfLiteOk;
}

}  // namespace

/* ============================================================
 * Zephyr shell 命令
 *   tflm float   仅跑 float
 *   tflm int8    仅跑 int8 (CMSIS-NN MVE)
 *   tflm all     两个都跑
 * ============================================================ */

static int cmd_tflm_float(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    return (RunFloatModel() == kTfLiteOk) ? 0 : -1;
}

static int cmd_tflm_int8(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    return (RunInt8Model() == kTfLiteOk) ? 0 : -1;
}

static int cmd_tflm_all(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    TfLiteStatus s1 = RunFloatModel();
    TfLiteStatus s2 = RunInt8Model();
    shell_print(sh, "\n[tflm] all done (float=%d int8=%d)", (int)s1, (int)s2);
    return (s1 == kTfLiteOk && s2 == kTfLiteOk) ? 0 : -1;
}

SHELL_STATIC_SUBCMD_SET_CREATE(tflm_cmds,
                               SHELL_CMD(float, NULL, "Run hello_world float (reference kernel)",     cmd_tflm_float),
                               SHELL_CMD(int8,  NULL, "Run hello_world int8  (CMSIS-NN MVE kernel)",  cmd_tflm_int8),
                               SHELL_CMD(all,   NULL, "Run both float and int8, compare outputs",     cmd_tflm_all),
                               SHELL_SUBCMD_SET_END
                              );

SHELL_CMD_REGISTER(tflm, &tflm_cmds, "TFLite Micro hello_world demo", NULL);
