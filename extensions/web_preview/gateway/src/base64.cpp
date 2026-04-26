#include "web_preview/base64.h"

namespace web_preview {

std::string Base64Encode(const uint8_t* data, size_t length)
{
    static constexpr char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    out.reserve(((length + 2) / 3) * 4);

    for (size_t i = 0; i < length; i += 3)
    {
        const uint32_t b0 = data[i];
        const uint32_t b1 = (i + 1 < length) ? data[i + 1] : 0;
        const uint32_t b2 = (i + 2 < length) ? data[i + 2] : 0;
        const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;

        out.push_back(kTable[(triple >> 18) & 0x3f]);
        out.push_back(kTable[(triple >> 12) & 0x3f]);
        out.push_back((i + 1 < length) ? kTable[(triple >> 6) & 0x3f] : '=');
        out.push_back((i + 2 < length) ? kTable[triple & 0x3f] : '=');
    }

    return out;
}

} // namespace web_preview
