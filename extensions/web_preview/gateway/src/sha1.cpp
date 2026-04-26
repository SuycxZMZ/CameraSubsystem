#include "web_preview/sha1.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace web_preview {
namespace {

uint32_t RotateLeft(uint32_t value, uint32_t bits)
{
    return (value << bits) | (value >> (32U - bits));
}

uint32_t ReadBigEndian32(const uint8_t* data)
{
    return (static_cast<uint32_t>(data[0]) << 24U) |
           (static_cast<uint32_t>(data[1]) << 16U) |
           (static_cast<uint32_t>(data[2]) << 8U) |
           static_cast<uint32_t>(data[3]);
}

void WriteBigEndian32(uint32_t value, uint8_t* out)
{
    out[0] = static_cast<uint8_t>((value >> 24U) & 0xffU);
    out[1] = static_cast<uint8_t>((value >> 16U) & 0xffU);
    out[2] = static_cast<uint8_t>((value >> 8U) & 0xffU);
    out[3] = static_cast<uint8_t>(value & 0xffU);
}

} // namespace

std::array<uint8_t, 20> Sha1(const std::string& input)
{
    std::vector<uint8_t> message(input.begin(), input.end());
    const uint64_t bit_length = static_cast<uint64_t>(message.size()) * 8U;

    message.push_back(0x80U);
    while ((message.size() % 64U) != 56U)
    {
        message.push_back(0);
    }

    for (int i = 7; i >= 0; --i)
    {
        message.push_back(static_cast<uint8_t>((bit_length >> (i * 8)) & 0xffU));
    }

    uint32_t h0 = 0x67452301U;
    uint32_t h1 = 0xefcdab89U;
    uint32_t h2 = 0x98badcfeU;
    uint32_t h3 = 0x10325476U;
    uint32_t h4 = 0xc3d2e1f0U;

    for (size_t offset = 0; offset < message.size(); offset += 64)
    {
        uint32_t w[80];
        std::memset(w, 0, sizeof(w));
        for (int i = 0; i < 16; ++i)
        {
            w[i] = ReadBigEndian32(&message[offset + static_cast<size_t>(i) * 4U]);
        }
        for (int i = 16; i < 80; ++i)
        {
            w[i] = RotateLeft(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = h0;
        uint32_t b = h1;
        uint32_t c = h2;
        uint32_t d = h3;
        uint32_t e = h4;

        for (int i = 0; i < 80; ++i)
        {
            uint32_t f = 0;
            uint32_t k = 0;
            if (i < 20)
            {
                f = (b & c) | ((~b) & d);
                k = 0x5a827999U;
            }
            else if (i < 40)
            {
                f = b ^ c ^ d;
                k = 0x6ed9eba1U;
            }
            else if (i < 60)
            {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8f1bbcdcU;
            }
            else
            {
                f = b ^ c ^ d;
                k = 0xca62c1d6U;
            }

            const uint32_t temp = RotateLeft(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = RotateLeft(b, 30);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    std::array<uint8_t, 20> digest{};
    WriteBigEndian32(h0, &digest[0]);
    WriteBigEndian32(h1, &digest[4]);
    WriteBigEndian32(h2, &digest[8]);
    WriteBigEndian32(h3, &digest[12]);
    WriteBigEndian32(h4, &digest[16]);
    return digest;
}

} // namespace web_preview
