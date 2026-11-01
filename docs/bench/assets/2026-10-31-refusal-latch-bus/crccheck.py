t = []
for b in range(256):
    c = b
    for _ in range(8):
        c = ((c << 1) ^ 7) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    t.append(c)


def crc(d):
    c = 0
    for x in d:
        c = t[c ^ x]
    return c


# CRC-8 over the observed frame: type 1, opcode 0x80, length 20, mode 0, then 0xFF.
observed = bytes([1, 0x80, 20, 0]) + b"\xff" * 19
print("crc of observed body:", hex(crc(observed)), "(received CRC byte would be 0xff)")
# A good frame cut off after k bytes, the rest read as 0xFF, CRC byte included.
good = bytes([1, 0x80, 20, 0, 0, 0xE9, 0, 0xDF] + [0] * 10 + [0xE8, 3, 0xE8, 3, 9, 0])
for k in range(3, 24):
    body = good[:k] + b"\xff" * (23 - k)
    print(k, "passes" if crc(body) == 0xFF else "fails")
print("type1, any mode byte:", sum(crc(bytes([1, 0x80, 20, b]) + b"\xff" * 19) == 0xFF for b in range(256)), "/256")
print("state after header+00 then n FF:", [hex(crc(bytes([1, 0x80, 20, 0]) + b"\xff" * n)) for n in range(6)])
