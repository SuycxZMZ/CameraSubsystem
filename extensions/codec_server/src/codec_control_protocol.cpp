#include "codec_server/codec_control_protocol.h"

#include <cstdlib>
#include <sstream>
#include <utility>

namespace camera_subsystem::extensions::codec_server {
namespace {

std::string JsonEscape(const std::string& value)
{
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value)
    {
        switch (c)
        {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
        }
    }
    return out;
}

bool ExtractStringField(const std::string& line,
                        const std::string& key,
                        std::string* value)
{
    const std::string pattern = "\"" + key + "\"";
    size_t pos = line.find(pattern);
    if (pos == std::string::npos)
    {
        return false;
    }
    pos = line.find(':', pos + pattern.size());
    if (pos == std::string::npos)
    {
        return false;
    }
    pos = line.find('"', pos + 1);
    if (pos == std::string::npos)
    {
        return false;
    }

    std::string out;
    bool escaped = false;
    for (++pos; pos < line.size(); ++pos)
    {
        const char c = line[pos];
        if (escaped)
        {
            switch (c)
            {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            default: out += c; break;
            }
            escaped = false;
            continue;
        }
        if (c == '\\')
        {
            escaped = true;
            continue;
        }
        if (c == '"')
        {
            *value = out;
            return true;
        }
        out += c;
    }
    return false;
}

bool ExtractUint32Field(const std::string& line,
                        const std::string& key,
                        uint32_t* value)
{
    const std::string pattern = "\"" + key + "\"";
    size_t pos = line.find(pattern);
    if (pos == std::string::npos)
    {
        return false;
    }
    pos = line.find(':', pos + pattern.size());
    if (pos == std::string::npos)
    {
        return false;
    }
    ++pos;
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
    {
        ++pos;
    }

    char* end = nullptr;
    const unsigned long parsed = std::strtoul(line.c_str() + pos, &end, 10);
    if (end == line.c_str() + pos || parsed > 0xffffffffUL)
    {
        return false;
    }
    *value = static_cast<uint32_t>(parsed);
    return true;
}

CodecControlCommand ParseCommand(const std::string& type)
{
    if (type == "start_recording")
    {
        return CodecControlCommand::kStartRecording;
    }
    if (type == "stop_recording")
    {
        return CodecControlCommand::kStopRecording;
    }
    if (type == "status")
    {
        return CodecControlCommand::kStatus;
    }
    return CodecControlCommand::kUnknown;
}

} // namespace

bool ParseCodecControlRequestLine(const std::string& line,
                                  CodecControlRequest* request,
                                  std::string* error)
{
    if (!request)
    {
        if (error)
        {
            *error = "invalid_request";
        }
        return false;
    }

    CodecControlRequest parsed;
    std::string type;
    if (!ExtractStringField(line, "type", &type))
    {
        if (error)
        {
            *error = "missing_type";
        }
        return false;
    }

    parsed.command = ParseCommand(type);
    if (parsed.command == CodecControlCommand::kUnknown)
    {
        if (error)
        {
            *error = "unknown_command";
        }
        return false;
    }

    (void)ExtractStringField(line, "request_id", &parsed.request_id);
    (void)ExtractStringField(line, "stream_id", &parsed.stream_id);
    (void)ExtractStringField(line, "codec", &parsed.codec);
    (void)ExtractStringField(line, "container", &parsed.container);
    (void)ExtractStringField(line, "output_dir", &parsed.output_dir);

    (void)ExtractUint32Field(line, "width", &parsed.profile.width);
    (void)ExtractUint32Field(line, "height", &parsed.profile.height);
    (void)ExtractUint32Field(line, "fps", &parsed.profile.fps);
    (void)ExtractUint32Field(line, "bitrate", &parsed.profile.bitrate);
    (void)ExtractUint32Field(line, "gop", &parsed.profile.gop);

    if (parsed.stream_id.empty())
    {
        if (error)
        {
            *error = "missing_stream_id";
        }
        return false;
    }
    if (parsed.codec != "h264")
    {
        if (error)
        {
            *error = "unsupported_codec";
        }
        return false;
    }
    if (parsed.container != "raw_h264")
    {
        if (error)
        {
            *error = "unsupported_container";
        }
        return false;
    }

    *request = std::move(parsed);
    return true;
}

std::string SerializeCodecControlStatus(const CodecControlStatus& status)
{
    std::ostringstream oss;
    oss << "{\"type\":\"record_status\""
        << ",\"request_id\":\"" << JsonEscape(status.request_id) << "\""
        << ",\"stream_id\":\"" << JsonEscape(status.stream_id) << "\""
        << ",\"recording\":" << (status.recording ? "true" : "false")
        << ",\"state\":\"" << JsonEscape(status.state) << "\""
        << ",\"codec\":\"" << JsonEscape(status.codec) << "\""
        << ",\"container\":\"" << JsonEscape(status.container) << "\""
        << ",\"file\":\"" << JsonEscape(status.file) << "\""
        << ",\"encoded_frames\":" << status.encoded_frames
        << ",\"decoded_frames\":" << status.decoded_frames
        << ",\"dropped_frames\":" << status.dropped_frames
        << ",\"input_frames\":" << status.input_frames
        << ",\"decode_failures\":" << status.decode_failures
        << ",\"write_failures\":" << status.write_failures;
    if (!status.error.empty())
    {
        oss << ",\"error\":\"" << JsonEscape(status.error) << "\"";
    }
    oss << "}";
    return oss.str();
}

} // namespace camera_subsystem::extensions::codec_server
