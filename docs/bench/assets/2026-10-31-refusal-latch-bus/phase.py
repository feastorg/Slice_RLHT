"""Do the failed RLHT replies cluster at one phase of a ~300 ms cycle?

For each candidate period near 300 ms, the Rayleigh statistic R (0 = spread
evenly, 1 = all at one phase) of the failure times. The board's clock drifts
against the Pi's, so the best period is searched, not assumed. A control row
uses the polls' own times, which are paced at about 117 ms.

  python phase.py LOGFILE [ADDR]
"""

import cmath
import math
import re
import sys

path = sys.argv[1]
addr = sys.argv[2] if len(sys.argv) > 2 else "19"
times = []
on = False
for line in open(path):
    if "start addr=" in line:
        on = f"addr={addr} " in line
        continue
    if on and ("Error" in line or "passed CRC" in line):
        h, m, s = re.match(r"(\d+):(\d+):([\d.]+)", line).groups()
        times.append((int(h) * 3600 + int(m) * 60 + float(s)) * 1000)


def rayleigh(ts, period):
    z = sum(cmath.exp(2j * math.pi * (t % period) / period) for t in ts) / len(ts)
    return abs(z), (cmath.phase(z) % (2 * math.pi)) / (2 * math.pi) * period


print(f"n={len(times)}")
best = max((rayleigh(times, p / 100), p / 100) for p in range(29000, 31001))
(r, ph), p = best
print(f"best period {p:.2f} ms: R={r:.2f} phase={ph:.0f} ms")
# Chance level: with n uniform phases, R is about 1/sqrt(n); the search over
# 2000 periods raises the expected maximum, so compare with other periods too.
for q in (150.0, 200.0, 250.0, 350.0, 1000.0):
    rr = max(rayleigh(times, q * (1 + d / 10000))[0] for d in range(-100, 101))
    print(f"control period ~{q:.0f} ms: best R={rr:.2f}")
