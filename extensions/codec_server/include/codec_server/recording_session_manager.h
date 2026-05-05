#ifndef CODEC_SERVER_RECORDING_SESSION_MANAGER_H
#define CODEC_SERVER_RECORDING_SESSION_MANAGER_H

#include "codec_server/codec_control_protocol.h"
#include "codec_server/camera_stream_subscriber.h"
#include "codec_server/h264_mpp_encoder.h"
#include "codec_server/jpeg_decode_stage.h"
#include "codec_server/recording_file_writer.h"

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "camera_subsystem/ipc/camera_data_ipc.h"

namespace camera_subsystem::extensions::codec_server {

struct RecordingSessionConfig
{
    std::string default_output_dir = "/home/luckfox/CameraSubsystem/recordings";
    CameraStreamSubscriberConfig subscriber;
    bool enable_camera_subscriber = false;
    uint32_t fps = 30;
    uint32_t bitrate = 4000000;
    uint32_t gop = 60;
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
    void HandleInputFrame(const camera_subsystem::ipc::CameraDataFrameHeader& header,
                          const std::vector<uint8_t>& payload);
    H264EncoderConfig BuildEncoderConfig(const DecodedImageFrame& frame) const;
    static std::string MapWriterError(WriterResult result);

    RecordingSessionConfig config_;
    mutable std::mutex mutex_;
    mutable std::mutex pipeline_mutex_;
    RecordingFileWriter writer_;
    CameraStreamSubscriber subscriber_;
    JpegDecodeStage jpeg_decoder_;
    H264MppEncoder h264_encoder_;
    std::string state_ = "idle";
    std::string stream_id_;
    std::string file_path_;
    CodecControlProfile active_profile_;
    std::atomic<uint64_t> encoded_frames_{0};
    std::atomic<uint64_t> dropped_frames_{0};
    uint64_t input_frames_ = 0;
    std::atomic<uint64_t> decoded_frames_{0};
    std::atomic<uint64_t> decode_failures_{0};
    std::string last_error_;
};

} // namespace camera_subsystem::extensions::codec_server

#endif // CODEC_SERVER_RECORDING_SESSION_MANAGER_H
