#!/usr/bin/env python3
"""用 baostock 拉 A股 K线，导出 desk-quant 统一 CSV（ts,open,high,low,close,volume,amount）。

用法：
    python3 fetch_baostock.py sh.600000 5 2025-01-01 2025-12-31
    参数：代码  周期(5/15/30/60分钟, d日线)  开始  结束
"""
import csv
import os
import sys
from datetime import datetime
import baostock as bs


def parse_ts(date, time):
    """兼容 baostock 的 time 字段：完整时间戳 / 纯时分秒 / 空（日线）"""
    if time and len(time) == 14:            # 20250101100000
        return int(datetime.strptime(time, "%Y%m%d%H%M%S").timestamp() * 1000)
    if time and len(time) == 6:             # 100000
        return int(datetime.strptime(date + time, "%Y-%m-%d%H%M%S").timestamp() * 1000)
    return int(datetime.strptime(date, "%Y-%m-%d").timestamp() * 1000)


if __name__ == "__main__":
    code  = sys.argv[1] if len(sys.argv) > 1 else "sh.600000"
    freq  = sys.argv[2] if len(sys.argv) > 2 else "5"
    start = sys.argv[3] if len(sys.argv) > 3 else "2025-01-01"
    end   = sys.argv[4] if len(sys.argv) > 4 else "2025-12-31"

    os.makedirs("data", exist_ok=True)

    bs.login()
    rs = bs.query_history_k_data_plus(
        code, "date,time,open,high,low,close,volume,amount",
        start_date=start, end_date=end, frequency=freq, adjustflag="2")  # 2=前复权

    out_path = f"data/{code.replace('.', '')}-{freq}m.csv"
    with open(out_path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["ts", "open", "high", "low", "close", "volume", "amount"])
        while rs.next():
            row = rs.get_row_data()
            # row: date,time,open,high,low,close,volume,amount
            w.writerow([parse_ts(row[0], row[1]), row[2], row[3], row[4], row[5], row[6], row[7]])
    bs.logout()
    print(f"saved -> {out_path}")