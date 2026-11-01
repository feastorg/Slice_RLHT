"""Poll RLHT GET_STATE every 100 ms for SECONDS and print a line whenever t1,
the relays or the fault flag change. Read-only.

  python f4log.py SECONDS
"""

import sys
import time

import rlht_bench as b


def main():
    end = time.monotonic() + float(sys.argv[1])
    last = None
    with b.LinuxBus("/dev/i2c-1") as bus:
        c = b.Controller(bus)
        while time.monotonic() < end:
            try:
                s = b.get_state(c)
            except Exception as e:
                print(b.stamp(), "poll failed", repr(e))
                time.sleep(0.1)
                continue
            r1 = 1 if s.flags & b.r.RLHT_FLAG_RELAY1_ON else 0
            fault = 1 if s.flags & b.r.RLHT_FLAG_FAULT else 0
            now = (s.t1_deci_c, r1, fault)
            if now != last:
                print(b.stamp(), f"t1={s.t1_deci_c / 10} relay1={r1} fault={fault} on1={s.on1_ms} flags=0x{s.flags:02x}")
                last = now
            time.sleep(0.1)
        b.show_faults(c)


if __name__ == "__main__":
    main()
