# Pace BMS Linux CLI Tool — Design Spec

**Date:** 2026-06-07  
**Scope:** v1 — read base info + get/set balance parameters for a specific pack (1–15)

---

## Overview

A C++17 command-line tool that communicates with Pace BMS hardware over a serial port using the Pace ASCII protocol. Reverse-engineered from decompiled C# source (`PbmsTools V2.5`).

---

## Wire Protocol

All frames are ASCII-encoded, delimited by `0x7E` (start) and `0x0D` (end):

```
7E | VER(2) | ADR(2) | 46 | CMD(2) | LCHKSUM(4) | DATA(n*2) | CHKSUM(4) | 0D
```

- `VER` = `"00"` (fixed)
- `ADR` = pack address as 2-char uppercase hex (e.g. `"01"`, `"FF"`)
- `46` = CID1, fixed (ASCII `'F'`)
- `CMD` = command byte as 2-char uppercase hex
- `LCHKSUM` = 4-char hex: upper 4 bits = `~(sum_of_nibbles(length) % 16) + 1` masked to 4 bits; lower 12 bits = byte count of DATA field (i.e. `n*2` ASCII chars)
- `DATA` = info bytes each encoded as 2 uppercase hex chars; empty for read requests
- `CHKSUM` = 4-char hex: `(~(sum_of_all_ascii_bytes_from_VER_to_end_of_DATA) % 65536) + 1`

Read requests: `DATA` is empty, `LCHKSUM` = `"0000"`.

Response format is identical. First data byte after decoding is `RTN` (0x00 = success).

---

## File Structure

```
pbmstool/
├── CMakeLists.txt
├── src/
│   ├── protocol.h / protocol.cpp   — frame encode/decode, checksum
│   ├── serial.h / serial.cpp       — POSIX serial open/close/read/write
│   ├── bms.h / bms.cpp             — command layer (read/write BMS)
│   └── main.cpp                    — CLI parsing, subcommand dispatch, output
```

---

## Commands (v1)

### Read commands

| Command | Byte | Address | Notes |
|---|---|---|---|
| GetPackCount | `0x90` | `0xFF` | Response byte[0] = pack count |
| GetVersionInfo | `0xC1` | pack (1–15) | 20 ASCII bytes → version string |
| GetAnalog | `0x42` | pack (1–15) | Cell voltages, temps, current, total V, SOC, SOH, capacity |
| GetAlarm | `0x44` | pack (1–15) | Status flags, alarm states |
| ReadBalance | `0xB6` | `0x00` | bytes[0..1] = threshold V×1000 (u16 BE), bytes[2..3] = delta mV (u16 BE) |

### Write commands

| Command | Byte | Address | Payload |
|---|---|---|---|
| WriteBalance | `0xB5` | `0x00` | bytes[0..1] = threshold V×1000 (u16 BE), bytes[2..3] = delta mV (u16 BE) |

---

## CLI Interface

```
pbmstool --port <dev> [--baud <rate>] [--timeout <ms>] [--pack <N>] <subcommand>
```

**Global flags:**
- `--port` — serial device (required), e.g. `/dev/ttyUSB0`
- `--baud` — baud rate (default: 9600)
- `--timeout` — read timeout in ms (default: 2000)
- `--pack N` — target pack 1–15 (default: 1; ignored by `discover`)

**Subcommands:**

| Subcommand | Description |
|---|---|
| `discover` | GetPackCount → prints `pack_count=N` |
| `info` | GetVersionInfo → prints `version=...` |
| `analog` | GetAnalog → prints cell voltages, temps, current, total_voltage, soc, soh, capacity |
| `alarm` | GetAlarm → prints status flags as key=value |
| `params get` | ReadBalance → prints `balance_threshold_v=X.XX` and `balance_delta_mv=N` |
| `params set balance-threshold=X.XX balance-delta=N` | WriteBalance with given values |

Output is plain-text `key=value` lines to stdout. Errors go to stderr.

---

## Error Handling

- 2-second default read timeout per command
- Up to 2 retries on timeout or checksum error (matches original tool)
- Non-zero exit code on: timeout, CHKSUM mismatch, LCHKSUM mismatch, RTN ≠ 0, invalid pack address
- `params set` validates: `balance_threshold` must be positive float, `balance_delta` must be positive integer

---

## Build

C++17, no external dependencies. CMake minimum version 3.14.

```sh
cmake -B build && cmake --build build
./build/pbmstool --port /dev/ttyUSB0 discover
```
