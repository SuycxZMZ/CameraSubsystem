#ifndef WEB_PREVIEW_SHA1_H
#define WEB_PREVIEW_SHA1_H

#include <array>
#include <cstdint>
#include <string>

namespace web_preview {

std::array<uint8_t, 20> Sha1(const std::string& input);

} // namespace web_preview

#endif // WEB_PREVIEW_SHA1_H
