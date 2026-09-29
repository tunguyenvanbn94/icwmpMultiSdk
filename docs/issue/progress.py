#!/usr/bin/env python3
"""Read the persisted implementation status. No SDK access or build required."""

import argparse
import json
import sys
import time
from pathlib import Path


STATE_FILE = Path(__file__).with_name("implementation-status.json")


def display(state, as_json):
    if as_json:
        print(json.dumps(state, ensure_ascii=False, indent=2))
        return
    print("icwmp multi-SDK — tiến độ thực thi")
    print("Cập nhật:", state["updated_at"])
    print("Phiên:", state["execution"])
    print("Baseline:", state["baseline"])
    print("Đang làm:", state["current"])
    print("Tiếp theo:", state["next"])
    print("\n{:<7} {:<19} {}".format("Phase", "Trạng thái", "Công việc"))
    for phase in state["phases"]:
        print("{:<7} {:<19} {}".format(phase["id"], phase["status"], phase["title"]))
        if phase.get("note"):
            print(" " * 9 + phase["note"])
    print("\nValidation:")
    for name, result in state["validation"].items():
        print("  {}: {}".format(name, result))
    print("\nTrạng thái được lưu bởi phiên làm việc, không phải heartbeat của process AI.")
    print("STATIC_VERIFIED chưa có nghĩa BUILD/BOARD PASS. --watch chỉ đọc lại file.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true", help="Xuất trạng thái máy đọc được")
    parser.add_argument("--watch", type=float, nargs="?", const=2,
                        metavar="SECONDS", help="Tự cập nhật, mặc định mỗi 2 giây")
    args = parser.parse_args()
    if args.watch is not None and not 0.2 <= args.watch <= 3600:
        parser.error("--watch phải từ 0.2 đến 3600 giây")
    if args.json and args.watch:
        parser.error("Dùng --json hoặc --watch, không kết hợp")
    try:
        while True:
            state = json.loads(STATE_FILE.read_text(encoding="utf-8"))
            if args.watch and sys.stdout.isatty():
                print("\033[2J\033[H", end="")
            display(state, args.json)
            if not args.watch:
                return 0
            sys.stdout.flush()
            time.sleep(args.watch)
    except KeyboardInterrupt:
        return 0
    except (OSError, ValueError, KeyError) as error:
        print("Không đọc được trạng thái {}: {}".format(STATE_FILE, error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
