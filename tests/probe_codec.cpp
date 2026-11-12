#include "../src/probe_codec.hpp"
#include <cassert>
std::vector<unsigned char> Unpack(const std::vector<unsigned char>& packed) {
    std::vector<unsigned char> out; std::size_t pos = 0;
    while (pos < packed.size()) {
        const auto flags = packed[pos++];
        for (unsigned bit = 0; bit < 8 && pos < packed.size(); ++bit) {
            if (flags & (1u<<bit)) {
                assert(pos+1 < packed.size());
                const unsigned token = packed[pos] | (static_cast<unsigned>(packed[pos+1])<<8); pos += 2;
                const unsigned distance = (token & 4095)+1, length = (token>>12)+3;
                assert(distance <= out.size());
                for (unsigned n = 0; n < length; ++n) out.push_back(out[out.size()-distance]);
            } else out.push_back(packed[pos++]);
        }
    }
    return out;
}
int main() {
    assert(probecodec::Base64({}) == "");
    assert(probecodec::Base64({102}) == "Zg==");
    assert(probecodec::Base64({102,111}) == "Zm8=");
    assert(probecodec::Base64({102,111,111}) == "Zm9v");
    std::vector<unsigned char> data;
    std::uint32_t random = 12345;
    for (unsigned i = 0; i < 8192; ++i) { random = random*1664525+1013904223; data.push_back(static_cast<unsigned char>(random>>24)); }
    assert(Unpack(probecodec::Pack(data)) == data);
    data.assign(8192, 0xCC);
    auto packed = probecodec::Pack(data);
    assert(Unpack(packed) == data && packed.size() < data.size()/4);
    for (unsigned i = 0; i < data.size(); ++i) data[i] = static_cast<unsigned char>(i%251);
    assert(Unpack(probecodec::Pack(data)) == data);
    return 0;
}
