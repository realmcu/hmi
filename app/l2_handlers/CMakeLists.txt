# Copyright (c) 2024 Realtek Semiconductor Corp.
# SPDX-License-Identifier: Apache-2.0
#
# EXCLUDED FROM BUILD.  These L2 command handlers were bound to the old
#百度 protocol stack (HMI_L2_CMD_* namespace, KV-list wire format).  V1.2
# migration replaces them with app/protocol/handlers/*.  Source files are
# retained for reference but not compiled.  See note eBadge8773g-p2p-xfer
# for the P2P sender (hmi_l2_xfer_client.c) that also parks here.

cmake_minimum_required(VERSION 3.10)
