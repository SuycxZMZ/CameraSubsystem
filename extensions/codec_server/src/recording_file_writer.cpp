#include "codec_server/recording_file_writer.h"

#include <chrono>
#include <climits>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>

namespace camera_subsystem::extensions::codec_server {

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

WriterResult RecordingFileWriter::ValidateStreamId(
    const std::string& stream_id) const
{
    if (stream_id.empty())
    {
        return WriterResult::kInvalidStreamId;
    }
    if (stream_id.size() > 128)
    {
        return WriterResult::kInvalidStreamId;
    }
    for (char c : stream_id)
    {
        if (c == '/' || c == '\\' || c == '\0')
        {
            return WriterResult::kInvalidStreamId;
        }
    }
    return WriterResult::kOk;
}

std::string RecordingFileWriter::GenerateFileName(
    const std::string& stream_id) const
{
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf
    {};
    localtime_r(&time_t_now, &tm_buf);

    char timestamp[16]
    {};
    std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &tm_buf);

    std::string base_name = stream_id + "_" + timestamp + ".h264";
    return base_name;
}

WriterResult RecordingFileWriter::EnsureOutputDir(
    const std::string& output_dir)
{
    namespace fs = std::filesystem;

    std::error_code ec;
    if (fs::exists(output_dir, ec))
    {
        // Directory exists — verify it is writable by creating a temp file.
        std::string test_file = output_dir + "/.codec_server_write_test_tmp";
        std::FILE* fp = std::fopen(test_file.c_str(), "wb");
        if (fp == nullptr)
        {
            return WriterResult::kOutputDirNotWritable;
        }
        std::fclose(fp);
        std::remove(test_file.c_str());
        return WriterResult::kOk;
    }

    // Directory does not exist — try to create it recursively.
    fs::create_directories(output_dir, ec);
    if (ec)
    {
        return WriterResult::kOutputDirNotWritable;
    }
    return WriterResult::kOk;
}

WriterResult RecordingFileWriter::DoFlush()
{
    if (std::fflush(file_handle_) != 0)
    {
        ++stats_.write_failures;
        return WriterResult::kRecordingIoError;
    }
    if (fsync(fileno(file_handle_)) != 0)
    {
        ++stats_.write_failures;
        return WriterResult::kRecordingIoError;
    }
    return WriterResult::kOk;
}

void RecordingFileWriter::CloseHandle()
{
    if (file_handle_ != nullptr)
    {
        std::fclose(file_handle_);
        file_handle_ = nullptr;
    }
    is_open_ = false;
}

// ---------------------------------------------------------------------------
// Public lifecycle methods
// ---------------------------------------------------------------------------

WriterResult RecordingFileWriter::Open(const std::string& stream_id,
                                       const std::string& output_dir)
{
    // Implicit close if a file is already open.
    if (is_open_)
    {
        Close();
    }

    // Validate stream_id.
    WriterResult vr = ValidateStreamId(stream_id);
    if (vr != WriterResult::kOk)
    {
        return vr;
    }

    // Ensure output directory exists and is writable.
    WriterResult dr = EnsureOutputDir(output_dir);
    if (dr != WriterResult::kOk)
    {
        return dr;
    }

    // Generate filename and build full path.
    std::string filename = GenerateFileName(stream_id);
    std::string full_path = output_dir + "/" + filename;

    // Handle filename conflicts: if file exists, append _N suffix.
    namespace fs = std::filesystem;
    if (fs::exists(full_path))
    {
        std::string base_no_ext = filename.substr(
            0, filename.size() - 5); // strip ".h264"
        for (int n = 2; ; ++n)
        {
            std::string candidate = base_no_ext + "_" +
                std::to_string(n) + ".h264";
            full_path = output_dir + "/" + candidate;
            if (!fs::exists(full_path))
            {
                break;
            }
        }
    }

    // Check path length.
    if (full_path.size() >= PATH_MAX)
    {
        return WriterResult::kFileCreateFailed;
    }

    // Open the file.
    file_handle_ = std::fopen(full_path.c_str(), "wb");
    if (file_handle_ == nullptr)
    {
        return WriterResult::kFileCreateFailed;
    }

    // Set file permissions to 0644.
    chmod(full_path.c_str(), 0644);

    file_path_ = full_path;
    is_open_ = true;
    ResetStats();
    return WriterResult::kOk;
}

WriterResult RecordingFileWriter::Write(const uint8_t* data, size_t size)
{
    if (!is_open_)
    {
        return WriterResult::kFileNotOpen;
    }
    if (size == 0)
    {
        return WriterResult::kOk;
    }
    size_t written = std::fwrite(data, 1, size, file_handle_);
    if (written != size)
    {
        ++stats_.write_failures;
        return WriterResult::kRecordingIoError;
    }
    stats_.bytes_written += size;
    ++stats_.packets_written;
    return WriterResult::kOk;
}

WriterResult RecordingFileWriter::Flush()
{
    if (!is_open_)
    {
        return WriterResult::kFileNotOpen;
    }
    return DoFlush();
}

WriterResult RecordingFileWriter::Close()
{
    if (!is_open_)
    {
        return WriterResult::kOk;
    }
    WriterResult flush_result = DoFlush();
    CloseHandle();
    return flush_result;
}

// ---------------------------------------------------------------------------
// Destructor, move, query
// ---------------------------------------------------------------------------

RecordingFileWriter::~RecordingFileWriter()
{
    Close();
}

RecordingFileWriter::RecordingFileWriter(RecordingFileWriter&& other) noexcept
    : file_handle_(other.file_handle_),
      file_path_(std::move(other.file_path_)),
      is_open_(other.is_open_),
      stats_(other.stats_)
{
    other.file_handle_ = nullptr;
    other.is_open_ = false;
    other.file_path_.clear();
}

RecordingFileWriter& RecordingFileWriter::operator=(
    RecordingFileWriter&& other) noexcept
{
    if (this != &other)
    {
        Close();
        file_handle_ = other.file_handle_;
        file_path_ = std::move(other.file_path_);
        is_open_ = other.is_open_;
        stats_ = other.stats_;
        other.file_handle_ = nullptr;
        other.is_open_ = false;
        other.file_path_.clear();
    }
    return *this;
}

WriterStats RecordingFileWriter::GetStats() const
{
    return stats_;
}

std::string RecordingFileWriter::GetFilePath() const
{
    return file_path_;
}

void RecordingFileWriter::ResetStats()
{
    stats_ = WriterStats{};
}

} // namespace camera_subsystem::extensions::codec_server
