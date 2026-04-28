#include "codec_server/recording_session_manager.h"

#include <utility>

namespace camera_subsystem::extensions::codec_server {

RecordingSessionManager::RecordingSessionManager(RecordingSessionConfig config)
    : config_(std::move(config))
{
}

CodecControlStatus RecordingSessionManager::StartRecording(
    const CodecControlRequest& request)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ == "recording" || state_ == "starting" || state_ == "stopping")
    {
        return BuildStatusLocked(request, "already_recording");
    }

    state_ = "starting";
    stream_id_ = request.stream_id;
    encoded_frames_.store(0);
    dropped_frames_.store(0);
    input_frames_ = 0;
    decoded_frames_.store(0);
    decode_failures_.store(0);
    last_error_.clear();

    const std::string output_dir =
        request.output_dir.empty() ? config_.default_output_dir : request.output_dir;
    const WriterResult result = writer_.Open(request.stream_id, output_dir);
    if (result != WriterResult::kOk)
    {
        state_ = "error";
        last_error_ = MapWriterError(result);
        file_path_.clear();
        return BuildStatusLocked(request, last_error_);
    }

    file_path_ = writer_.GetFilePath();
    if (config_.enable_camera_subscriber)
    {
        CameraStreamSubscriberConfig subscriber_config = config_.subscriber;
        subscriber_config.client_id = "camera_codec_server_" + request.stream_id;
        subscriber_config.frame_callback =
            [this](const camera_subsystem::ipc::CameraDataFrameHeader& header,
                   const std::vector<uint8_t>& payload) {
                HandleInputFrame(header, payload);
            };
        if (!subscriber_.Start(subscriber_config))
        {
            (void)writer_.Close();
            state_ = "error";
            last_error_ = "stream_not_found";
            return BuildStatusLocked(request, last_error_);
        }
    }

    state_ = "recording";
    return BuildStatusLocked(request, std::string());
}

CodecControlStatus RecordingSessionManager::StopRecording(
    const CodecControlRequest& request)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ == "idle")
    {
        return BuildStatusLocked(request, "not_recording");
    }
    if (state_ == "error")
    {
        state_ = "idle";
        return BuildStatusLocked(request, last_error_);
    }

    state_ = "stopping";
    subscriber_.Stop();
    {
        std::lock_guard<std::mutex> pipeline_lock(pipeline_mutex_);
        h264_encoder_.Close();
    }
    const WriterResult close_result = writer_.Close();
    if (close_result != WriterResult::kOk)
    {
        state_ = "error";
        last_error_ = MapWriterError(close_result);
        return BuildStatusLocked(request, last_error_);
    }

    state_ = "idle";
    return BuildStatusLocked(request, std::string());
}

CodecControlStatus RecordingSessionManager::GetStatus(
    const CodecControlRequest& request) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return BuildStatusLocked(request, std::string());
}

CodecControlStatus RecordingSessionManager::BuildStatusLocked(
    const CodecControlRequest& request,
    const std::string& error) const
{
    const CameraStreamSubscriberStats subscriber_stats = subscriber_.GetStats();
    std::lock_guard<std::mutex> pipeline_lock(pipeline_mutex_);
    const WriterStats stats = writer_.GetStats();
    CodecControlStatus status;
    status.request_id = request.request_id;
    status.stream_id = stream_id_.empty() ? request.stream_id : stream_id_;
    status.recording = state_ == "recording";
    status.state = state_;
    status.codec = request.codec.empty() ? "h264" : request.codec;
    status.container = request.container.empty() ? "raw_h264" : request.container;
    status.file = file_path_;
    status.encoded_frames = encoded_frames_.load();
    status.decoded_frames = decoded_frames_.load();
    status.dropped_frames = dropped_frames_.load();
    status.input_frames = config_.enable_camera_subscriber
                              ? subscriber_stats.input_frames
                              : input_frames_;
    status.decode_failures = decode_failures_.load();
    status.write_failures = stats.write_failures + subscriber_stats.read_failures;
    status.error = error;
    return status;
}

void RecordingSessionManager::HandleInputFrame(
    const camera_subsystem::ipc::CameraDataFrameHeader& header,
    const std::vector<uint8_t>& payload)
{
    (void)header;

    std::lock_guard<std::mutex> pipeline_lock(pipeline_mutex_);

    DecodedImageFrame decoded;
    const JpegDecodeResult decode_result =
        jpeg_decoder_.Decode(payload.data(), payload.size(), &decoded);
    if (decode_result == JpegDecodeResult::kOk)
    {
        decoded_frames_.fetch_add(1);
    }
    else
    {
        decode_failures_.fetch_add(1);
        return;
    }

    if (!h264_encoder_.IsOpen())
    {
        const H264EncodeResult open_result =
            h264_encoder_.Open(BuildEncoderConfig(decoded));
        if (open_result != H264EncodeResult::kOk)
        {
            dropped_frames_.fetch_add(1);
            return;
        }
    }

    std::vector<EncodedPacket> packets;
    const H264EncodeResult encode_result = h264_encoder_.EncodeFrame(decoded, &packets);
    if (encode_result != H264EncodeResult::kOk)
    {
        dropped_frames_.fetch_add(1);
        return;
    }

    for (const EncodedPacket& packet : packets)
    {
        const WriterResult write_result =
            writer_.Write(packet.payload.data(), packet.payload.size());
        if (write_result != WriterResult::kOk)
        {
            dropped_frames_.fetch_add(1);
            return;
        }
    }
    encoded_frames_.fetch_add(1);
}

H264EncoderConfig RecordingSessionManager::BuildEncoderConfig(
    const DecodedImageFrame& frame) const
{
    H264EncoderConfig config;
    config.width = frame.width;
    config.height = frame.height;
    config.hor_stride = frame.hor_stride;
    config.ver_stride = frame.ver_stride;
    config.fps = 30;
    config.bitrate = 4000000;
    config.gop = 60;
    return config;
}

std::string RecordingSessionManager::MapWriterError(WriterResult result)
{
    switch (result)
    {
    case WriterResult::kOk:
        return std::string();
    case WriterResult::kOutputDirNotWritable:
        return "output_dir_not_writable";
    case WriterResult::kInvalidStreamId:
        return "invalid_stream_id";
    case WriterResult::kFileCreateFailed:
        return "recording_file_create_failed";
    case WriterResult::kFileNotOpen:
    case WriterResult::kRecordingIoError:
        return "recording_io_error";
    }
    return "recording_io_error";
}

} // namespace camera_subsystem::extensions::codec_server
