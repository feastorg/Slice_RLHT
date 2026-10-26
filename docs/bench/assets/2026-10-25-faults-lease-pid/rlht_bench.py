"""RLHT bench helper for a Pi controller: CRUMBS over /dev/i2c-1 and the
board's USB serial port. Run it with a venv that pins crumbs-i2c 0.16.2 and
bread-crumbs-contracts 0.11.0:

  venv/bin/python rlht_bench.py state

  rlht_bench.py version | caps | state | wdog | ack
  rlht_bench.py mode N | duty D1 D2 | sp T1 T2 | wd MS | clear
                                   duty in percent; sp in degrees C
  rlht_bench.py wdl MS FLAGS       3-byte SET_WATCHDOG (the lease; 1 = commands only)
  rlht_bench.py pid KP KI KD       12-byte SET_PID, the same gains on both heaters
  rlht_bench.py pid6 KP KI KD      6-byte SET_PID (u8 gains x10)
  rlht_bench.py faults | fclear    GET_FAULTS; CLEAR_FAULTS
  rlht_bench.py raw OP HEX         any SET opcode with a hex payload
  rlht_bench.py watch SECONDS      poll GET_STATE every 200 ms, print each change
                                   of relays, fault flag, on-times or e-stop, then
                                   GET_WATCHDOG and GET_FAULTS
  rlht_bench.py ser LINE...        send serial lines, print the replies
  rlht_bench.py flood SECONDS      invalid serial lines nonstop while polling
                                   GET_STATE every 20 ms; prints each change
                                   of the e-stop flag or count and any failed
                                   poll
  rlht_bench.py prep               stop the port from resetting the board on
                                   every open (stty -hupcl); run once

Every command prints a timestamp. SETs print the GET_ACK that follows.
Address: RLHT_ADDR (default 19). Port: RLHT_PORT, or the only
ttyACM*/ttyUSB* present.
"""

import os
import subprocess
import sys
import time

from bread_crumbs_contracts import rlht_ops as r
from crumbs_i2c import Controller, LinuxBus, Message

ADDR = int(os.environ.get("RLHT_ADDR", "19"), 0)


def default_port():
    # A Nano Every enumerates as ttyACM*, a classic Nano as ttyUSB*. Use the
    # only one present; with none or several, RLHT_PORT has to say which.
    import glob

    ports = sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))
    return ports[0] if len(ports) == 1 else None


PORT = os.environ.get("RLHT_PORT") or default_port()
BREAD_OP_GET_WATCHDOG = 0x7D
BREAD_OP_SET_WATCHDOG = 0x7E
BREAD_OP_CLEAR_WATCHDOG_TRIP = 0x7C
BREAD_OP_GET_CAPS = 0x7F
BREAD_OP_GET_FAULTS = 0x7A
BREAD_OP_CLEAR_FAULTS = 0x7B
FAULT_NAMES = {
    0x01: "H1_OVERTEMP", 0x02: "H1_NO_RISE", 0x04: "H1_SENSOR",
    0x08: "H2_OVERTEMP", 0x10: "H2_NO_RISE", 0x20: "H2_SENSOR",
}


def stamp():
    return time.strftime("%H:%M:%S") + f".{int(time.time() * 1000) % 1000:03d}"


def get_state(c):
    return r.rlht_parse_state_payload(
        c.query(ADDR, type_id=r.RLHT_TYPE_ID, opcode=r.RLHT_OP_GET_STATE).data
    )


def show_state(c):
    s = get_state(c)
    estop = 1 if s.flags & r.RLHT_FLAG_ESTOP else 0
    relay1 = 1 if s.flags & r.RLHT_FLAG_RELAY1_ON else 0
    relay2 = 1 if s.flags & r.RLHT_FLAG_RELAY2_ON else 0
    fault = 1 if s.flags & r.RLHT_FLAG_FAULT else 0
    print(
        f"{stamp()} state mode={s.mode} estop={estop} fault={fault} relays={relay1},{relay2} "
        f"t={s.t1_deci_c / 10},{s.t2_deci_c / 10} sp={s.sp1_deci_c / 10},{s.sp2_deci_c / 10} "
        f"on={s.on1_ms},{s.on2_ms} period={s.period1_ms},{s.period2_ms} "
        f"tc={s.tc1},{s.tc2} estop_count={s.estop_count}"
    )
    return s


def send(c, opcode, payload):
    c.send(ADDR, Message(type_id=r.RLHT_TYPE_ID, opcode=opcode, data=payload))
    time.sleep(0.01)
    a = c.read_ack(ADDR)
    print(f"{stamp()} sent 0x{opcode:02X} {payload.hex()} -> ack {a}")


def serial_port():
    import serial

    if not PORT:
        sys.exit("no single serial port found; set RLHT_PORT")
    return serial.Serial(PORT, 115200, timeout=0.2)


def main():
    cmd, *args = sys.argv[1:]
    if cmd == "prep":
        if not PORT:
            sys.exit("no single serial port found; set RLHT_PORT")
        subprocess.run(["stty", "-F", PORT, "-hupcl"], check=True)
        print(f"{stamp()} {PORT}: hupcl off")
        return
    if cmd == "ser":
        with serial_port() as p:
            for line in args:
                p.write(line.encode() + b"\n")
                time.sleep(0.15)
                print(f"{stamp()} > {line}")
                for reply in p.read(4096).decode(errors="replace").splitlines():
                    print(f"{stamp()} < {reply}")
        return
    with LinuxBus("/dev/i2c-1") as bus:
        c = Controller(bus)
        if cmd == "version":
            print(stamp(), c.get_version(ADDR, type_id=r.RLHT_TYPE_ID))
        elif cmd == "caps":
            q = c.query(ADDR, type_id=r.RLHT_TYPE_ID, opcode=BREAD_OP_GET_CAPS)
            print(stamp(), "caps", q.data.hex())
        elif cmd == "wdog":
            q = c.query(ADDR, type_id=r.RLHT_TYPE_ID, opcode=BREAD_OP_GET_WATCHDOG)
            print(stamp(), "wdog [armed][timeout_ms][tripped][trips]", q.data.hex())
        elif cmd == "state":
            show_state(c)
        elif cmd == "ack":
            print(stamp(), c.read_ack(ADDR))
        elif cmd == "mode":
            send(c, r.RLHT_OP_SET_MODE, r.rlht_send_set_mode(int(args[0])))
        elif cmd == "duty":
            send(c, r.RLHT_OP_SET_OPEN_DUTY, r.rlht_send_set_open_duty(int(args[0]), int(args[1])))
        elif cmd == "sp":
            send(
                c,
                r.RLHT_OP_SET_SETPOINTS,
                r.rlht_send_set_setpoints(round(float(args[0]) * 10), round(float(args[1]) * 10)),
            )
        elif cmd == "wd":
            send(c, BREAD_OP_SET_WATCHDOG, r.rlht_send_set_watchdog(int(args[0])))
        elif cmd == "clear":
            send(c, BREAD_OP_CLEAR_WATCHDOG_TRIP, r.rlht_send_clear_watchdog_trip())
        elif cmd == "flood":
            flood(c, float(args[0]))
        elif cmd == "wdl":
            send(c, BREAD_OP_SET_WATCHDOG, r.rlht_send_set_watchdog_lease(int(args[0]), int(args[1], 0)))
        elif cmd == "pid":
            g = [round(float(x) * 1000) for x in args[:3]]
            send(c, r.RLHT_OP_SET_PID, r.rlht_send_set_pid_x1000(*g, *g))
        elif cmd == "pid6":
            g = [round(float(x) * 10) for x in args[:3]]
            send(c, r.RLHT_OP_SET_PID, r.rlht_send_set_pid_x10(*g, *g))
        elif cmd == "faults":
            show_faults(c)
        elif cmd == "fclear":
            send(c, BREAD_OP_CLEAR_FAULTS, r.rlht_send_clear_faults())
        elif cmd == "raw":
            send(c, int(args[0], 0), bytes.fromhex(args[1] if len(args) > 1 else ""))
        elif cmd == "watch":
            watch(c, float(args[0]))
        else:
            sys.exit(f"unknown command {cmd}")


def show_faults(c):
    q = c.query(ADDR, type_id=r.RLHT_TYPE_ID, opcode=BREAD_OP_GET_FAULTS)
    bits = q.data[0] if q.data else 0
    names = [n for b, n in FAULT_NAMES.items() if bits & b] or ["none"]
    print(f"{stamp()} faults 0x{bits:02x} {' '.join(names)}")


def watch(c, seconds):
    # GET_STATE only: under a commands-only lease these polls do not refresh
    # the watchdog, which is what the lease step checks.
    end = time.monotonic() + seconds
    last = None
    while time.monotonic() < end:
        try:
            s = get_state(c)
            now = (s.flags, s.on1_ms, s.on2_ms)
            if now != last:
                show_state(c)
                last = now
        except Exception as e:
            print(f"{stamp()} poll failed: {e!r}")
        time.sleep(0.2)
    q = c.query(ADDR, type_id=r.RLHT_TYPE_ID, opcode=BREAD_OP_GET_WATCHDOG)
    print(stamp(), "wdog [armed][timeout_ms][tripped][trips]", q.data.hex())
    show_faults(c)


def flood(c, seconds):
    # Each invalid line makes the firmware print its error reply, so the
    # replies alone keep its transmit buffer full.
    with serial_port() as p:
        p.timeout = 0
        p.write_timeout = 0
        end = time.monotonic() + seconds
        last = None
        lines = polls = fails = 0
        while time.monotonic() < end:
            try:
                p.write(b"x\n" * 8)
                lines += 8
            except Exception:
                pass
            p.reset_input_buffer()
            try:
                s = get_state(c)
                polls += 1
                now = (1 if s.flags & r.RLHT_FLAG_ESTOP else 0, s.estop_count)
                if now != last:
                    print(f"{stamp()} estop={now[0]} estop_count={now[1]}")
                    last = now
            except Exception as e:
                fails += 1
                print(f"{stamp()} poll failed: {e!r}")
            time.sleep(0.02)
        print(f"{stamp()} done: {lines} lines sent, {polls} polls, {fails} failed")


if __name__ == "__main__":
    main()
