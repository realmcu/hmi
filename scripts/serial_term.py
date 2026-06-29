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
    import msvcrt  # Windows 键盘逐字符读取
    _HAS_MSVCRT = True
except ImportError:
    _HAS_MSVCRT = False


ESC_KEY = '\x1d'  # Ctrl + ]  作为命令引导键(沿用 telnet 习惯)

# Windows 控制台读到功能键时,会先给 \x00 或 \xe0 前缀,再给一个扫描码字符。
# 这里把它翻译成 VT100/ANSI 转义序列发到串口 —— 这样目标设备的 shell
# (readline / linenoise 一类) 才认得 "上一条命令"。PuTTY 默认就是这么干的。
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
        self.crlf = crlf          # 回车时是否发 \r\n(否则只发 \r)
        self.ser = None
        self._running = False     # 程序主循环是否在跑(q 退出才置 False)
        self._connected = False   # 串口当前是否打开
        self._rx_thread = None
        self._log_file = None     # 正在记录的 log 文件句柄(None=未记录)
        self._log_path = None

    # ---- 连接:打开串口 + 启动接收线程,全程不 assert RTS/DTR ----
    def connect(self):
        if self._connected:
            sys.stdout.write("[已经是连接状态]\r\n")
            return True
        try:
            ser = serial.Serial()            # 空构造,此时尚未打开,不会动控制线
            ser.port = self.port
            ser.baudrate = self.baud
            ser.bytesize = serial.EIGHTBITS
            ser.parity = serial.PARITY_NONE
            ser.stopbits = serial.STOPBITS_ONE
            ser.rtscts = False               # 关闭硬件流控,否则驱动会接管 RTS
            ser.dsrdtr = False
            ser.xonxoff = False
            ser.timeout = 0.05               # 读超时,让接收线程能定期检查标志
            ser.rts = False                  # 关键:open 前就把状态钉成 deassert
            ser.dtr = False
            ser.open()                       # 按预设状态配置控制线 -> 不复位
            ser.rts = False                  # 双保险
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

    # ---- 断开:停接收线程 + 关串口,但不退出程序 ----
    def disconnect(self):
        # 用 self.ser 判断而不是 _connected:接收线程异常退出时会把
        # _connected 置 False 但句柄还在,这种情况下也得把端口收回来。
        if self.ser is None:
            sys.stdout.write("[当前未连接]\r\n")
            sys.stdout.flush()
            return
        self._connected = False
        t = self._rx_thread
        # 不要 join 自己 —— 万一以后从 rx 线程里走到这条路径会真死锁
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

    # ---- 开始/停止记录 log ----
    def toggle_log(self):
        if self._log_file is None:
            # COM 口名一般是 "COM7" 没问题,但 pyserial 也接受 "\\.\COM27"
            # 这种带反斜杠的写法,直接拿来拼文件名会炸,先 sanitize 一下。
            safe_port = self.port.replace('\\', '_').replace('/', '_').replace(':', '_')
            fname = "serial_%s_%s.log" % (safe_port, time.strftime("%Y%m%d_%H%M%S"))
            try:
                # buffering=0:无缓冲,每块直接落盘 —— 即使直接关窗口也几乎不丢 log
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
            self._log_file = None     # 先断开引用,接收线程不再写
            try:
                f.flush()
                f.close()
            except OSError:
                pass
            sys.stdout.write("[停止记录 log,已保存 -> %s]\r\n" % self._log_path)
            self._log_path = None
        sys.stdout.flush()

    # ---- 接收线程 ----
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
            # 直接写二进制到 stdout.buffer,绕过文本层的 codec —— 这样设备发的
            # 任何字节(含 ANSI 颜色 \x1b[...m、光标控制等)都能原样透传到终端,
            # 不会被 cp936/utf-8 解码层吃成 "?"。
            try:
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()
            except (OSError, ValueError, AttributeError):
                # 万一 stdout 被重定向到不支持 .buffer 的对象,降级回文本写
                sys.stdout.write(data.decode("latin-1"))
                sys.stdout.flush()
            # 记录原始字节到 log(取本地引用,避免与停止记录竞争)
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

    # ---- 命令菜单(按 Ctrl+] 进入)----
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
        # 其它(含回车)直接返回终端

    # ---- 主循环 ----
    def run(self):
        self._running = True
        self.connect()  # 启动时自动连接一次(失败也进入终端,可 Ctrl+] -> o 重试)

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
            if ch in ('\x00', '\xe0'):        # 功能键/方向键前缀,后跟扫描码
                # 必须把扫描码读掉,否则下一轮 getwch 会拿到一个孤零零的字符
                scan = msvcrt.getwch() if msvcrt.kbhit() else ''
                if not self._connected:
                    continue
                payload = _FN_KEY_MAP.get(scan)
                if payload is None:           # 未映射的功能键,静默忽略
                    continue
                try:
                    self.ser.write(payload.encode("latin-1"))
                except (serial.SerialException, OSError, AttributeError):
                    sys.stdout.write("\r\n[串口写异常,连接已断开]\r\n")
                    self._connected = False
                continue
            # 普通字符:未连接时静默忽略,避免无端口可发
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
