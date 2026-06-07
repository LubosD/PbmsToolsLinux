# AGENTS.md — Codebase Guide for AI Agents

This document explains the project structure, the relationship between the C++ CLI tool and the C# reference implementation, and the protocol encoding rules that govern all BMS communication.

---

## Project Layout

```
PbmsTools/
├── pbmstool/src/          ← C++ CLI tool (the active codebase)
│   ├── bms.h / bms.cpp    ← BMS communication layer
│   ├── protocol.h/.cpp    ← Pace ASCII frame encode/decode
│   ├── serial.h/.cpp      ← Linux termios serial port
│   └── main.cpp           ← CLI entry point
└── pbmstoolsgui/          ← C# Windows GUI (read-only reference)
    └── PbmsTools/
        └── FrmMain.cs     ← ~9700-line main form; contains all protocol logic
```

The C# GUI is **not built or run** — it is the authoritative reference for understanding what commands exist, how payloads are encoded, and what each parameter means. All new features in the C++ CLI must be cross-checked against `FrmMain.cs`.

---

## C# ↔ C++ Mapping

### How to find something in FrmMain.cs

- **Read handlers**: `switch (entity.Cmd)` starting around line 3507 — each `case N:` parses the device response and populates UI fields (`parameterText_1` … `parameterText_53`).
- **Write handler**: `btnParamsWrite_Click` (line 5547) — builds byte arrays and enqueues `FrameInfo` objects with the write command byte.
- **Read trigger**: `btnParamsRead_Click` (line 5397) — enqueues a sequence of read commands.

### Command numbering convention

Read and write commands are paired: **even = write**, **odd = read** (the device echoes the read command + 1 as the response identifier). Example: write Cell OVP with 0xD0, receive response tagged as 0xD1.

---

## Parameter Groups

The table below maps every C# parameter group to the C++ implementation.

| C# params | Meaning | Read CMD | Write CMD | C++ get method | C++ set method |
|---|---|---|---|---|---|
| 1–4 | Cell over-voltage protection (OVP) | 0xD1 | 0xD0 | `get_cell_ovp(addr, …)` | `set_cell_ovp(…)` |
| 5–8 | Pack over-voltage protection | 0xD5 | 0xD4 | `get_pack_ovp(addr, …)` | `set_pack_ovp(…)` |
| 9–12 | Charge overcurrent protection (OCP) | 0xD3 | 0xD2 | `get_chg_ocp(addr, …)` | `set_chg_ocp(…)` |
| 13–16 | Discharge overcurrent protection | 0xD7 | 0xD6 | `get_dchg_ocp(addr, …)` | `set_dchg_ocp(…)` |
| 17–19 | Short-circuit protection (SCP) | 0xD9 | 0xD8 | `get_scp(…)` | `set_scp(…)` |
| 20–22 | Cell charge over-temperature (OTP) | 0xDB | 0xDA | `get_cell_chg_otp(…)` | `set_cell_chg_otp(…)` |
| 23–24 | MOSFET over-temperature protection | 0xE3 | 0xE2 | `get_mosfet_otp(…)` | `set_mosfet_otp(…)` |
| 25 | Cell under-temperature protection (UTP) | 0xE5 | 0xE4 | `get_utp(…)` | `set_utp(…)` |
| 26–27 | Pack params A (balance start/delta) | 0xB6 | 0xB5 | `get_balance(…)` | `set_balance(…)` |
| 28–29 | Pack params B | 0xA0 | 0xA8 | `get_pack_b(…)` | `set_pack_b(…)` |
| 30–31, 53 | Cell equalization | 0xAF | 0xAE | `get_equalization(…)` | `set_equalization(…)` |
| 32–37 | Charge temperature protection (6 sensors) | 0xDD | 0xDC | `get_chg_temp_prot(…)` | `set_chg_temp_prot(…)` |
| 38–43 | Discharge temperature protection (6 sensors) | 0xDF | 0xDE | `get_dchg_temp_prot(…)` | `set_dchg_temp_prot(…)` |
| 44–46 | MOS charge temperature (3 sensors, optional) | 0xE1 | 0xE0 | `get_mos_chg_temp(…)` | `set_mos_chg_temp(…)` |
| 47–52 | MOS discharge temperature (6 sensors, optional) | 0xE7 | 0xE6 | `get_mos_dchg_temp(…)` | `set_mos_dchg_temp(…)` |

**Optional groups** (MOS charge/discharge temp): if the device doesn't support the group it returns RTN=4 instead of 0. `get_mos_chg_temp` and `get_mos_dchg_temp` set `TempProtGroup::present = false` in that case; `params get` silently skips printing them.

---

## Protocol Encoding Rules

All values are **big-endian**. These rules are extracted from `FrmMain.cs` and must be followed exactly.

### Voltage / current (×1000)

```
raw (uint16) = value_in_V_or_A × 1000   (rounded to nearest integer)
value         = raw / 1000.0
```

Used by: cell OVP, pack OVP, charge OCP, discharge OCP, equalization start voltage, pack params A/B.

### Temperature — Kelvin×10 encoding

```
raw (uint16) = (celsius + 273) × 10
celsius       = raw / 10 − 273
```

Used by: all 6-sensor and 3-sensor temperature protection groups (params 32–52).

### Temperature — signed int16 (OTP/UTP)

```
raw (int16)   = celsius   (stored directly as a signed 16-bit integer)
display       = abs(raw)  (C# uses Math.Abs; always shown positive in UI)
```

The C++ CLI outputs the absolute value. When writing, pass the value as positive integer; it is cast to `int16_t` before encoding.

Used by: cell charge OTP (params 20–22), MOSFET OTP (params 23–24).

### Delay with ×100 multiplier

```
raw (uint8)   = delay_ms / 100
delay_ms      = raw × 100
```

Used by: cell OVP/OCP/OTP groups.

### Delay with ×25 multiplier

```
raw (uint8)   = delay_ms / 25
delay_ms      = raw × 25
```

Used by: MOSFET OTP (param 24), UTP (param 25).

### Enable flag (first byte of most groups)

```
byte[0] = 0x01 if enabled, 0x00 if disabled
```

### Payload size summary

| Group type | Bytes | Structure |
|---|---|---|
| Voltage/current prot (OVP, OCP, etc.) | 8 | `[enable, prot_hi, prot_lo, alarm_hi, alarm_lo, rec_hi, rec_lo, delay/100]` |
| Short-circuit prot | 6 | `[enable, thr_hi, thr_lo, time_hi, time_lo, delay/100]` |
| Cell OTP (charge) | 6 | `[enable, trig_hi, trig_lo, rec_hi, rec_lo, delay/100]` |
| MOSFET OTP | 3 | `[trig_hi, trig_lo, delay/25]` |
| UTP | 1 | `[delay/25]` |
| Pack params A/B, Equalization A | 4 | `[v_hi, v_lo, int_hi, int_lo]` |
| Equalization | 5 | `[v_hi, v_lo, delta_hi, delta_lo, cell_cnt]` |
| Temp group (6 sensors) | 13 | `[enable, t1_hi, t1_lo, …, t6_hi, t6_lo]` |
| Temp group (3 sensors) | 7 | `[enable, t1_hi, t1_lo, t2_hi, t2_lo, t3_hi, t3_lo]` |

---

## Protocol Frame Format

From `protocol.cpp` and `CommonMethod.GetData()` in C#:

```
0x7E          start byte
00            version/leading zeros
AA            address (2 hex ASCII digits)
46            fixed literal
CC            command byte (2 hex ASCII digits)
LLLL          length code: 12-bit data length + 4-bit nibble checksum
[DD...]       data bytes (each byte as 2 uppercase hex ASCII chars)
CCCC          frame checksum (16-bit two's complement of all payload bytes)
0x0D          end byte (CR)
```

**Frame checksum**: sum all data bytes → take two's complement modulo 65536 → encode as 4-char uppercase hex string.

**Length checksum** (nibble sum): `lchksum = (~(sum of nibbles of length field) + 1) & 0xF`, placed in the top nibble of the length word.

---

## Address Conventions

- **Read commands**: use the pack address (1–15), passed as `addr` parameter to `get_*` methods that take `uint8_t addr`. The pack address selects which BMS pack responds.
- **Write commands**: use address `0x00` (broadcast). All `set_*` methods hardcode `0x00`.
- **Exception**: `get_balance`, `get_scp`, `get_cell_chg_otp`, `get_mosfet_otp`, `get_utp`, `get_pack_b`, `get_equalization`, and all temperature group reads use address `0x00` (these are non-pack-specific settings stored globally in the BMS).

---

## CLI Output Format

`params get` prints every group in sequence, one `key=value` per line, to stdout. Boolean `enable` fields print as `1` or `0`. Voltages print with 3 decimal places. Temperatures are integers (°C). Optional groups (MOS temp) are omitted from output if `present=false`.

`params set` accepts any subset of keys. For each group that has at least one key specified, any unspecified keys are filled by reading the current device value first (read-modify-write). Only groups with at least one changed key are written to the device.

---

## Adding a New Parameter Group

1. Add a struct in `bms.h` (or reuse an existing one).
2. Add read/write command constants in `bms.cpp`.
3. Implement `get_*/set_*` methods following the existing patterns (helpers: `parse_volt_prot`, `encode_volt_prot`, `parse_temp_group6`, etc.).
4. Add output lines to the `params get` branch in `main.cpp`.
5. Add key parsing and read-modify-write logic to the `params set` branch.
6. Update the usage string in `usage()`.
7. Cross-check byte layout against the corresponding `case N:` read handler and `numArrayN` write block in `FrmMain.cs`.
