#pragma once
#include <vector>
#include <string>
#include <cstdint>
namespace probecodec {
// LZSS: per 8 tokens one flag byte, bit 1 => 16-bit LE match token.
// Token high nibble is length-3, low 12 bits are backward distance-1.
inline std::vector<unsigned char> Pack(const std::vector<unsigned char>& input) {
    std::vector<unsigned char> out;
    std::size_t pos = 0;
    while (pos < input.size()) {
        const auto flag = out.size(); out.push_back(0);
        for (unsigned bit = 0; bit < 8 && pos < input.size(); ++bit) {
            std::size_t best = 0, distance = 0;
            const auto limit = pos < 4096 ? pos : 4096;
            for (std::size_t d = 1; d <= limit; ++d) {
                std::size_t n = 0;
                while (n < 18 && pos+n < input.size() && input[pos+n] == input[pos+n-d]) ++n;
                if (n > best) { best = n; distance = d; }
                if (best == 18) break;
            }
            if (best >= 3) {
                const auto token = static_cast<std::uint16_t>(((best-3)<<12)|(distance-1));
                out[flag] |= static_cast<unsigned char>(1u<<bit);
                out.push_back(static_cast<unsigned char>(token & 255));
                out.push_back(static_cast<unsigned char>(token>>8)); pos += best;
            } else out.push_back(input[pos++]);
        }
    }
    return out;
}
inline std::string Base64(const std::vector<unsigned char>& bytes) {
    const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned a = bytes[i], b = i+1 < bytes.size() ? bytes[i+1] : 0, c = i+2 < bytes.size() ? bytes[i+2] : 0;
        const unsigned word = (a<<16)|(b<<8)|c;
        out += alphabet[(word>>18)&63]; out += alphabet[(word>>12)&63];
        out += i+1 < bytes.size() ? alphabet[(word>>6)&63] : '=';
        out += i+2 < bytes.size() ? alphabet[word&63] : '=';
    }
    return out;
}
}
