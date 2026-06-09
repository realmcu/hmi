import argparse
import binascii
import os
import socket
import struct
import sys
import time
from pathlib import Path


def crc32_file(path: Path) -> int:
    crc = 0
    with path.open("rb") as handle:
        while True:
            chunk = handle.read(64 * 1024)
            if not chunk:
                break
            crc = binascii.crc32(chunk, crc)
    return crc & 0xFFFFFFFF


def recv_line(sock: socket.socket, timeout: float) -> str:
    sock.settimeout(timeout)
    data = bytearray()
    while True:
        ch = sock.recv(1)
        if not ch:
            raise TimeoutError("connection closed before newline")
        if ch == b"\n":
            break
        if ch != b"\r":
            data.extend(ch)
    return data.decode("utf-8", errors="replace")


def transfer_tcp(host: str, port: int, file_path: Path, timeout: float) -> int:
    file_size = file_path.stat().st_size
    file_crc = crc32_file(file_path)
    header = f"Z2FT1 TCP {file_path.name} {file_size} {file_crc:08x}\n".encode("ascii")

    start = time.perf_counter()
    with socket.create_connection((host, port), timeout=timeout) as sock:
        sock.sendall(header)
        ready = recv_line(sock, timeout)
        print(f"board: {ready}")
        if "READY" not in ready:
            raise RuntimeError("board did not accept TCP transfer")

        with file_path.open("rb") as handle:
            while True:
                chunk = handle.read(64 * 1024)
                if not chunk:
                    break
                sock.sendall(chunk)

        result = recv_line(sock, timeout)
    elapsed = time.perf_counter() - start
    speed_mbps = (file_size * 8.0 / elapsed / 1_000_000.0) if elapsed > 0 else 0.0
    print(f"host: bytes={file_size} crc32={file_crc:08x} elapsed={elapsed:.3f}s mbps={speed_mbps:.3f}")
    print(f"board: {result}")
    return 0


def transfer_udp(host: str, port: int, file_path: Path, timeout: float, payload_size: int) -> int:
    file_size = file_path.stat().st_size
    file_crc = crc32_file(file_path)
    header = f"Z2FT1 UDP {file_path.name} {file_size} {payload_size} {file_crc:08x}\n".encode("ascii")
    end_packet = b"Z2FT1 END\n"
    seq = 0

    start = time.perf_counter()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(timeout)
        sock.sendto(header, (host, port))
        ready, _ = sock.recvfrom(512)
        ready_text = ready.decode("utf-8", errors="replace").strip()
        print(f"board: {ready_text}")
        if "READY" not in ready_text:
            raise RuntimeError("board did not accept UDP transfer")

        with file_path.open("rb") as handle:
            while True:
                payload = handle.read(payload_size)
                if not payload:
                    break
                packet = struct.pack("<I", seq) + payload
                sock.sendto(packet, (host, port))
                seq += 1

        sock.sendto(end_packet, (host, port))
        result, _ = sock.recvfrom(1024)
    elapsed = time.perf_counter() - start
    speed_mbps = (file_size * 8.0 / elapsed / 1_000_000.0) if elapsed > 0 else 0.0
    print(f"host: bytes={file_size} crc32={file_crc:08x} packets={seq} elapsed={elapsed:.3f}s mbps={speed_mbps:.3f}")
    print(f"board: {result.decode('utf-8', errors='replace').strip()}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Send a file to z2plus TCP/UDP ATTF receiver and print throughput.")
    parser.add_argument("--mode", choices=["tcp", "udp"], required=True, help="transfer mode")
    parser.add_argument("--host", required=True, help="board IP address")
    parser.add_argument("--port", type=int, required=True, help="board listening port")
    parser.add_argument("--file", required=True, help="file to send")
    parser.add_argument("--timeout", type=float, default=10.0, help="socket timeout in seconds")
    parser.add_argument("--payload-size", type=int, default=1024, help="UDP payload size in bytes")
    args = parser.parse_args()

    file_path = Path(args.file)
    if not file_path.is_file():
        print(f"file not found: {file_path}", file=sys.stderr)
        return 2

    if args.mode == "tcp":
        return transfer_tcp(args.host, args.port, file_path, args.timeout)
    return transfer_udp(args.host, args.port, file_path, args.timeout, args.payload_size)


if __name__ == "__main__":
    raise SystemExit(main())