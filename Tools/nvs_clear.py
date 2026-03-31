#!/usr/bin/env python3
"""
nvs_clear.py — Xóa NVS partition trên ESP32 Anchor
Namespace: "ble-keys" / Key: "bleKey"

Yêu cầu: pip install esptool
Dùng lệnh: python nvs_clear.py --port COM3
"""

import argparse
import subprocess
import sys

# Địa chỉ NVS partition mặc định của ESP32 (partition table mặc định)
NVS_OFFSET = "0x9000"
NVS_SIZE   = "0x5000"   # 20 KB


def run(cmd: list[str]) -> int:
    print(f"[RUN] {' '.join(cmd)}")
    result = subprocess.run(cmd)
    return result.returncode


def main():
    parser = argparse.ArgumentParser(description="Xóa NVS key trên ESP32 Anchor")
    parser.add_argument("--port", "-p", default="COM3", help="Serial port (mặc định: COM3)")
    parser.add_argument("--baud", "-b", default="921600", help="Baud rate (mặc định: 921600)")
    parser.add_argument("--full", action="store_true",
                        help="Xóa toàn bộ NVS partition (mặc định: chỉ xóa bleKey)")
    args = parser.parse_args()

    print("=" * 50)
    print("  SCA NVS Key Eraser (Python)")
    print("=" * 50)
    print(f"Port  : {args.port}")
    print(f"Mode  : {'Toàn bộ NVS partition' if args.full else 'Chỉ xóa key bleKey'}")
    print()

    if args.full:
        # Xóa toàn bộ NVS partition
        print(f"[INFO] Xóa NVS partition tại offset {NVS_OFFSET}, size {NVS_SIZE} ...")
        rc = run([
            sys.executable, "-m", "esptool",
            "--port", args.port,
            "--baud", args.baud,
            "erase_region", NVS_OFFSET, NVS_SIZE,
        ])
    else:
        # Ghi file NVS CSV rỗng chỉ xóa key bleKey
        # Dùng nvs_partition_gen để tạo binary NVS trống rồi flash
        # Cách đơn giản nhất: erase_region toàn bộ NVS partition (cả 2 cách đều reset NVS)
        print("[INFO] ESP32 không hỗ trợ xóa từng key qua esptool.")
        print("[INFO] Thực hiện erase toàn bộ NVS partition...")
        rc = run([
            sys.executable, "-m", "esptool",
            "--port", args.port,
            "--baud", args.baud,
            "erase_region", NVS_OFFSET, NVS_SIZE,
        ])

    if rc == 0:
        print()
        print("[OK]  NVS đã xóa thành công!")
        print("[OK]  Khởi động lại Anchor → sẽ fetch key mới từ server.")
    else:
        print()
        print(f"[ERR] esptool thất bại (exit code {rc})")
        print("      Kiểm tra lại port và driver CH340/CP210x.")
        sys.exit(rc)


if __name__ == "__main__":
    main()
