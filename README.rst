Overview
********

DeskMate dashboard application for the rtl87x3g series. It drives the SH8601Z
410x502 QSPI LCD through HoneyGUI and renders a vehicle-style dashboard: speed
and power arcs, map panel, media/lyrics card, turn indicators, LED strip and a
status bar.

The UI is not hand-written C. It is a HoneyGUI Visual Designer project kept in
``ui_design/`` and exported to C sources that the application compiles in. The
BLE layer adds a custom navigation-projection service so a phone can stream
JPEG map frames to the panel.

Layout
******

* ``main.c`` - application entry point: lower-level init, GUI server start.
* ``app_lower_init.c`` - MPU region, 200 MHz CPU, 4-bit NOR flash at 200 MHz and
  the two OPI PSRAM partitions at 280 MHz.
* ``flash_map.h`` - the flash partition table (app bank, OTA, user data, FTL).
  It must stay in sync with the Realtek ``mp.ini`` package layout.
* ``boards/rtl87x3g_evb.overlay`` - console UART, pinctrl groups, LCD node,
  ``gpio-keys`` node.
* ``port/ui/`` - HoneyGUI porting layer: display controller, input devices,
  OS glue, filesystem, FTL and the GPU/2D accelerator binding.
* ``port/flashdb/`` - FlashDB + FAL port on top of the Realtek FMC API. Skipped
  at configure time while ``component/FlashDB`` is absent.
* ``port/posix/`` - posix-device port. Skipped at configure time while
  ``component/posix`` is absent.
* ``src/ble/`` - GAP / advertising / GATT services, the navigation-projection
  service and the BLE event thread.
* ``ui_design/`` - the designer project: ``ui/`` HML sources, ``assets/``, the
  exported ``src/`` tree and the romfs it builds. This is a **separate git
  repository**, see `UI workflow`_.
* ``scripts/`` - ``build.sh`` / ``flash.sh`` wrappers around ``west build`` and
  ``mpcli.exe``.
* ``mp.ini`` - firmware package description consumed by the post-build step.

Building
********

.. code-block:: console

   west build -b rtl87x3g_evb -p

``scripts/build.sh [-p]`` does the same and is what ``.vscode/tasks.json``
runs. Add ``DRY=1`` to only print the command.

Two images land in ``bin/``:

* ``app.bin`` - headerless application image at ``BANK0_APP_ADDR``, what
  ``scripts/flash.sh`` writes.
* ``app_MP-<version>--<hash>.bin`` - the same code with the MP/ECDSA header
  prepended by ``cmake/post_build.cmake``, for the production programming flow.

Flashing
********

Two independent regions must be programmed; the build only produces the first.

**Application image** - ``0x7009E000`` (``BANK0_APP_ADDR``):

.. code-block:: console

   scripts/flash.sh              # default COM7
   scripts/flash.sh COM8         # another download port

The device has to be in MP download mode first (P2_0 to GND, then reset).

**UI resources** - the romfs mounted by ``dashboardEntry.c``:

* ``ui_design/build/app_romfs.bin`` - raw payload, write at ``0x704D1400``,
  which is ``USER_DATA1_ADDR`` (``0x704D1000``) plus ``IMG_HDR_SIZE`` (``0x400``).
* ``ui_design/build/UserData1_MP-<hash>.bin`` - the same payload with the MP
  header, write at ``USER_DATA1_ADDR`` (``0x704D1000``).

Use the same ``mpcli`` invocation as for the app, changing only ``-A`` and
``-F``. The romfs address also appears as ``romfsBaseAddr`` in
``ui_design/project.json`` and as a literal in ``ui_design/src/dashboardEntry.c``;
all three must agree with ``USER_DATA1_ADDR`` in ``flash_map.h``. If the UI
shows a blank screen with an asset assert, this region is the first thing to
check - a ``west build`` alone never updates it.

Boot sequence
*************

1. ``main()`` - when ``CONFIG_UART_CONSOLE`` is off, remaps the ROM log UART to
   P3_1 first, so the rest of the boot is visible on that pad.
2. ``app_system_lower_init()`` - MPU region over ``SPIC1_MEM_BASE``, CPU to
   200 MHz, NOR flash to 4-bit mode at 200 MHz, both OPI PSRAM partitions to
   280 MHz. Each step logs its result through ``DBG_DIRECT``.
3. The ``printf`` banner prints the board target and the ``main`` thread id.
4. ``gui_server_init()`` - runs the port hooks (``gui_port_*_init``), loads the
   default font, swaps the framebuffer and creates the GUI server thread.
5. The GUI thread calls ``gui_components_init()``, which walks the
   ``.honeygui$init7`` linker section.
6. ``ui_design/src/dashboardEntry.c:app_init()`` - mounts the romfs, creates the
   stream transport and then ``gui_view_create(..., "dashboardMainView", ...)``.
7. ``gui_set_keep_active_time(10000000)`` - the panel stays on for 10 s after
   the last input event.

Configuration
*************

Everything is set in ``prj.conf``. The only application-level Kconfig options
are the Bluetooth profile switches in ``Kconfig``, inside the
``DeskMate Application`` menu.

Logging and console
===================

.. code-block:: none

   CONFIG_LOG=y
   CONFIG_LOG_BACKEND_UART=y
   CONFIG_SHELL_LOG_BACKEND=n
   CONFIG_LOG_BUFFER_SIZE=4096
   CONFIG_LOG_PROCESS_THREAD_CUSTOM_PRIORITY=y
   CONFIG_LOG_PROCESS_THREAD_PRIORITY=1
   CONFIG_LOG_PROCESS_TRIGGER_THRESHOLD=1
   CONFIG_LOG_PROCESS_THREAD_SLEEP_MS=100

``LOG_*`` goes to the console UART directly instead of through the shell: log
lines keep their own format and are not dropped while the shell is parsing.
The log thread is raised to priority 1 and woken on every message so deferred
output is not parked at the end of the buffer. ``gui_log()`` takes a different
path - it is ``printf`` through ``port/ui/gui_port_os.c`` - so GUI traces and
``LOG_*`` traces both end up on the same UART.

The console is ``uart2`` on P2_0 (TX) / P2_1 (RX). ``boards/rtl87x3g_evb.overlay``
currently pins it to **115200** while the pin routing is being brought up; the
board default is 2000000. Change ``current-speed`` there to match your adapter.

Shell
=====

.. code-block:: none

   CONFIG_SHELL=y
   CONFIG_SHELL_BACKEND_SERIAL=y
   CONFIG_SHELL_THREAD_PRIORITY_OVERRIDE=y
   CONFIG_SHELL_THREAD_PRIORITY=0

The shell shares ``uart2`` with the console and runs at the highest priority.
It blocks in ``k_poll`` while idle, so it only costs CPU when a key press wakes
it. Note that the ``cmd`` dashboard command from ``ui_design/README.md`` is
compiled only for the simulator build, not for the target.

Display and GUI
===============

.. code-block:: none

   CONFIG_REALTEK_HONEYGUI=y
   CONFIG_REALTEK_H264_DECODER=y
   CONFIG_REALTEK_H264BSD=y
   CONFIG_DISPLAY=y
   CONFIG_REALTEK_DISPLAY=y
   CONFIG_REALTEK_DISPLAY_ENABLE_LCDC=y
   CONFIG_REALTEK_DISPLAY_ENABLE_PPE=y
   CONFIG_REALTEK_DISPLAY_ENABLE_IDU=y
   CONFIG_REALTEK_DISPLAY_GUI_ACC=y
   CONFIG_REALTEK_DISPLAY_ENABLE_JPU=y
   CONFIG_REALTEK_LCD_SH8601Z_410_502_QSPI=y

``CONFIG_REALTEK_DISPLAY_GUI_ACC`` enables the 2D accelerator path and the JPU
entry (``modules/display/bindings/honeygui/hardware_engine/JPU``) that HoneyGUI
uses for hardware JPEG decode; the framework also carries a software decoder
(``realgui/3rd/tjpgd``). ``CONFIG_REALTEK_H264_DECODER`` plus
``CONFIG_REALTEK_H264BSD`` bring in the H.264 software decoder behind the video
widgets - drop both if the UI has no video, ``samples/honeygui`` leaves them off
for that reason.

**Do not enable ``CONFIG_REALTEK_BUILD_EXAMPLE_*_WIDGET`` in this application.**
Every HoneyGUI example exports its own ``app_init`` through
``GUI_INIT_APP_EXPORT``, which collides at link time with the one in
``ui_design/src/dashboardEntry.c`` (duplicate ``__honeygui_init_app_init``) and
would create a second view on the GUI root. The application owns the entry
point; widget examples belong to ``samples/honeygui``.

Bluetooth
=========

.. code-block:: none

   CONFIG_REALTEK_SUBSYS_BLE_MGR=y
   CONFIG_RTK_BT_BREDR=y
   CONFIG_RTK_BR_PROFILE_A2DP=y
   CONFIG_RTK_BR_PROFILE_AVRCP=y
   CONFIG_RTK_BR_PROFILE_HFP=y
   CONFIG_RTK_BR_PROFILE_PAN=y

``CONFIG_RTK_BT_BREDR`` selects the BTM, SYSM and Remote subsystems; the four
profile switches depend on it, so switching BR/EDR off turns them off too.
Turn off the profiles you do not need - each one pulls a codec or protocol stack
into the image.

Memory
======

.. code-block:: none

   CONFIG_MAIN_STACK_SIZE=4096
   CONFIG_HEAP_MEM_POOL_SIZE=2048

The GUI server does not run as a Zephyr thread: ``port_thread_create()`` in
``port/ui/gui_port_os.c`` forwards to the Realtek ROM ``os_task_create``, and
``gui_server_init()`` asks for a 10 KB stack there. ``CONFIG_MAIN_STACK_SIZE``
therefore only covers ``main()``, and ``CONFIG_HEAP_MEM_POOL_SIZE`` only the
Zephyr heap. The GUI frame buffers live in PSRAM, which is why
``app_lower_init.c`` brings the OPI partitions up before ``gui_server_init()``.

Input
*****

Keys are declared in ``boards/rtl87x3g_evb.overlay`` under ``gpio_keys`` and
registered by ``port/ui/gui_port_indev.c``. The names there must match the
``strcmp`` targets in ``ui_design/src/callbacks/DashboardMain_callbacks.c``.

===========  ==================  ==========================================  ========
Key          Pad                 Source                                      Status
===========  ==================  ==========================================  ========
``Back``     MFB                 ``mfb_api`` (dedicated pad, not a GPIO)     active
``Power``    P0_0 (ADC_0)        ``gpio-keys`` key2                          active
``Home``     P3_0 (PA23)         ``gpio-keys`` key1                          disabled
``Menu``     P3_1 (PA24)         ``gpio-keys`` key3                          disabled
===========  ==================  ==========================================  ========

Disabled keys are not registered: ``gui_port_indev.c`` gates each entry on
``DT_NODE_HAS_STATUS``, so the key table shrinks to whatever the device tree
enables. The MFB path debounces in a 30 ms ``k_work`` because the pad only
delivers an edge interrupt.

The touch panel (CHSC6417) is currently out of the build. Its SCL pad is P0_0,
the same pad as the Power key, so the two cannot coexist: ``i2c1`` is
``status = "disabled"``, ``CONFIG_REALTEK_TOUCH_CHSC6417_ZEPHYR`` is not set and
the overlay carries the instructions for restoring both once the key moves to
another pad.

Bluetooth services
******************

The device advertises as ``BLE_NAVI`` and registers:

* DIS and BAS plus the generic simple-BLE service, from
  ``subsys/bluetooth/gatt_profile`` inside ``realtek-app``.
* A custom navigation-projection service, UUID ``0xFFD0``, with four
  characteristics: ``0xFFD1`` control write, ``0xFFD2`` control notify,
  ``0xFFD3`` data write-without-response, ``0xFFD4`` data notify.

The projection protocol is two layers: L1 frames (sync ``0xAB``, control,
length, CRC16, sequence) and L2 commands (``OPEN`` / ``ACK`` / ``CLOSE`` /
``ERROR`` / ``FRAME`` / ``CREDIT`` / ``REPORT``). A session negotiates geometry,
fps, quality and MTU in ``OPEN``, then streams JPEG frames up to 128 KB into a
triple buffer that the GUI reads latest-frame-wins. See
``src/ble/navi_projection_service.h`` for the full constant list and
``NAVI_QR_URL_BASE`` for the companion-app QR link.

.. note::

   ``src/ble/`` is compiled but not started yet: ``ble_task_init()`` has no
   caller, and ``navi_projection_init()`` is registering with a ``NULL`` frame
   callback. Call ``ble_task_init()`` after ``app_system_lower_init()`` and pass
   a real ``navi_frame_ready_cb_t`` to get frames on the panel.

UI workflow
***********

``ui_design/`` is exported by HoneyGUI Visual Designer and is its own git
repository (with its own ``.gitignore``), so it is intentionally excluded from
the sub-directory scan in the top-level ``CMakeLists.txt`` - the build wiring
lives in the application to avoid leaving untracked files inside that
repository. Edits belong in a commit made inside ``ui_design/``.

The generated tree is:

* ``src/dashboardEntry.c`` - ``GUI_INIT_APP_EXPORT`` entry, romfs mount, view
  creation. The ``@protected`` blocks survive a re-export.
* ``src/ui/DashboardMain_ui.c`` - the widget tree built from
  ``ui/DashboardMain.hml``.
* ``src/callbacks/`` - widget event handlers.
* ``src/user/`` - application logic: data refresh, media card, stream
  transport, shell command.

After editing the UI in the designer, rebuild the romfs and flash the resource
region again (see `Flashing`_); recompiling alone shows the old assets. Note
that ``project.json`` declares a 1024x600 design canvas while the panel is
410x502 and that geometry is fixed by ``port/ui/gui_port_dc.c`` - check the
layout on the target after an export rather than trusting the designer canvas.

Notes
*****

* ``ui_design/`` is currently untracked in the dashboard repository, and
  ``build/`` / ``bin/`` are gitignored, so a fresh clone has no exported UI and
  the build prints ``ui_design: no generated sources ... UI will not be built``.
  Without ``ui_design/src`` the firmware boots to an empty root node.
* ``port/flashdb`` and ``port/posix`` return early when their ``component/``
  trees are missing; the build prints a ``skip`` status line and continues. That
  is expected, not a failure.
* ``flash_map.h`` and ``mp.ini`` describe the same flash package. Editing one
  without the other shifts the app image or the OTA bank and the bootloader will
  refuse the image.
