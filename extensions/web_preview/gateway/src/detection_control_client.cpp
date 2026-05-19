#include "web_preview/detection_control_client.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <optional>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace web_preview {
namespace {

constexpr int kCommandTimeoutMs = 3000;

bool WriteFull(int fd, const void* buffer, size_t length)
{
    size_t total = 0;
    const auto* data = static_cast<const uint8_t*>(buffer);
    while (total < length)
    {
        const ssize_t ret = write(fd, data + total, length - total);
        if (ret < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        if (ret == 0)
        {
            return false;
        }
        total += static_cast<size_t>(ret);
    }
    return true;
}

bool ReadLine(int fd, std::string* line, int timeout_ms)
{
    if (!line)
    {
        return false;
    }
    line->clear();

    char ch = '\0';
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (true)
    {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
        {
            return false;
        }

        pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        const int remaining_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
        const int poll_ret = poll(&pfd, 1, remaining_ms);

        if (poll_ret == 0)
        {
            return false; // timeout
        }
        if (poll_ret < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }

        const ssize_t ret = read(fd, &ch, 1);
        if (ret == 0)
        {
            return false;
        }
        if (ret < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        if (ch == '\n')
        {
            return true;
        }
        line->push_back(ch);
    }
}

// JSON 解析辅助函数
bool ExtractStringField(const std::string& json, const std::string& key, std::string* value)
{
    if (!value)
    {
        return false;
    }
    const std::string needle = "\"" + key + "\":\"";
    const size_t start = json.find(needle);
    if (start == std::string::npos)
    {
        return false;
    }

    std::string result;
    bool escape = false;
    for (size_t i = start + needle.size(); i < json.size(); ++i)
    {
        const char ch = json[i];
        if (escape)
        {
            switch (ch)
            {
            case '"':
            case '\\':
            case '/':
                result.push_back(ch);
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            default:
                result.push_back(ch);
                break;
            }
            escape = false;
            continue;
        }

        if (ch == '\\')
        {
            escape = true;
            continue;
        }
        if (ch == '"')
        {
            *value = result;
            return true;
        }
        result.push_back(ch);
    }
    return false;
}

bool ExtractUint32Field(const std::string& json, const std::string& key, uint32_t* value)
{
    if (!value)
    {
        return false;
    }
    const std::string needle = "\"" + key + "\":";
    const size_t start = json.find(needle);
    if (start == std::string::npos)
    {
        return false;
    }
    const char* begin = json.c_str() + start + needle.size();
    char* end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(begin, &end, 10);
    if (begin == end || errno != 0 || parsed > 0xffffffffUL)
    {
        return false;
    }
    *value = static_cast<uint32_t>(parsed);
    return true;
}

bool ExtractUint64Field(const std::string& json, const std::string& key, uint64_t* value)
{
    if (!value)
    {
        return false;
    }
    const std::string needle = "\"" + key + "\":";
    const size_t start = json.find(needle);
    if (start == std::string::npos)
    {
        return false;
    }
    const char* begin = json.c_str() + start + needle.size();
    char* end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(begin, &end, 10);
    if (begin == end || errno != 0)
    {
        return false;
    }
    *value = static_cast<uint64_t>(parsed);
    return true;
}

bool ExtractDoubleField(const std::string& json, const std::string& key, double* value)
{
    if (!value)
    {
        return false;
    }
    const std::string needle = "\"" + key + "\":";
    const size_t start = json.find(needle);
    if (start == std::string::npos)
    {
        return false;
    }
    const char* begin = json.c_str() + start + needle.size();
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(begin, &end);
    if (begin == end || errno != 0)
    {
        return false;
    }
    *value = parsed;
    return true;
}

bool ExtractBoolField(const std::string& json, const std::string& key, bool* value)
{
    if (!value)
    {
        return false;
    }
    const std::string true_needle = "\"" + key + "\":true";
    const std::string false_needle = "\"" + key + "\":false";
    if (json.find(true_needle) != std::string::npos)
    {
        *value = true;
        return true;
    }
    if (json.find(false_needle) != std::string::npos)
    {
        *value = false;
        return true;
    }
    return false;
}

} // namespace

DetectionControlClient::DetectionControlClient(const std::string& socket_path)
    : socket_path_(socket_path)
{
}

int DetectionControlClient::Connect()
{
    last_errno_ = 0;
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        last_errno_ = errno;
        return -1;
    }

    sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        last_errno_ = errno;
        close(fd);
        return -1;
    }

    return fd;
}

std::string DetectionControlClient::BuildRequestId()
{
    return "web-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
           "-" + std::to_string(++request_counter_);
}

std::string DetectionControlClient::SendCommand(const std::string& json_line)
{
    const int fd = Connect();
    if (fd < 0)
    {
        return "";
    }

    const std::string line = json_line + "\n";
    if (!WriteFull(fd, line.data(), line.size()))
    {
        last_errno_ = errno;
        close(fd);
        return "";
    }

    std::string response;
    if (!ReadLine(fd, &response, kCommandTimeoutMs))
    {
        last_errno_ = errno != 0 ? errno : ETIMEDOUT;
        close(fd);
        return "";
    }

    last_errno_ = 0;
    close(fd);
    return response;
}

DetectionStatusResult DetectionControlClient::GetDetectionStatus()
{
    std::lock_guard<std::mutex> lock(mutex_);

    DetectionStatusResult result;

    const std::string request = "{\"type\":\"get_detection_status\",\"request_id\":\"" +
                                BuildRequestId() + "\"}";

    const std::string response = SendCommand(request);
    if (response.empty())
    {
        result.available = false;
        result.error = "connect failed: " + std::string(std::strerror(last_errno_));
        return result;
    }

    // 解析响应
    result.available = true;

    ExtractStringField(response, "state", &result.state);
    ExtractStringField(response, "model_name", &result.model_name);
    ExtractUint32Field(response, "npu_core_mask", &result.npu_core_mask);

    // 解析 config 对象
    std::string config_str;
    if (ExtractStringField(response, "config", &config_str))
    {
        // config 是嵌套对象，需要从 response 中找到 config 块
    }
    // 直接从 response 中提取 config 字段
    const size_t config_start = response.find("\"config\":{");
    if (config_start != std::string::npos)
    {
        const std::string config_section = response.substr(config_start);
        ExtractUint32Field(config_section, "infer_every_n_frames", &result.config.infer_every_n_frames);
        ExtractDoubleField(config_section, "score_threshold", &result.config.score_threshold);
        ExtractDoubleField(config_section, "nms_threshold", &result.config.nms_threshold);
    }

    // 解析 metrics 对象
    const size_t metrics_start = response.find("\"metrics\":{");
    if (metrics_start != std::string::npos)
    {
        const std::string metrics_section = response.substr(metrics_start);
        ExtractUint64Field(metrics_section, "input_frames", &result.metrics.input_frames);
        ExtractUint64Field(metrics_section, "inferred_frames", &result.metrics.inferred_frames);
        ExtractUint32Field(metrics_section, "last_object_count", &result.metrics.last_object_count);
    }

    // 检查是否有错误
    ExtractStringField(response, "last_error", &result.error);

    return result;
}

DetectionControlResult DetectionControlClient::StartDetection()
{
    std::lock_guard<std::mutex> lock(mutex_);

    DetectionControlResult result;

    const std::string request =
        "{\"type\":\"start_detection\",\"request_id\":\"" + BuildRequestId() + "\"}";

    const std::string response = SendCommand(request);
    if (response.empty())
    {
        result.ok = false;
        result.error_code = "connect_failed";
        result.message = "connect failed: " + std::string(std::strerror(last_errno_));
        return result;
    }

    ExtractBoolField(response, "ok", &result.ok);
    ExtractStringField(response, "error_code", &result.error_code);
    ExtractStringField(response, "message", &result.message);
    ExtractStringField(response, "state", &result.state);

    return result;
}

DetectionControlResult DetectionControlClient::StopDetection()
{
    std::lock_guard<std::mutex> lock(mutex_);

    DetectionControlResult result;

    const std::string request =
        "{\"type\":\"stop_detection\",\"request_id\":\"" + BuildRequestId() + "\"}";

    const std::string response = SendCommand(request);
    if (response.empty())
    {
        result.ok = false;
        result.error_code = "connect_failed";
        result.message = "connect failed: " + std::string(std::strerror(last_errno_));
        return result;
    }

    ExtractBoolField(response, "ok", &result.ok);
    ExtractStringField(response, "error_code", &result.error_code);
    ExtractStringField(response, "message", &result.message);
    ExtractStringField(response, "state", &result.state);

    return result;
}

DetectionControlResult DetectionControlClient::SetDetectionConfig(
    std::optional<uint32_t> infer_every_n_frames,
    std::optional<double> score_threshold,
    std::optional<double> nms_threshold)
{
    std::lock_guard<std::mutex> lock(mutex_);

    DetectionControlResult result;

    std::string request = "{\"type\":\"set_detection_config\",\"request_id\":\"" +
                          BuildRequestId() + "\"";
    if (infer_every_n_frames.has_value())
    {
        request += ",\"infer_every_n_frames\":" + std::to_string(*infer_every_n_frames);
    }
    if (score_threshold.has_value())
    {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.6f", *score_threshold);
        request += ",\"score_threshold\":";
        request += buffer;
    }
    if (nms_threshold.has_value())
    {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.6f", *nms_threshold);
        request += ",\"nms_threshold\":";
        request += buffer;
    }
    request += "}";

    const std::string response = SendCommand(request);
    if (response.empty())
    {
        result.ok = false;
        result.error_code = "connect_failed";
        result.message = "connect failed: " + std::string(std::strerror(last_errno_));
        return result;
    }

    ExtractBoolField(response, "ok", &result.ok);
    ExtractStringField(response, "error_code", &result.error_code);
    ExtractStringField(response, "message", &result.message);
    ExtractStringField(response, "state", &result.state);

    return result;
}

} // namespace web_preview
