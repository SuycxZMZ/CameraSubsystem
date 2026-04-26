#ifndef WEB_PREVIEW_BASE64_H
#define WEB_PREVIEW_BASE64_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace web_preview {

std::string Base64Encode(const uint8_t* data, size_t length);

} // namespace web_preview

#endif // WEB_PREVIEW_BASE64_H
