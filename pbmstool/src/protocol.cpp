#include "protocol.h"
#include <cstdio>

namespace pace {

namespace {

std::string lchksum(int ascii_len) {
    // ascii_len is limited to 12 bits (max 4095 ASCII chars = 2047 data bytes)
    if (ascii_len > 0x0FFF) ascii_len = 0x0FFF; // clamp; BMS commands never approach this limit
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

    auto parse_hex_byte = [&](const char* s) -> int {
        try { return static_cast<int>(std::stoul(s, nullptr, 16)); }
        catch (...) { return -1; }
    };

    // ADR at [3..4]
    char adr_str[3] = { static_cast<char>(raw[3]), static_cast<char>(raw[4]), 0 };
    int adr_val = parse_hex_byte(adr_str);
    if (adr_val < 0) { err = "invalid ADR field"; return false; }
    out.adr = static_cast<uint8_t>(adr_val);

    // RTN at [7..8]
    char rtn_str[3] = { static_cast<char>(raw[7]), static_cast<char>(raw[8]), 0 };
    int rtn_val = parse_hex_byte(rtn_str);
    if (rtn_val < 0) { err = "invalid RTN field"; return false; }
    out.rtn = static_cast<uint8_t>(rtn_val);

    // DATA: ASCII hex at [13 .. size-6], decode to bytes
    if (raw.size() > 18) {
        size_t data_ascii_len = raw.size() - 18;
        if (data_ascii_len % 2 != 0) {
            err = "odd data field length";
            return false;
        }
        out.data.resize(data_ascii_len / 2);
        for (size_t i = 0; i < out.data.size(); ++i) {
            char nibbles[3] = { static_cast<char>(raw[13 + 2*i]),
                                static_cast<char>(raw[13 + 2*i + 1]), 0 };
            int val = parse_hex_byte(nibbles);
            if (val < 0) { err = "invalid DATA field at byte " + std::to_string(i); return false; }
            out.data[i] = static_cast<uint8_t>(val);
        }
    }

    return true;
}

} // namespace pace
