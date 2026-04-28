#ifndef CODEC_SERVER_RECORDING_SESSION_MANAGER_H
#define CODEC_SERVER_RECORDING_SESSION_MANAGER_H

#include "codec_server/codec_control_protocol.h"
#include "codec_server/recording_file_writer.h"

#include <mutex>
#include <string>

namespace camera_subsystem::extensions::codec_server {

struct RecordingSessionConfig
{
    std::string default_output_dir = "/home/luckfox/recordings";
};

class RecordingSessionManager
{
public:
    explicit RecordingSessionManager(RecordingSessionConfig config);

    CodecControlStatus StartRecording(const CodecControlRequest& request);
    CodecControlStatus StopRecording(const CodecControlRequest& request);
    CodecControlStatus GetStatus(const CodecControlRequest& request) const;

private:
    CodecControlStatus BuildStatusLocked(const CodecControlRequest& request,
                                         const std::string& error) const;
    static std::string MapWriterError(WriterResult result);

    RecordingSessionConfig config_;
    mutable std::mutex mutex_;
    RecordingFileWriter writer_;
    std::string state_ = "idle";
    std::string stream_id_;
    std::string file_path_;
    uint64_t encoded_frames_ = 0;
    uint64_t dropped_frames_ = 0;
    uint64_t input_frames_ = 0;
    std::string last_error_;
};

} // namespace camera_subsystem::extensions::codec_server

#endif // CODEC_SERVER_RECORDING_SESSION_MANAGER_H
