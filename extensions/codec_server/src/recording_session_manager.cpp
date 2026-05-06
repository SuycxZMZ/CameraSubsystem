#include "codec_server/recording_session_manager.h"

#include <utility>

namespace camera_subsystem::extensions::codec_server {
namespace {

bool IsValidStreamId(const std::string& stream_id)
{
    if (stream_id.empty() || stream_id.size() > 128)
    {
        return false;
    }
    for (char c : stream_id)
    {
        if (c == '/' || c == '\\' || c == '\0')
        {
            return false;
        }
    }
    return true;
}

} // namespace

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

    if (!IsValidStreamId(request.stream_id))
    {
        state_ = "error";
        stream_id_ = request.stream_id;
        last_error_ = "invalid_stream_id";
        return BuildStatusLocked(request, last_error_);
    }

    state_ = "starting";
    stream_id_ = request.stream_id;
    active_profile_ = {};
    active_profile_.fps = request.profile.fps > 0 ? request.profile.fps : config_.fps;
    active_profile_.bitrate =
        request.profile.bitrate > 0 ? request.profile.bitrate : config_.bitrate;
    active_profile_.gop = request.profile.gop > 0 ? request.profile.gop : config_.gop;
    started_at_ = std::chrono::steady_clock::now();
    last_duration_ms_ = 0;
    encoded_frames_.store(0);
    dropped_frames_.store(0);
    input_frames_ = 0;
    decoded_frames_.store(0);
    decode_failures_.store(0);
    last_error_.clear();
    active_container_ = request.container == "mp4" ? ActiveContainer::kMp4 : ActiveContainer::kRawH264;

    output_dir_ =
        request.output_dir.empty() ? config_.default_output_dir : request.output_dir;
    if (active_container_ == ActiveContainer::kRawH264)
    {
        RecordingFileWriterOptions writer_options;
        writer_options.file_extension = ".h264";
        const WriterResult result = writer_.Open(request.stream_id, output_dir_, writer_options);
        if (result != WriterResult::kOk)
        {
            state_ = "error";
            last_error_ = MapWriterError(result);
            file_path_.clear();
            return BuildStatusLocked(request, last_error_);
        }
        file_path_ = writer_.GetFilePath();
    }
    else
    {
        file_path_.clear();
    }
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
            (void)mp4_writer_.Close();
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
    last_duration_ms_ = GetDurationMsLocked();
    subscriber_.Stop();
    {
        std::lock_guard<std::mutex> pipeline_lock(pipeline_mutex_);
        h264_encoder_.Close();
    }
    if (active_container_ == ActiveContainer::kMp4)
    {
        const Mp4WriterResult close_result = mp4_writer_.Close();
        if (close_result != Mp4WriterResult::kOk)
        {
            state_ = "error";
            last_error_ = MapMp4WriterError(close_result);
            return BuildStatusLocked(request, last_error_);
        }
    }
    else
    {
        const WriterResult close_result = writer_.Close();
        if (close_result != WriterResult::kOk)
        {
            state_ = "error";
            last_error_ = MapWriterError(close_result);
            return BuildStatusLocked(request, last_error_);
        }
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
    const WriterStats stats = GetActiveWriterStatsLocked();
    CodecControlStatus status;
    status.request_id = request.request_id;
    status.stream_id = stream_id_.empty() ? request.stream_id : stream_id_;
    status.recording = state_ == "recording";
    status.state = state_;
    status.codec = request.codec.empty() ? "h264" : request.codec;
    status.container = GetActiveContainerName();
    status.file = file_path_;
    status.encoded_frames = encoded_frames_.load();
    status.decoded_frames = decoded_frames_.load();
    status.dropped_frames = dropped_frames_.load();
    status.input_frames = config_.enable_camera_subscriber
                              ? subscriber_stats.input_frames
                              : input_frames_;
    status.duration_ms = GetDurationMsLocked();
    status.bytes_written = stats.bytes_written;
    status.packets_written = stats.packets_written;
    status.decode_failures = decode_failures_.load();
    status.write_failures = stats.write_failures + subscriber_stats.read_failures;
    status.error = error;
    status.profile = active_profile_;
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
        if (active_container_ == ActiveContainer::kMp4 &&
            !EnsureMp4WriterOpenLocked(decoded))
        {
            dropped_frames_.fetch_add(1);
            return;
        }
        if (!WritePacketLocked(packet))
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
    config.fps = active_profile_.fps > 0 ? active_profile_.fps : config_.fps;
    config.bitrate = active_profile_.bitrate > 0 ? active_profile_.bitrate : config_.bitrate;
    config.gop = active_profile_.gop > 0 ? active_profile_.gop : config_.gop;
    return config;
}

WriterStats RecordingSessionManager::GetActiveWriterStatsLocked() const
{
    if (active_container_ == ActiveContainer::kMp4)
    {
        return mp4_writer_.GetStats();
    }
    return writer_.GetStats();
}

std::string RecordingSessionManager::GetActiveContainerName() const
{
    return active_container_ == ActiveContainer::kMp4 ? "mp4" : "raw_h264";
}

bool RecordingSessionManager::EnsureMp4WriterOpenLocked(const DecodedImageFrame& frame)
{
    if (!file_path_.empty())
    {
        return true;
    }

    Mp4WriterConfig mp4_config;
    mp4_config.width = frame.width;
    mp4_config.height = frame.height;
    mp4_config.fps = active_profile_.fps > 0 ? active_profile_.fps : config_.fps;
    const Mp4WriterResult result = mp4_writer_.Open(stream_id_, output_dir_, mp4_config);
    if (result != Mp4WriterResult::kOk)
    {
        last_error_ = MapMp4WriterError(result);
        return false;
    }
    file_path_ = mp4_writer_.GetFilePath();
    return true;
}

bool RecordingSessionManager::WritePacketLocked(const EncodedPacket& packet)
{
    if (active_container_ == ActiveContainer::kMp4)
    {
        const Mp4WriterResult write_result =
            mp4_writer_.WriteAnnexBPacket(packet.payload.data(), packet.payload.size());
        if (write_result != Mp4WriterResult::kOk)
        {
            last_error_ = MapMp4WriterError(write_result);
            return false;
        }
        return true;
    }

    const WriterResult write_result =
        writer_.Write(packet.payload.data(), packet.payload.size());
    if (write_result != WriterResult::kOk)
    {
        last_error_ = MapWriterError(write_result);
        return false;
    }
    return true;
}

uint64_t RecordingSessionManager::GetDurationMsLocked() const
{
    if (started_at_ == std::chrono::steady_clock::time_point{})
    {
        return last_duration_ms_;
    }
    if (state_ == "recording" || state_ == "starting" || state_ == "stopping")
    {
        const auto elapsed = std::chrono::steady_clock::now() - started_at_;
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
    }
    return last_duration_ms_;
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

std::string RecordingSessionManager::MapMp4WriterError(Mp4WriterResult result)
{
    switch (result)
    {
    case Mp4WriterResult::kOk:
        return std::string();
    case Mp4WriterResult::kOutputDirNotWritable:
        return "output_dir_not_writable";
    case Mp4WriterResult::kInvalidStreamId:
        return "invalid_stream_id";
    case Mp4WriterResult::kFileCreateFailed:
        return "recording_file_create_failed";
    case Mp4WriterResult::kMissingParameterSets:
        return "missing_h264_parameter_sets";
    case Mp4WriterResult::kInvalidPacket:
        return "invalid_h264_packet";
    case Mp4WriterResult::kFileNotOpen:
    case Mp4WriterResult::kRecordingIoError:
        return "recording_io_error";
    }
    return "recording_io_error";
}

} // namespace camera_subsystem::extensions::codec_server
