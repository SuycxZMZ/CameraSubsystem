#include "codec_server/mp4_file_writer.h"

#include "codec_server/h264_annexb_parser.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace camera_subsystem::extensions::codec_server {
namespace {

class ByteWriter
{
public:
    void U8(uint8_t value) { data_.push_back(value); }

    void U16(uint16_t value)
    {
        data_.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
        data_.push_back(static_cast<uint8_t>(value & 0xff));
    }

    void U24(uint32_t value)
    {
        data_.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
        data_.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
        data_.push_back(static_cast<uint8_t>(value & 0xff));
    }

    void U32(uint32_t value)
    {
        data_.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
        data_.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
        data_.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
        data_.push_back(static_cast<uint8_t>(value & 0xff));
    }

    void String(const char* value)
    {
        data_.insert(data_.end(), value, value + std::strlen(value));
    }

    void Bytes(const std::vector<uint8_t>& value)
    {
        data_.insert(data_.end(), value.begin(), value.end());
    }

    void Zeros(size_t count)
    {
        data_.insert(data_.end(), count, 0);
    }

    void Box(const char type[4], const std::vector<uint8_t>& payload)
    {
        U32(static_cast<uint32_t>(payload.size() + 8));
        String(type);
        Bytes(payload);
    }

    std::vector<uint8_t> Take() { return std::move(data_); }
    const std::vector<uint8_t>& Data() const { return data_; }

private:
    std::vector<uint8_t> data_;
};

std::vector<uint8_t> MakeBox(const char type[4], const std::vector<uint8_t>& payload)
{
    ByteWriter out;
    out.Box(type, payload);
    return out.Take();
}

uint32_t MovieDuration(uint32_t sample_count, uint32_t fps)
{
    const uint32_t safe_fps = fps == 0 ? 30 : fps;
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(sample_count) * 1000ULL + safe_fps - 1ULL) / safe_fps);
}

std::vector<uint8_t> MakeFtyp()
{
    ByteWriter payload;
    payload.String("isom");
    payload.U32(0x00000200);
    payload.String("isom");
    payload.String("iso2");
    payload.String("avc1");
    payload.String("mp41");
    return MakeBox("ftyp", payload.Take());
}

std::vector<uint8_t> MakeMvhd(uint32_t duration)
{
    ByteWriter p;
    p.U32(0);
    p.U32(0);
    p.U32(0);
    p.U32(1000);
    p.U32(duration);
    p.U32(0x00010000);
    p.U16(0x0100);
    p.U16(0);
    p.Zeros(8);
    p.U32(0x00010000);
    p.U32(0);
    p.U32(0);
    p.U32(0);
    p.U32(0x00010000);
    p.U32(0);
    p.U32(0);
    p.U32(0);
    p.U32(0x40000000);
    p.Zeros(24);
    p.U32(2);
    return MakeBox("mvhd", p.Take());
}

std::vector<uint8_t> MakeTkhd(uint32_t duration, uint32_t width, uint32_t height)
{
    ByteWriter p;
    p.U8(0);
    p.U24(0x000007);
    p.U32(0);
    p.U32(0);
    p.U32(1);
    p.U32(0);
    p.U32(duration);
    p.Zeros(8);
    p.U16(0);
    p.U16(0);
    p.U16(0);
    p.U16(0);
    p.U32(0x00010000);
    p.U32(0);
    p.U32(0);
    p.U32(0);
    p.U32(0x00010000);
    p.U32(0);
    p.U32(0);
    p.U32(0);
    p.U32(0x40000000);
    p.U32(width << 16U);
    p.U32(height << 16U);
    return MakeBox("tkhd", p.Take());
}

std::vector<uint8_t> MakeMdhd(uint32_t timescale, uint32_t duration)
{
    ByteWriter p;
    p.U32(0);
    p.U32(0);
    p.U32(0);
    p.U32(timescale);
    p.U32(duration);
    p.U16(0x55c4);
    p.U16(0);
    return MakeBox("mdhd", p.Take());
}

std::vector<uint8_t> MakeHdlr()
{
    ByteWriter p;
    p.U32(0);
    p.U32(0);
    p.String("vide");
    p.Zeros(12);
    p.String("VideoHandler");
    p.U8(0);
    return MakeBox("hdlr", p.Take());
}

std::vector<uint8_t> MakeVmhd()
{
    ByteWriter p;
    p.U8(0);
    p.U24(1);
    p.U16(0);
    p.U16(0);
    p.U16(0);
    p.U16(0);
    return MakeBox("vmhd", p.Take());
}

std::vector<uint8_t> MakeDinf()
{
    ByteWriter url;
    url.U8(0);
    url.U24(1);

    ByteWriter dref;
    dref.U32(0);
    dref.U32(1);
    dref.Bytes(MakeBox("url ", url.Take()));
    return MakeBox("dinf", MakeBox("dref", dref.Take()));
}

std::vector<uint8_t> MakeAvcC(const std::vector<uint8_t>& sps,
                              const std::vector<uint8_t>& pps)
{
    ByteWriter p;
    p.U8(1);
    p.U8(sps.size() > 1 ? sps[1] : 0x64);
    p.U8(sps.size() > 2 ? sps[2] : 0x00);
    p.U8(sps.size() > 3 ? sps[3] : 0x28);
    p.U8(0xff);
    p.U8(0xe1);
    p.U16(static_cast<uint16_t>(sps.size()));
    p.Bytes(sps);
    p.U8(1);
    p.U16(static_cast<uint16_t>(pps.size()));
    p.Bytes(pps);
    return MakeBox("avcC", p.Take());
}

std::vector<uint8_t> MakeStsd(uint32_t width,
                              uint32_t height,
                              const std::vector<uint8_t>& sps,
                              const std::vector<uint8_t>& pps)
{
    ByteWriter avc1;
    avc1.Zeros(6);
    avc1.U16(1);
    avc1.Zeros(16);
    avc1.U16(static_cast<uint16_t>(width));
    avc1.U16(static_cast<uint16_t>(height));
    avc1.U32(0x00480000);
    avc1.U32(0x00480000);
    avc1.U32(0);
    avc1.U16(1);
    avc1.Zeros(32);
    avc1.U16(0x0018);
    avc1.U16(0xffff);
    avc1.Bytes(MakeAvcC(sps, pps));

    ByteWriter p;
    p.U32(0);
    p.U32(1);
    p.Bytes(MakeBox("avc1", avc1.Take()));
    return MakeBox("stsd", p.Take());
}

std::vector<uint8_t> MakeStts(uint32_t sample_count)
{
    ByteWriter p;
    p.U32(0);
    p.U32(1);
    p.U32(sample_count);
    p.U32(1);
    return MakeBox("stts", p.Take());
}

std::vector<uint8_t> MakeStss(const std::vector<Mp4FileWriter::Sample>& samples)
{
    ByteWriter p;
    p.U32(0);
    uint32_t sync_count = 0;
    for (const auto& sample : samples)
    {
        if (sample.is_sync)
        {
            ++sync_count;
        }
    }
    p.U32(sync_count);
    for (size_t i = 0; i < samples.size(); ++i)
    {
        if (samples[i].is_sync)
        {
            p.U32(static_cast<uint32_t>(i + 1));
        }
    }
    return MakeBox("stss", p.Take());
}

std::vector<uint8_t> MakeStsc()
{
    ByteWriter p;
    p.U32(0);
    p.U32(1);
    p.U32(1);
    p.U32(1);
    p.U32(1);
    return MakeBox("stsc", p.Take());
}

std::vector<uint8_t> MakeStsz(const std::vector<Mp4FileWriter::Sample>& samples)
{
    ByteWriter p;
    p.U32(0);
    p.U32(0);
    p.U32(static_cast<uint32_t>(samples.size()));
    for (const auto& sample : samples)
    {
        p.U32(static_cast<uint32_t>(sample.payload.size()));
    }
    return MakeBox("stsz", p.Take());
}

std::vector<uint8_t> MakeStco(const std::vector<uint32_t>& offsets)
{
    ByteWriter p;
    p.U32(0);
    p.U32(static_cast<uint32_t>(offsets.size()));
    for (uint32_t offset : offsets)
    {
        p.U32(offset);
    }
    return MakeBox("stco", p.Take());
}

std::vector<uint8_t> MakeMoov(const Mp4WriterConfig& config,
                              const std::vector<uint8_t>& sps,
                              const std::vector<uint8_t>& pps,
                              const std::vector<Mp4FileWriter::Sample>& samples,
                              const std::vector<uint32_t>& sample_offsets)
{
    const uint32_t sample_count = static_cast<uint32_t>(samples.size());
    const uint32_t fps = config.fps == 0 ? 30 : config.fps;
    const uint32_t movie_duration = MovieDuration(sample_count, fps);

    ByteWriter stbl;
    stbl.Bytes(MakeStsd(config.width, config.height, sps, pps));
    stbl.Bytes(MakeStts(sample_count));
    stbl.Bytes(MakeStss(samples));
    stbl.Bytes(MakeStsc());
    stbl.Bytes(MakeStsz(samples));
    stbl.Bytes(MakeStco(sample_offsets));

    ByteWriter minf;
    minf.Bytes(MakeVmhd());
    minf.Bytes(MakeDinf());
    minf.Bytes(MakeBox("stbl", stbl.Take()));

    ByteWriter mdia;
    mdia.Bytes(MakeMdhd(fps, sample_count));
    mdia.Bytes(MakeHdlr());
    mdia.Bytes(MakeBox("minf", minf.Take()));

    ByteWriter trak;
    trak.Bytes(MakeTkhd(movie_duration, config.width, config.height));
    trak.Bytes(MakeBox("mdia", mdia.Take()));

    ByteWriter moov;
    moov.Bytes(MakeMvhd(movie_duration));
    moov.Bytes(MakeBox("trak", trak.Take()));
    return MakeBox("moov", moov.Take());
}

Mp4WriterResult MapWriterResult(WriterResult result)
{
    switch (result)
    {
    case WriterResult::kOk:
        return Mp4WriterResult::kOk;
    case WriterResult::kOutputDirNotWritable:
        return Mp4WriterResult::kOutputDirNotWritable;
    case WriterResult::kFileCreateFailed:
        return Mp4WriterResult::kFileCreateFailed;
    case WriterResult::kInvalidStreamId:
        return Mp4WriterResult::kInvalidStreamId;
    case WriterResult::kFileNotOpen:
        return Mp4WriterResult::kFileNotOpen;
    case WriterResult::kRecordingIoError:
        return Mp4WriterResult::kRecordingIoError;
    }
    return Mp4WriterResult::kRecordingIoError;
}

} // namespace

Mp4FileWriter::~Mp4FileWriter()
{
    Close();
}

Mp4WriterResult Mp4FileWriter::Open(const std::string& stream_id,
                                    const std::string& output_dir,
                                    const Mp4WriterConfig& config)
{
    Close();
    if (config.width == 0 || config.height == 0)
    {
        return Mp4WriterResult::kInvalidPacket;
    }

    RecordingFileWriterOptions options;
    options.file_extension = ".mp4";
    const WriterResult open_result = path_writer_.Open(stream_id, output_dir, options);
    if (open_result != WriterResult::kOk)
    {
        return MapWriterResult(open_result);
    }
    file_path_ = path_writer_.GetFilePath();
    (void)path_writer_.Close();

    file_handle_ = std::fopen(file_path_.c_str(), "wb");
    if (!file_handle_)
    {
        return Mp4WriterResult::kFileCreateFailed;
    }

    config_ = config;
    if (config_.fps == 0)
    {
        config_.fps = 30;
    }
    sps_.clear();
    pps_.clear();
    samples_.clear();
    stats_ = WriterStats{};
    is_open_ = true;
    return Mp4WriterResult::kOk;
}

Mp4WriterResult Mp4FileWriter::WriteAnnexBPacket(const uint8_t* data, size_t size)
{
    if (!is_open_)
    {
        return Mp4WriterResult::kFileNotOpen;
    }
    if (!data || size == 0)
    {
        return Mp4WriterResult::kInvalidPacket;
    }

    Sample sample;
    for (const H264NalUnit& unit : ParseAnnexBNalUnits(data, size))
    {
        if (unit.nal_type == static_cast<uint8_t>(H264NalType::kSps))
        {
            sps_.assign(unit.data, unit.data + unit.size);
            continue;
        }
        if (unit.nal_type == static_cast<uint8_t>(H264NalType::kPps))
        {
            pps_.assign(unit.data, unit.data + unit.size);
            continue;
        }
        if (unit.nal_type == static_cast<uint8_t>(H264NalType::kAud))
        {
            continue;
        }

        sample.payload.push_back(static_cast<uint8_t>((unit.size >> 24) & 0xff));
        sample.payload.push_back(static_cast<uint8_t>((unit.size >> 16) & 0xff));
        sample.payload.push_back(static_cast<uint8_t>((unit.size >> 8) & 0xff));
        sample.payload.push_back(static_cast<uint8_t>(unit.size & 0xff));
        sample.payload.insert(sample.payload.end(), unit.data, unit.data + unit.size);
        if (unit.nal_type == static_cast<uint8_t>(H264NalType::kSliceIdr))
        {
            sample.is_sync = true;
        }
    }

    if (!sample.payload.empty())
    {
        stats_.bytes_written += sample.payload.size();
        ++stats_.packets_written;
        samples_.push_back(std::move(sample));
    }
    return Mp4WriterResult::kOk;
}

Mp4WriterResult Mp4FileWriter::Close()
{
    if (!is_open_)
    {
        return Mp4WriterResult::kOk;
    }
    const Mp4WriterResult result = WriteFile();
    CloseHandle();
    is_open_ = false;
    return result;
}

WriterStats Mp4FileWriter::GetStats() const
{
    return stats_;
}

std::string Mp4FileWriter::GetFilePath() const
{
    return file_path_;
}

Mp4WriterResult Mp4FileWriter::WriteFile()
{
    if (sps_.empty() || pps_.empty() || samples_.empty())
    {
        ++stats_.write_failures;
        return Mp4WriterResult::kMissingParameterSets;
    }

    const std::vector<uint8_t> ftyp = MakeFtyp();
    const uint32_t mdat_payload_size = static_cast<uint32_t>(stats_.bytes_written);
    const uint32_t mdat_data_offset = static_cast<uint32_t>(ftyp.size() + 8);
    std::vector<uint32_t> sample_offsets;
    sample_offsets.reserve(samples_.size());
    uint32_t cursor = mdat_data_offset;
    for (const Sample& sample : samples_)
    {
        sample_offsets.push_back(cursor);
        cursor += static_cast<uint32_t>(sample.payload.size());
    }

    const std::vector<uint8_t> moov =
        MakeMoov(config_, sps_, pps_, samples_, sample_offsets);

    if (WriteBytes(ftyp) != Mp4WriterResult::kOk)
    {
        return Mp4WriterResult::kRecordingIoError;
    }

    ByteWriter mdat_header;
    mdat_header.U32(mdat_payload_size + 8);
    mdat_header.String("mdat");
    if (WriteBytes(mdat_header.Take()) != Mp4WriterResult::kOk)
    {
        return Mp4WriterResult::kRecordingIoError;
    }
    for (const Sample& sample : samples_)
    {
        if (WriteBytes(sample.payload) != Mp4WriterResult::kOk)
        {
            return Mp4WriterResult::kRecordingIoError;
        }
    }
    if (WriteBytes(moov) != Mp4WriterResult::kOk)
    {
        return Mp4WriterResult::kRecordingIoError;
    }
    if (std::fflush(file_handle_) != 0)
    {
        ++stats_.write_failures;
        return Mp4WriterResult::kRecordingIoError;
    }
    return Mp4WriterResult::kOk;
}

Mp4WriterResult Mp4FileWriter::WriteBytes(const std::vector<uint8_t>& bytes)
{
    if (bytes.empty())
    {
        return Mp4WriterResult::kOk;
    }
    const size_t written = std::fwrite(bytes.data(), 1, bytes.size(), file_handle_);
    if (written != bytes.size())
    {
        ++stats_.write_failures;
        return Mp4WriterResult::kRecordingIoError;
    }
    return Mp4WriterResult::kOk;
}

void Mp4FileWriter::CloseHandle()
{
    if (file_handle_)
    {
        std::fclose(file_handle_);
        file_handle_ = nullptr;
    }
}

const char* ToErrorString(Mp4WriterResult result)
{
    switch (result)
    {
    case Mp4WriterResult::kOk:
        return "";
    case Mp4WriterResult::kOutputDirNotWritable:
        return "output_dir_not_writable";
    case Mp4WriterResult::kFileCreateFailed:
        return "file_create_failed";
    case Mp4WriterResult::kInvalidStreamId:
        return "invalid_stream_id";
    case Mp4WriterResult::kFileNotOpen:
        return "file_not_open";
    case Mp4WriterResult::kMissingParameterSets:
        return "missing_h264_parameter_sets";
    case Mp4WriterResult::kInvalidPacket:
        return "invalid_h264_packet";
    case Mp4WriterResult::kRecordingIoError:
        return "recording_io_error";
    }
    return "recording_io_error";
}

} // namespace camera_subsystem::extensions::codec_server
