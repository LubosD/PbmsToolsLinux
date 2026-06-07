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
