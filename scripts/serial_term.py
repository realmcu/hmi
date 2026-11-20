#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
serial_term —— 一个"打开时绝不拉低 RTS/DTR"的串口终端工具

为什么要重写一个:
    绝大多数串口工具(PuTTY 流控None、pyserial 默认构造、各种 SerialAssistant)
    在 open 串口的瞬间会 assert RTS(以及 DTR)。在本工程的 RTL87X3G 硬件上,
    RTS 是低有效复位线 —— assert RTS = 物理低电平 = 芯片被摁住复位。于是"一打开
    终端芯片就复位/跑飞"。

    pyserial 的 Serial(port=...) 构造会立即 open,且 open 时默认 _rts_state=True
    (assert),所以即使事后再 ser.rts=False,打开那一下仍然把 RTS 拉低了一次。

    本工具的做法:空构造 Serial() -> 先把 rts/dtr 状态预设为 False -> 再 open()。
    这样从打开的第一刻起 RTS/DTR 就保持 deassert(物理高电平 = 不复位),全程不动。

快捷键(终端运行时):
    Ctrl + ]      呼出命令菜单
    其它任意键    原样作为字节发送到串口

命令菜单里:
    o   打开串口(连接) —— 关闭状态下用
    c   关闭串口(断开,释放端口给别的程序,如 mpcli 下载) —— 不退出程序
    l   开始/停止 记录 log(把接收到的数据原样存到文件)
    i   打印当前状态
    q   退出
    回车 返回终端继续收发

典型用法 —— 看 log 与下载共用一个端口:
    1) 终端里看芯片 log
    2) Ctrl+] -> c  关闭串口,释放 COMx
    3) 用 mpcli 下载固件(此时端口空闲)
    4) Ctrl+] -> o  重新打开,继续看 log(全程不退出终端)

用法:
    python serial_term.py COM7
    python serial_term.py COM7 -b 2000000
    python serial_term.py COM7 -b 115200 --crlf
"""

import os
import sys
import time
import threading
import argparse

import serial

try:
    import msvcrt  # per-character keyboard read on Windows
    _HAS_MSVCRT = True
except ImportError:
    _HAS_MSVCRT = False


ESC_KEY = '\x1d'  # Ctrl + ]  as the command-lead key (telnet convention)

# The Windows console prefixes a function key with \x00 or \xe0 plus a scan-code char;
# here that is translated into a VT100/ANSI escape sequence for the port, so the target
# shell (readline / linenoise and the like) recognizes "previous command", as PuTTY does.
_FN_KEY_MAP = {
    'H': '\x1b[A',   # Up
    'P': '\x1b[B',   # Down
    'M': '\x1b[C',   # Right
    'K': '\x1b[D',   # Left
    'G': '\x1b[H',   # Home
    'O': '\x1b[F',   # End
    'R': '\x1b[2~',  # Insert
    'S': '\x1b[3~',  # Delete
    'I': '\x1b[5~',  # PageUp
    'Q': '\x1b[6~',  # PageDown
}


class SerialTerm(object):
    def __init__(self, port, baud, crlf=False):
        self.port = port
        self.baud = baud
        self.crlf = crlf          # whether to send \r\n on Enter (otherwise only \r)
        self.ser = None
        self._running = False     # whether the main loop runs (False only on q quit)
        self._connected = False   # whether the serial port is currently open
        self._rx_thread = None
        self._log_file = None     # log file handle being recorded (None = not recording)
        self._log_path = None

    # ---- connect: open the port + start the rx thread, never assert RTS/DTR ----
    def connect(self):
        if self._connected:
            sys.stdout.write("[已经是连接状态]\r\n")
            return True
        try:
            ser = serial.Serial()            # empty construct, not open yet, control lines untouched
            ser.port = self.port
            ser.baudrate = self.baud
            ser.bytesize = serial.EIGHTBITS
            ser.parity = serial.PARITY_NONE
            ser.stopbits = serial.STOPBITS_ONE
            ser.rtscts = False               # disable HW flow control, or the driver takes over RTS
            ser.dsrdtr = False
            ser.xonxoff = False
            ser.timeout = 0.05               # read timeout so the rx thread can poll flags
            ser.rts = False                  # key: pin the state to deassert before open
            ser.dtr = False
            ser.open()                       # configure control lines from the preset -> no reset
            ser.rts = False                  # extra safety
            ser.dtr = False
        except (serial.SerialException, OSError) as err:
            sys.stdout.write("[打开 %s 失败: %s]\r\n" % (self.port, err))
            sys.stdout.flush()
            return False
        self.ser = ser
        self._connected = True
        self._rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
        self._rx_thread.start()
        sys.stdout.write("[已打开 %s @ %d]\r\n" % (self.port, self.baud))
        sys.stdout.flush()
        return True

    # ---- disconnect: stop the rx thread + close the port, without exiting ----
    def disconnect(self):
        # Test self.ser rather than _connected: when the rx thread dies abnormally
        # it sets _connected False but the handle remains, so the port must still be reclaimed.
        if self.ser is None:
            sys.stdout.write("[当前未连接]\r\n")
            sys.stdout.flush()
            return
        self._connected = False
        t = self._rx_thread
        # do not join yourself -- if this path is ever reached from the rx thread it deadlocks
        if t is not None and t is not threading.current_thread():
            t.join(timeout=1)
        self._rx_thread = None
        try:
            if self.ser.is_open:
                self.ser.close()
        except (serial.SerialException, OSError):
            pass
        self.ser = None
        sys.stdout.write("[已关闭 %s,端口已释放]\r\n" % self.port)
        sys.stdout.flush()

    # ---- start/stop logging ----
    def toggle_log(self):
        if self._log_file is None:
            # A port name is normally "COM7" and fine, but pyserial also accepts "\\.\COM27"
            # with backslashes, which would blow up a file name, so sanitize it first.
            safe_port = self.port.replace('\\', '_').replace('/', '_').replace(':', '_')
            fname = "serial_%s_%s.log" % (safe_port, time.strftime("%Y%m%d_%H%M%S"))
            try:
                # buffering=0: unbuffered, each chunk goes straight to disk -- closing the window loses nothing
                f = open(fname, "ab", buffering=0)
            except OSError as err:
                sys.stdout.write("[创建 log 文件失败: %s]\r\n" % err)
                sys.stdout.flush()
                return
            self._log_file = f
            self._log_path = os.path.abspath(fname)
            sys.stdout.write("[开始记录 log -> %s]\r\n" % self._log_path)
        else:
            f = self._log_file
            self._log_file = None     # drop the reference first so the rx thread stops writing
            try:
                f.flush()
                f.close()
            except OSError:
                pass
            sys.stdout.write("[停止记录 log,已保存 -> %s]\r\n" % self._log_path)
            self._log_path = None
        sys.stdout.flush()

    # ---- rx thread ----
    def _rx_loop(self):
        while self._running and self._connected:
            try:
                n = self.ser.in_waiting
                data = self.ser.read(n if n else 1)
            except (serial.SerialException, OSError, AttributeError, TypeError):
                if self._running and self._connected:
                    sys.stdout.write("\r\n[串口读异常,连接已断开]\r\n")
                    sys.stdout.flush()
                self._connected = False
                break
            if not data:
                continue
            # Write binary straight to stdout.buffer, bypassing the text-layer codec -- so
            # any byte the device sends (ANSI colors \x1b[...m, cursor control, etc.) passes
            # through to the terminal unchanged instead of being eaten into "?" by cp936/utf-8.
            try:
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()
            except (OSError, ValueError, AttributeError):
                # if stdout is redirected to an object without .buffer, fall back to text writes
                sys.stdout.write(data.decode("latin-1"))
                sys.stdout.flush()
            # record raw bytes to the log (local reference, avoids racing with stop)
            f = self._log_file
            if f is not None:
                try:
                    f.write(data)
                except (OSError, ValueError):
                    pass

    def status(self):
        log_state = ("记录中 -> %s" % self._log_path) if self._log_file else "未记录"
        if not self._connected or self.ser is None:
            return "[端口=%s 波特率=%d  未连接(Ctrl+] -> o 打开)  log:%s]" % (
                self.port, self.baud, log_state)
        return "[端口=%s 波特率=%d  已连接  log:%s]" % (self.port, self.ser.baudrate, log_state)

    # ---- command menu (entered with Ctrl+]) ----
    def _command_menu(self):
        sys.stdout.write(
            "\r\n--- 命令: [o]打开 [c]关闭 [l]记录log [i]状态 [q]退出 [回车]返回 ---\r\n")
        sys.stdout.flush()
        ch = msvcrt.getwch()
        if ch in ('o', 'O'):
            self.connect()
        elif ch in ('c', 'C'):
            self.disconnect()
        elif ch in ('l', 'L'):
            self.toggle_log()
        elif ch in ('i', 'I'):
            sys.stdout.write(self.status() + "\r\n")
            sys.stdout.flush()
        elif ch in ('q', 'Q'):
            sys.stdout.write("[退出]\r\n")
            sys.stdout.flush()
            self._running = False
        # anything else (including Enter) just returns to the terminal

    # ---- main loop ----
    def run(self):
        self._running = True
        self.connect()  # connect once on startup (still enters the terminal on failure, retry with Ctrl+] -> o)

        sys.stdout.write(self.status() + "\r\n")
        sys.stdout.write("按 Ctrl+] 进入命令菜单(o打开 / c关闭 / l记录log / q退出)。\r\n")
        sys.stdout.flush()

        while self._running:
            if not msvcrt.kbhit():
                time.sleep(0.005)
                continue
            ch = msvcrt.getwch()
            if ch == ESC_KEY:                 # Ctrl + ]
                self._command_menu()
                continue
            if ch in ('\x00', '\xe0'):        # function/arrow key prefix, followed by a scan code
                # the scan code must be consumed, or the next getwch gets a lone character
                scan = msvcrt.getwch() if msvcrt.kbhit() else ''
                if not self._connected:
                    continue
                payload = _FN_KEY_MAP.get(scan)
                if payload is None:           # unmapped function key, ignore silently
                    continue
                try:
                    self.ser.write(payload.encode("latin-1"))
                except (serial.SerialException, OSError, AttributeError):
                    sys.stdout.write("\r\n[串口写异常,连接已断开]\r\n")
                    self._connected = False
                continue
            # ordinary character: ignore silently when disconnected, nothing to send to
            if not self._connected:
                continue
            if ch == '\r':
                payload = "\r\n" if self.crlf else "\r"
            else:
                payload = ch
            try:
                self.ser.write(payload.encode("latin-1"))
            except (serial.SerialException, OSError, AttributeError):
                sys.stdout.write("\r\n[串口写异常,连接已断开]\r\n")
                self._connected = False

    def cleanup(self):
        self._running = False
        self.disconnect()
        if self._log_file is not None:
            try:
                self._log_file.flush()
                self._log_file.close()
            except OSError:
                pass
            self._log_file = None


def main():
    if not _HAS_MSVCRT:
        sys.stderr.write("本工具的交互终端目前仅支持 Windows。\n")
        return 1

    parser = argparse.ArgumentParser(
        description="打开时绝不拉低 RTS/DTR 的串口终端(适配 RTL87X3G 复位线极性)")
    parser.add_argument("port", help="串口名,例如 COM7")
    parser.add_argument("-b", "--baud", type=int, default=2000000, help="波特率(默认 2000000)")
    parser.add_argument("--crlf", action="store_true", help="回车发送 \\r\\n(默认只发 \\r)")
    args = parser.parse_args()

    term = SerialTerm(args.port, args.baud, crlf=args.crlf)
    try:
        term.run()
    except KeyboardInterrupt:
        pass
    finally:
        term.cleanup()
        sys.stdout.write("\r\n已退出。\r\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
