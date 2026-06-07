# Pace BMS Linux CLI Tool — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a C++17 CLI tool that reads status and gets/sets balance parameters on Pace BMS packs over a serial port.

**Architecture:** Multi-file CMake project. `protocol` handles frame encode/decode and checksum. `serial` owns the POSIX file descriptor and handles raw I/O. `bms` builds BMS commands on top of serial+protocol. `main` does CLI parsing and output.

**Tech Stack:** C++17, CMake 3.14+, POSIX termios, no external dependencies.

---

## File Map

| File | Responsibility |
|---|---|
| `src/protocol.h/cpp` | Frame encoding, decoding, LCHKSUM + CHKSUM calculation |
| `src/serial.h/cpp` | POSIX serial open/close/write/read_frame |
| `src/bms.h/cpp` | BMS commands: discover, version, analog, alarm, balance get/set |
| `src/main.cpp` | Argument parsing, subcommand dispatch, stdout output |
| `CMakeLists.txt` | Build definition |

---

## Task 1: CMake Scaffold

**Files:**
- Create: `pbmstool/CMakeLists.txt`
- Create: `pbmstool/src/protocol.h`
- Create: `pbmstool/src/protocol.cpp`
- Create: `pbmstool/src/serial.h`
- Create: `pbmstool/src/serial.cpp`
- Create: `pbmstool/src/bms.h`
- Create: `pbmstool/src/bms.cpp`
- Create: `pbmstool/src/main.cpp`

- [ ] **Step 1: Create `pbmstool/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.14)
project(pbmstool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(pbmstool
    src/protocol.cpp
    src/serial.cpp
    src/bms.cpp
    src/main.cpp
)
target_include_directories(pbmstool PRIVATE src)
target_compile_options(pbmstool PRIVATE -Wall -Wextra)
```

- [ ] **Step 2: Create stub source files**

`src/protocol.h`:
```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace pace {

std::vector<uint8_t> encode_frame(uint8_t adr, uint8_t cmd,
                                  const std::vector<uint8_t>& info = {});

struct Response {
    uint8_t adr = 0;
    uint8_t rtn = 0;
    std::vector<uint8_t> data;
};

bool decode_frame(const std::vector<uint8_t>& raw, Response& out, std::string& err);

} // namespace pace
```

`src/protocol.cpp`:
```cpp
#include "protocol.h"
```

`src/serial.h`:
```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace pace {

class Serial {
public:
    ~Serial();
    bool open(const std::string& port, int baud, int timeout_ms);
    void close();
    bool write(const std::vector<uint8_t>& data, std::string& err);
    bool read_frame(std::vector<uint8_t>& frame, std::string& err);
private:
    int fd_ = -1;
};

} // namespace pace
```

`src/serial.cpp`:
```cpp
#include "serial.h"
```

`src/bms.h`:
```cpp
#pragma once
#include "protocol.h"
#include "serial.h"
#include <cstdint>
#include <string>
#include <vector>

namespace pace {

struct AnalogData {
    int cell_count = 0;
    std::vector<double> cell_mv;
    int temp_count = 0;
    std::vector<double> temp_raw;   // Kelvin * 10
    int16_t  current_10ma = 0;      // signed, 10 mA per LSB
    uint16_t total_volt_mv = 0;     // mV
    int      remain_cap_mah = 0;
    int      full_cap_mah = 0;
    int      design_cap_mah = 0;
    uint16_t cycle = 0;
};

struct AlarmData {
    int cell_count = 0;
    std::vector<uint8_t> cell_volt_alarm;
    int temp_count = 0;
    std::vector<uint8_t> temp_alarm;
    uint8_t charge_curr_state = 0;
    uint8_t total_volt_state = 0;
    uint8_t discharge_curr_state = 0;
    uint8_t status[9] = {};
};

struct BalanceParams {
    double threshold_v = 0.0;
    int    delta_mv = 0;
};

class BMS {
public:
    explicit BMS(Serial& serial);

    // Returns pack count (1-15) or -1 on error.
    int  discover(std::string& err);

    bool get_version(uint8_t addr, std::string& version, std::string& err);
    bool get_analog (uint8_t addr, AnalogData&    out, std::string& err);
    bool get_alarm  (uint8_t addr, AlarmData&     out, std::string& err);
    bool get_balance(BalanceParams& out, std::string& err);
    bool set_balance(const BalanceParams& p, std::string& err);

private:
    static constexpr int kRetries = 2;
    bool send_recv(uint8_t adr, uint8_t cmd,
                   const std::vector<uint8_t>& data,
                   Response& resp, std::string& err);
    Serial& serial_;
};

} // namespace pace
```

`src/bms.cpp`:
```cpp
#include "bms.h"
```

`src/main.cpp`:
```cpp
int main() { return 0; }
```

- [ ] **Step 3: Verify the scaffold compiles**

```bash
cd pbmstool
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Expected: build succeeds, binary `build/pbmstool` exists and exits 0.

- [ ] **Step 4: Commit**

```bash
git add pbmstool/
git commit -m "feat: scaffold pbmstool CMake project"
```

---

## Task 2: Protocol Layer

**Files:**
- Modify: `pbmstool/src/protocol.cpp`

The Pace BMS wire format (ASCII-framed, from `CommonMethod.GetData`):
```
0x7E | VER(2) | ADR(2) | 46 | CMD(2) | LCHKSUM(4) | DATA(n*2) | CHKSUM(4) | 0x0D
```
- VER = `"00"`, all hex ASCII uppercase
- LCHKSUM: 4-char hex; upper 4 bits = `(~nibble_sum % 16) + 1) & 0xF`, lower 12 bits = ASCII length of DATA field
- CHKSUM: 4-char hex = `(~sum_of_bytes_from_VER_through_DATA + 1) % 65536`
- Response: same structure; byte at offset `[7..8]` is RTN (0x00 = success); data at `[13 .. len-6]`

- [ ] **Step 1: Implement `encode_frame` in `protocol.cpp`**

```cpp
#include "protocol.h"
#include <cstdio>
#include <stdexcept>

namespace pace {

namespace {

std::string lchksum(int ascii_len) {
    int nibble_sum = (ascii_len & 0x0F)
                   + ((ascii_len & 0xF0) >> 4)
                   + ((ascii_len & 0xF00) >> 8);
    uint8_t lchk = static_cast<uint8_t>((~(nibble_sum % 16) + 1) & 0x0F);
    uint16_t val = static_cast<uint16_t>((ascii_len & 0x0FFF) | (lchk << 12));
    char buf[5];
    std::snprintf(buf, sizeof(buf), "%04X", val);
    return buf;
}

std::string chksum(const std::vector<uint8_t>& bytes) {
    uint32_t sum = 0;
    for (auto b : bytes) sum += b;
    uint16_t chk = static_cast<uint16_t>((~(sum % 65536u) + 1u) & 0xFFFFu);
    char buf[5];
    std::snprintf(buf, sizeof(buf), "%04X", chk);
    return buf;
}

} // namespace

std::vector<uint8_t> encode_frame(uint8_t adr, uint8_t cmd,
                                  const std::vector<uint8_t>& info) {
    std::string data_hex;
    for (auto b : info) {
        char buf[3];
        std::snprintf(buf, sizeof(buf), "%02X", b);
        data_hex += buf;
    }

    char adr_hex[3], cmd_hex[3];
    std::snprintf(adr_hex, sizeof(adr_hex), "%02X", adr);
    std::snprintf(cmd_hex, sizeof(cmd_hex), "%02X", cmd);

    std::string payload = std::string("00")
                        + adr_hex + "46" + cmd_hex
                        + lchksum(static_cast<int>(data_hex.size()))
                        + data_hex;

    std::vector<uint8_t> payload_bytes(payload.begin(), payload.end());
    std::string ck = chksum(payload_bytes);

    std::vector<uint8_t> frame;
    frame.push_back(0x7E);
    for (auto c : payload_bytes) frame.push_back(static_cast<uint8_t>(c));
    for (char c : ck)            frame.push_back(static_cast<uint8_t>(c));
    frame.push_back(0x0D);
    return frame;
}

bool decode_frame(const std::vector<uint8_t>& raw, Response& out, std::string& err) {
    // Minimum frame: 0x7E + "00" + ADR + "46" + RTN + LCHKSUM + CHKSUM + 0x0D = 18 bytes
    if (raw.size() < 18 || raw.front() != 0x7E || raw.back() != 0x0D) {
        err = "invalid frame boundaries";
        return false;
    }

    // Verify CHKSUM: covers bytes [1 .. size-6], result must match [size-5 .. size-2]
    {
        uint32_t sum = 0;
        for (size_t i = 1; i < raw.size() - 5; ++i) sum += raw[i];
        uint16_t expected = static_cast<uint16_t>((~(sum % 65536u) + 1u) & 0xFFFFu);
        char exp_hex[5];
        std::snprintf(exp_hex, sizeof(exp_hex), "%04X", expected);
        std::string actual(reinterpret_cast<const char*>(raw.data() + raw.size() - 5), 4);
        if (actual != exp_hex) { err = "checksum mismatch"; return false; }
    }

    // ADR at [3..4]
    char adr_str[3] = { static_cast<char>(raw[3]), static_cast<char>(raw[4]), 0 };
    out.adr = static_cast<uint8_t>(std::stoul(adr_str, nullptr, 16));

    // RTN at [7..8]
    char rtn_str[3] = { static_cast<char>(raw[7]), static_cast<char>(raw[8]), 0 };
    out.rtn = static_cast<uint8_t>(std::stoul(rtn_str, nullptr, 16));

    // DATA: ASCII hex at [13 .. size-6], decode to bytes
    if (raw.size() > 18) {
        size_t data_ascii_len = raw.size() - 18;
        out.data.resize(data_ascii_len / 2);
        for (size_t i = 0; i < out.data.size(); ++i) {
            char nibbles[3] = { static_cast<char>(raw[13 + 2*i]),
                                static_cast<char>(raw[13 + 2*i + 1]), 0 };
            out.data[i] = static_cast<uint8_t>(std::stoul(nibbles, nullptr, 16));
        }
    }

    return true;
}

} // namespace pace
```

- [ ] **Step 2: Build and verify it compiles**

```bash
cmake --build build 2>&1
```

Expected: zero errors.

- [ ] **Step 3: Spot-check encode by hand**

In a scratch main (or just reason through it):

`encode_frame(0x01, 0xC1, {})` should produce a frame where:
- Byte 0 = 0x7E
- Bytes 1–2 = '0','0'
- Bytes 3–4 = '0','1'
- Bytes 5–6 = '4','6'
- Bytes 7–8 = 'C','1'
- Bytes 9–12 = '0','0','0','0'  (empty LCHKSUM)
- Bytes 13–16 = CHKSUM of "0001" + "46" + "C1" + "0000"
- Byte 17 = 0x0D

Manually compute CHKSUM:
`"0001 46 C1 0000"` as ASCII bytes:
'0'=0x30,'0'=0x30,'0'=0x30,'1'=0x31,'4'=0x34,'6'=0x36,'C'=0x43,'1'=0x31,'0'=0x30,'0'=0x30,'0'=0x30,'0'=0x30
sum = 0x30+0x30+0x30+0x31+0x34+0x36+0x43+0x31+0x30+0x30+0x30+0x30 = 0x31D
~0x31D + 1 = 0xFCE3
CHKSUM = "FCE3"

Total frame length = 18 bytes.

You can add a temporary `assert` in main() to verify:
```cpp
#include "protocol.h"
#include <cassert>
int main() {
    auto f = pace::encode_frame(0x01, 0xC1);
    assert(f.size() == 18);
    assert(f[0] == 0x7E);
    assert(f[17] == 0x0D);
    assert(f[13]=='F' && f[14]=='C' && f[15]=='E' && f[16]=='3');
    return 0;
}
```

Run: `cmake --build build && ./build/pbmstool`
Expected: exits 0 (no assertion failures).

Remove the temporary assert code from main.cpp afterwards.

- [ ] **Step 4: Commit**

```bash
git add pbmstool/src/protocol.cpp pbmstool/src/protocol.h
git commit -m "feat: implement protocol encode/decode"
```

---

## Task 3: Serial Layer

**Files:**
- Modify: `pbmstool/src/serial.cpp`

Uses POSIX termios for 8N1 serial. `read_frame` reads byte-by-byte until it sees 0x0D, enforcing the timeout set on the file descriptor.

- [ ] **Step 1: Implement `serial.cpp`**

```cpp
#include "serial.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

namespace pace {

Serial::~Serial() { close(); }

static speed_t to_baud(int baud) {
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        default:     return B9600;
    }
}

bool Serial::open(const std::string& port, int baud, int timeout_ms) {
    fd_ = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) return false;

    // Switch to blocking with VTIME timeout
    int flags = fcntl(fd_, F_GETFL, 0);
    fcntl(fd_, F_SETFL, flags & ~O_NONBLOCK);

    struct termios tty {};
    if (tcgetattr(fd_, &tty) != 0) { ::close(fd_); fd_ = -1; return false; }

    cfsetispeed(&tty, to_baud(baud));
    cfsetospeed(&tty, to_baud(baud));
    cfmakeraw(&tty);

    tty.c_cflag  = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag &= ~(PARENB | PARODD | CSTOPB | CRTSCTS);
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_iflag  = IGNBRK;
    tty.c_lflag  = 0;
    tty.c_oflag  = 0;

    // VTIME in 0.1s units; VMIN=0 → pure timeout mode
    int vtime = (timeout_ms + 99) / 100;  // round up to 0.1s
    if (vtime < 1) vtime = 1;
    if (vtime > 255) vtime = 255;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = static_cast<uint8_t>(vtime);

    if (tcsetattr(fd_, TCSANOW, &tty) != 0) { ::close(fd_); fd_ = -1; return false; }
    tcflush(fd_, TCIOFLUSH);
    return true;
}

void Serial::close() {
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

bool Serial::write(const std::vector<uint8_t>& data, std::string& err) {
    ssize_t n = ::write(fd_, data.data(), data.size());
    if (n < 0 || static_cast<size_t>(n) != data.size()) {
        err = std::strerror(errno);
        return false;
    }
    return true;
}

bool Serial::read_frame(std::vector<uint8_t>& frame, std::string& err) {
    frame.clear();
    uint8_t byte;
    // Wait for start byte 0x7E
    while (true) {
        ssize_t n = ::read(fd_, &byte, 1);
        if (n <= 0) { err = (n == 0) ? "timeout" : std::strerror(errno); return false; }
        if (byte == 0x7E) { frame.push_back(byte); break; }
    }
    // Read until 0x0D end byte
    for (int i = 0; i < 2048; ++i) {
        ssize_t n = ::read(fd_, &byte, 1);
        if (n <= 0) { err = (n == 0) ? "timeout" : std::strerror(errno); return false; }
        frame.push_back(byte);
        if (byte == 0x0D) return true;
    }
    err = "frame too long";
    return false;
}

} // namespace pace
```

- [ ] **Step 2: Build and verify**

```bash
cmake --build build 2>&1
```

Expected: zero errors.

- [ ] **Step 3: Commit**

```bash
git add pbmstool/src/serial.cpp pbmstool/src/serial.h
git commit -m "feat: implement POSIX serial layer"
```

---

## Task 4: BMS — `send_recv`, `discover`, `get_version`

**Files:**
- Modify: `pbmstool/src/bms.cpp`

- [ ] **Step 1: Implement `send_recv` helper in `bms.cpp`**

```cpp
#include "bms.h"
#include <cstring>

namespace pace {

static constexpr uint8_t CMD_VERSION  = 0xC1;
static constexpr uint8_t CMD_ANALOG   = 0x42;
static constexpr uint8_t CMD_ALARM    = 0x44;
static constexpr uint8_t CMD_BAL_READ = 0xB6;
static constexpr uint8_t CMD_BAL_WRITE= 0xB5;

BMS::BMS(Serial& serial) : serial_(serial) {}

bool BMS::send_recv(uint8_t adr, uint8_t cmd,
                    const std::vector<uint8_t>& data,
                    Response& resp, std::string& err) {
    auto frame = encode_frame(adr, cmd, data);
    for (int attempt = 0; attempt < kRetries; ++attempt) {
        if (!serial_.write(frame, err)) return false;
        std::vector<uint8_t> raw;
        if (!serial_.read_frame(raw, err)) {
            if (attempt + 1 < kRetries) continue;
            return false;
        }
        if (!decode_frame(raw, resp, err)) {
            if (attempt + 1 < kRetries) continue;
            return false;
        }
        return true;
    }
    return false;
}
```

- [ ] **Step 2: Implement `discover` and `get_version`**

Append to `bms.cpp`:

```cpp
int BMS::discover(std::string& err) {
    // Probe packs 1..15 with GetVersionInfo. Stop after 3 consecutive failures.
    int count = 0;
    int consecutive_fail = 0;
    for (uint8_t addr = 1; addr <= 15; ++addr) {
        Response resp;
        std::string e;
        if (send_recv(addr, CMD_VERSION, {}, resp, e) && resp.rtn == 0) {
            ++count;
            consecutive_fail = 0;
        } else {
            if (++consecutive_fail >= 3) break;
        }
    }
    if (count == 0) { err = "no packs found"; return -1; }
    return count;
}

bool BMS::get_version(uint8_t addr, std::string& version, std::string& err) {
    Response resp;
    if (!send_recv(addr, CMD_VERSION, {}, resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    // Response data: 20 ASCII bytes of version string
    if (resp.data.size() < 20) { err = "short version response"; return false; }
    version = std::string(reinterpret_cast<const char*>(resp.data.data()), 20);
    // Trim nulls and spaces
    while (!version.empty() && (version.back() == '\0' || version.back() == ' '))
        version.pop_back();
    return true;
}
```

- [ ] **Step 3: Build and verify**

```bash
cmake --build build 2>&1
```

Expected: zero errors.

- [ ] **Step 4: Commit**

```bash
git add pbmstool/src/bms.cpp pbmstool/src/bms.h
git commit -m "feat: implement BMS send_recv, discover, get_version"
```

---

## Task 5: BMS — `get_analog`

**Files:**
- Modify: `pbmstool/src/bms.cpp`

The GetAnalog (0x42) response decoded data layout:
- `data[0]` = DATAFLAG (skip)
- `data[1]` = pack address in response (skip for single-pack)
- `data[2]` = CellCount (N)
- `data[3 .. 2+2N]` = N × u16 big-endian cell voltages in mV
- `data[3+2N]` = TempCount (M)
- `data[4+2N .. 3+2N+2M]` = M × u16 big-endian temp values (Kelvin × 10)
- `data[4+2N+2M .. 5+2N+2M]` = s16 current in 10 mA
- `data[6+2N+2M .. 7+2N+2M]` = u16 total voltage in mV
- `data[8+2N+2M .. 9+2N+2M]` = u16 remaining capacity in 10 mAh (×10 = mAh)
- `data[10+2N+2M .. 11+2N+2M]` = u16 full capacity in 10 mAh (×10 = mAh)
- `data[12+2N+2M]` = 1 byte skipped (custom number)
- `data[13+2N+2M .. 14+2N+2M]` = u16 cycle count
- `data[15+2N+2M .. 16+2N+2M]` = u16 design capacity in 10 mAh (×10 = mAh)

- [ ] **Step 1: Implement `get_analog`**

Append to `bms.cpp`:

```cpp
bool BMS::get_analog(uint8_t addr, AnalogData& out, std::string& err) {
    Response resp;
    if (!send_recv(addr, CMD_ANALOG, {}, resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }

    const auto& d = resp.data;
    // Need at least 2 header bytes + 1 CellCount
    if (d.size() < 3) { err = "analog response too short"; return false; }

    size_t i = 2;  // skip DATAFLAG and pack-address bytes
    out.cell_count = d[i++];
    if (out.cell_count > 16) { err = "cell count > 16"; return false; }
    if (d.size() < i + static_cast<size_t>(out.cell_count) * 2 + 1)
        { err = "analog response truncated (cells)"; return false; }

    out.cell_mv.resize(out.cell_count);
    for (int k = 0; k < out.cell_count; ++k) {
        out.cell_mv[k] = static_cast<double>((d[i] << 8) | d[i+1]);
        i += 2;
    }

    out.temp_count = d[i++];
    if (d.size() < i + static_cast<size_t>(out.temp_count) * 2 + 8)
        { err = "analog response truncated (temps)"; return false; }

    out.temp_raw.resize(out.temp_count);
    for (int k = 0; k < out.temp_count; ++k) {
        out.temp_raw[k] = static_cast<double>((d[i] << 8) | d[i+1]);
        i += 2;
    }

    out.current_10ma  = static_cast<int16_t>((d[i] << 8) | d[i+1]); i += 2;
    out.total_volt_mv = static_cast<uint16_t>((d[i] << 8) | d[i+1]); i += 2;
    uint16_t rem_raw  = static_cast<uint16_t>((d[i] << 8) | d[i+1]); i += 2;
    out.remain_cap_mah = static_cast<int>(rem_raw) * 10;
    uint16_t full_raw  = static_cast<uint16_t>((d[i] << 8) | d[i+1]); i += 2;
    out.full_cap_mah   = static_cast<int>(full_raw) * 10;
    ++i;  // skip custom number byte
    out.cycle          = static_cast<uint16_t>((d[i] << 8) | d[i+1]); i += 2;
    if (i + 1 < d.size()) {
        uint16_t des_raw   = static_cast<uint16_t>((d[i] << 8) | d[i+1]);
        out.design_cap_mah = static_cast<int>(des_raw) * 10;
    }

    return true;
}
```

- [ ] **Step 2: Build and verify**

```bash
cmake --build build 2>&1
```

Expected: zero errors.

- [ ] **Step 3: Commit**

```bash
git add pbmstool/src/bms.cpp
git commit -m "feat: implement get_analog"
```

---

## Task 6: BMS — `get_alarm`

**Files:**
- Modify: `pbmstool/src/bms.cpp`

GetAlarm (0x44) decoded data layout:
- `data[0]` = DATAFLAG (skip)
- `data[1]` = pack address (skip)
- `data[2]` = CellCount (N)
- `data[3 .. 2+N]` = N cell voltage alarm flags (1 byte each)
- `data[3+N]` = TempCount (M)
- `data[4+N .. 3+N+M]` = M temp alarm flags (1 byte each)
- `data[4+N+M]` = CharCurrState
- `data[5+N+M]` = TotalVoltState
- `data[6+N+M]` = DischarCurrState
- `data[7+N+M .. 15+N+M]` = Status1..Status9 (9 bytes)

- [ ] **Step 1: Implement `get_alarm`**

Append to `bms.cpp`:

```cpp
bool BMS::get_alarm(uint8_t addr, AlarmData& out, std::string& err) {
    Response resp;
    if (!send_recv(addr, CMD_ALARM, {}, resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }

    const auto& d = resp.data;
    if (d.size() < 3) { err = "alarm response too short"; return false; }

    size_t i = 2;
    out.cell_count = d[i++];
    if (out.cell_count > 16) { err = "cell count > 16"; return false; }
    if (d.size() < i + static_cast<size_t>(out.cell_count) + 1)
        { err = "alarm response truncated (cells)"; return false; }

    out.cell_volt_alarm.assign(d.begin() + i, d.begin() + i + out.cell_count);
    i += out.cell_count;

    out.temp_count = d[i++];
    if (d.size() < i + static_cast<size_t>(out.temp_count) + 12)
        { err = "alarm response truncated (temps)"; return false; }

    out.temp_alarm.assign(d.begin() + i, d.begin() + i + out.temp_count);
    i += out.temp_count;

    out.charge_curr_state    = d[i++];
    out.total_volt_state     = d[i++];
    out.discharge_curr_state = d[i++];

    for (int k = 0; k < 9 && i < d.size(); ++k)
        out.status[k] = d[i++];

    return true;
}
```

- [ ] **Step 2: Build and verify**

```bash
cmake --build build 2>&1
```

Expected: zero errors.

- [ ] **Step 3: Commit**

```bash
git add pbmstool/src/bms.cpp
git commit -m "feat: implement get_alarm"
```

---

## Task 7: BMS — `get_balance` and `set_balance`

**Files:**
- Modify: `pbmstool/src/bms.cpp`

`CMD_BAL_READ (0xB6)` response decoded data:
- `data[0..1]` = threshold V × 1000 as u16 big-endian
- `data[2..3]` = delta mV as u16 big-endian

`CMD_BAL_WRITE (0xB5)` write payload (sent as info bytes, addr=0):
- `info[0..1]` = threshold V × 1000 as u16 big-endian
- `info[2..3]` = delta mV as u16 big-endian

- [ ] **Step 1: Implement `get_balance` and `set_balance`**

Append to `bms.cpp`:

```cpp
bool BMS::get_balance(BalanceParams& out, std::string& err) {
    Response resp;
    if (!send_recv(0x00, CMD_BAL_READ, {}, resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    if (resp.data.size() < 4) { err = "balance response too short"; return false; }
    const auto& d = resp.data;
    uint16_t raw_thresh = static_cast<uint16_t>((d[0] << 8) | d[1]);
    uint16_t raw_delta  = static_cast<uint16_t>((d[2] << 8) | d[3]);
    out.threshold_v = raw_thresh / 1000.0;
    out.delta_mv    = static_cast<int>(raw_delta);
    return true;
}

bool BMS::set_balance(const BalanceParams& p, std::string& err) {
    if (p.threshold_v <= 0.0 || p.delta_mv < 0) {
        err = "invalid balance params";
        return false;
    }
    uint16_t raw_thresh = static_cast<uint16_t>(static_cast<int>(p.threshold_v * 1000.0 + 0.5));
    uint16_t raw_delta  = static_cast<uint16_t>(p.delta_mv);
    std::vector<uint8_t> info = {
        static_cast<uint8_t>(raw_thresh >> 8),
        static_cast<uint8_t>(raw_thresh & 0xFF),
        static_cast<uint8_t>(raw_delta >> 8),
        static_cast<uint8_t>(raw_delta & 0xFF),
    };
    Response resp;
    if (!send_recv(0x00, CMD_BAL_WRITE, info, resp, err)) return false;
    if (resp.rtn != 0) { err = "BMS error RTN=" + std::to_string(resp.rtn); return false; }
    return true;
}
```

- [ ] **Step 2: Build and verify**

```bash
cmake --build build 2>&1
```

Expected: zero errors.

- [ ] **Step 3: Commit**

```bash
git add pbmstool/src/bms.cpp
git commit -m "feat: implement get_balance and set_balance"
```

---

## Task 8: CLI — `main.cpp`

**Files:**
- Modify: `pbmstool/src/main.cpp`

Argument syntax:
```
pbmstool --port <dev> [--baud <N>] [--timeout <ms>] [--pack <N>] <subcommand>

Subcommands:
  discover
  info
  analog
  alarm
  params get
  params set balance-threshold=X.XX [balance-delta=N]
```

Output is plain `key=value` lines to stdout. Errors to stderr, non-zero exit on failure.

- [ ] **Step 1: Implement `main.cpp`**

```cpp
#include "bms.h"
#include "serial.h"
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace pace;

static void usage(const char* prog) {
    std::cerr << "Usage: " << prog
              << " --port <dev> [--baud <N>] [--timeout <ms>] [--pack <N>]"
                 " <subcommand>\n"
                 "Subcommands: discover | info | analog | alarm |"
                 " params get | params set balance-threshold=X.XX [balance-delta=N]\n";
}

static std::string get_kv(const std::string& arg, const std::string& key) {
    std::string prefix = key + "=";
    if (arg.substr(0, prefix.size()) == prefix) return arg.substr(prefix.size());
    return {};
}

int main(int argc, char* argv[]) {
    std::string port;
    int baud       = 9600;
    int timeout_ms = 2000;
    uint8_t pack   = 1;
    std::vector<std::string> subcmd;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--port"    && i+1 < argc) { port       = argv[++i]; }
        else if (a == "--baud"    && i+1 < argc) { baud  = std::atoi(argv[++i]); }
        else if (a == "--timeout" && i+1 < argc) { timeout_ms = std::atoi(argv[++i]); }
        else if (a == "--pack"    && i+1 < argc) { pack  = static_cast<uint8_t>(std::atoi(argv[++i])); }
        else if (a.substr(0,2) != "--")           { subcmd.push_back(a); }
        else { std::cerr << "unknown flag: " << a << "\n"; return 1; }
    }

    if (port.empty() || subcmd.empty()) { usage(argv[0]); return 1; }

    Serial serial;
    std::string err;
    if (!serial.open(port, baud, timeout_ms)) {
        std::cerr << "cannot open " << port << ": " << std::strerror(errno) << "\n";
        return 1;
    }

    BMS bms(serial);

    // --- discover ---
    if (subcmd[0] == "discover") {
        int count = bms.discover(err);
        if (count < 0) { std::cerr << err << "\n"; return 1; }
        std::cout << "pack_count=" << count << "\n";
        return 0;
    }

    // --- info ---
    if (subcmd[0] == "info") {
        std::string version;
        if (!bms.get_version(pack, version, err)) { std::cerr << err << "\n"; return 1; }
        std::cout << "pack=" << (int)pack << "\nversion=" << version << "\n";
        return 0;
    }

    // --- analog ---
    if (subcmd[0] == "analog") {
        AnalogData a;
        if (!bms.get_analog(pack, a, err)) { std::cerr << err << "\n"; return 1; }
        std::cout << "pack=" << (int)pack << "\n";
        std::cout << "cell_count=" << a.cell_count << "\n";
        for (int k = 0; k < a.cell_count; ++k)
            std::cout << "cell_" << (k+1) << "_mv=" << static_cast<int>(a.cell_mv[k]) << "\n";
        std::cout << "temp_count=" << a.temp_count << "\n";
        for (int k = 0; k < a.temp_count; ++k) {
            double tc = a.temp_raw[k] / 10.0 - 273.15;
            std::cout << "temp_" << (k+1) << "_c=" << std::fixed << std::setprecision(1) << tc << "\n";
        }
        std::cout << "current_a=" << std::fixed << std::setprecision(2)
                  << (a.current_10ma / 100.0) << "\n";
        std::cout << "total_voltage_mv=" << a.total_volt_mv << "\n";
        std::cout << "remain_cap_mah=" << a.remain_cap_mah << "\n";
        std::cout << "full_cap_mah="   << a.full_cap_mah   << "\n";
        std::cout << "design_cap_mah=" << a.design_cap_mah << "\n";
        std::cout << "cycle="          << a.cycle          << "\n";
        return 0;
    }

    // --- alarm ---
    if (subcmd[0] == "alarm") {
        AlarmData al;
        if (!bms.get_alarm(pack, al, err)) { std::cerr << err << "\n"; return 1; }
        std::cout << "pack=" << (int)pack << "\n";
        std::cout << "cell_count=" << al.cell_count << "\n";
        for (int k = 0; k < al.cell_count; ++k)
            std::cout << "cell_" << (k+1) << "_volt_alarm=" << (int)al.cell_volt_alarm[k] << "\n";
        std::cout << "temp_count=" << al.temp_count << "\n";
        for (int k = 0; k < al.temp_count; ++k)
            std::cout << "temp_" << (k+1) << "_alarm=" << (int)al.temp_alarm[k] << "\n";
        std::cout << "charge_curr_state="    << (int)al.charge_curr_state    << "\n";
        std::cout << "total_volt_state="     << (int)al.total_volt_state     << "\n";
        std::cout << "discharge_curr_state=" << (int)al.discharge_curr_state << "\n";
        for (int k = 0; k < 9; ++k)
            std::cout << "status" << (k+1) << "=0x"
                      << std::hex << std::setw(2) << std::setfill('0')
                      << (int)al.status[k] << std::dec << "\n";
        return 0;
    }

    // --- params ---
    if (subcmd[0] == "params") {
        if (subcmd.size() < 2) { usage(argv[0]); return 1; }

        if (subcmd[1] == "get") {
            BalanceParams bp;
            if (!bms.get_balance(bp, err)) { std::cerr << err << "\n"; return 1; }
            std::cout << std::fixed << std::setprecision(3)
                      << "balance_threshold_v=" << bp.threshold_v << "\n"
                      << "balance_delta_mv="    << bp.delta_mv    << "\n";
            return 0;
        }

        if (subcmd[1] == "set") {
            BalanceParams bp;
            bool has_thresh = false, has_delta = false;
            for (size_t i = 2; i < subcmd.size(); ++i) {
                std::string v = get_kv(subcmd[i], "balance-threshold");
                if (!v.empty()) { bp.threshold_v = std::stod(v); has_thresh = true; continue; }
                v = get_kv(subcmd[i], "balance-delta");
                if (!v.empty()) { bp.delta_mv = std::stoi(v); has_delta = true; continue; }
                std::cerr << "unknown param: " << subcmd[i] << "\n"; return 1;
            }
            if (!has_thresh && !has_delta) {
                std::cerr << "params set: specify balance-threshold and/or balance-delta\n";
                return 1;
            }
            // Read current values for any param not specified
            if (!has_thresh || !has_delta) {
                BalanceParams cur;
                if (!bms.get_balance(cur, err)) { std::cerr << err << "\n"; return 1; }
                if (!has_thresh) bp.threshold_v = cur.threshold_v;
                if (!has_delta)  bp.delta_mv    = cur.delta_mv;
            }
            if (!bms.set_balance(bp, err)) { std::cerr << err << "\n"; return 1; }
            std::cout << "ok\n";
            return 0;
        }

        usage(argv[0]); return 1;
    }

    usage(argv[0]);
    return 1;
}
```

- [ ] **Step 2: Build the complete binary**

```bash
cmake --build build 2>&1
```

Expected: zero errors. Binary at `build/pbmstool`.

- [ ] **Step 3: Smoke-test the CLI without hardware (expect "cannot open")**

```bash
./build/pbmstool --port /dev/null --pack 1 discover
```

Expected: `cannot open /dev/null: ...` or a timeout error on stderr, exit code 1.

```bash
./build/pbmstool
```

Expected: usage message on stderr, exit code 1.

- [ ] **Step 4: Commit**

```bash
git add pbmstool/src/main.cpp
git commit -m "feat: implement CLI subcommands (discover/info/analog/alarm/params)"
```

---

## Task 9: End-to-End Hardware Verification

No automated tests — verify manually against a real Pace BMS pack.

- [ ] **Step 1: Connect hardware and verify discover**

```bash
./build/pbmstool --port /dev/ttyUSB0 --baud 9600 discover
```

Expected output (example 3-pack system):
```
pack_count=3
```

- [ ] **Step 2: Read version info**

```bash
./build/pbmstool --port /dev/ttyUSB0 --pack 1 info
```

Expected:
```
pack=1
version=<firmware version string>
```

- [ ] **Step 3: Read analog data**

```bash
./build/pbmstool --port /dev/ttyUSB0 --pack 1 analog
```

Expected: cell voltages between 2500–4200 mV each, temperatures near ambient.

- [ ] **Step 4: Read balance params**

```bash
./build/pbmstool --port /dev/ttyUSB0 params get
```

Expected:
```
balance_threshold_v=3.400
balance_delta_mv=20
```
(exact values depend on BMS configuration)

- [ ] **Step 5: Round-trip set/get balance params**

```bash
# Read current values first
./build/pbmstool --port /dev/ttyUSB0 params get

# Set new values
./build/pbmstool --port /dev/ttyUSB0 params set balance-threshold=3.400 balance-delta=20

# Confirm
./build/pbmstool --port /dev/ttyUSB0 params get
```

Expected: `ok` after set, then get returns the values just written.

- [ ] **Step 6: Final commit**

```bash
git commit --allow-empty -m "chore: pbmstool v1 hardware verified"
```
