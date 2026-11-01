"""Log RLHT thermocouple readings from GET_STATE at 100 ms for SECONDS.

Prints every reading more than 3 C from the median of the last 31 polls of
that channel, every invalid reading, and every change of the fault flag,
then a summary. Read-only: sends no commands.

  python tclog.py SECONDS
"""

import statistics
import sys
import time
from collections import deque

import rlht_bench as b

INVALID = -32768


def main():
    seconds = float(sys.argv[1])
    hist = {1: deque(maxlen=31), 2: deque(maxlen=31)}
    stats = {1: [], 2: []}
    outliers = {1: 0, 2: 0}
    invalid = {1: 0, 2: 0}
    polls = fails = 0
    last_fault = None
    end = time.monotonic() + seconds
    with b.LinuxBus("/dev/i2c-1") as bus:
        c = b.Controller(bus)
        print(b.stamp(), "start")
        while time.monotonic() < end:
            try:
                q = c.query(b.ADDR, type_id=b.r.RLHT_TYPE_ID, opcode=b.r.RLHT_OP_GET_STATE)
                s = b.r.rlht_parse_state_payload(q.data)
            except Exception as e:
                fails += 1
                print(b.stamp(), "poll failed", repr(e))
                time.sleep(0.1)
                continue
            polls += 1
            fault = 1 if s.flags & b.r.RLHT_FLAG_FAULT else 0
            if fault != last_fault:
                print(b.stamp(), f"fault={fault} len={len(q.data)} data={q.data.hex()} q={q!r}")
                last_fault = fault
            for ch, raw in ((1, s.t1_deci_c), (2, s.t2_deci_c)):
                if raw == INVALID:
                    invalid[ch] += 1
                    print(b.stamp(), f"t{ch} invalid ({raw})")
                    continue
                t = raw / 10
                h = hist[ch]
                if len(h) >= 5 and abs(t - statistics.median(h)) > 3.0:
                    outliers[ch] += 1
                    print(b.stamp(), f"t{ch}={t} median={statistics.median(h)}")
                h.append(t)
                stats[ch].append(t)
            time.sleep(0.1)
        print(b.stamp(), "end")
        b.show_faults(c)
    print(f"polls={polls} failed={fails}")
    for ch in (1, 2):
        v = stats[ch]
        if v:
            print(
                f"t{ch}: n={len(v)} min={min(v)} max={max(v)} "
                f"median={statistics.median(v)} outliers={outliers[ch]} invalid={invalid[ch]}"
            )


if __name__ == "__main__":
    main()
