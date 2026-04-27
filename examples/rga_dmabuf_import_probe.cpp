/**
 * @file rga_dmabuf_import_probe.cpp
 * @brief Probe whether Rockchip RGA can import V4L2 exported DMA-BUF fds
 *
 * Usage:
 *   ./rga_dmabuf_import_probe [device_path] [width] [height] [fourcc]
 *
 * Example:
 *   ./rga_dmabuf_import_probe /dev/video22 800 600 NV12
 */

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

#include <im2d.h>

namespace
{

constexpr uint32_t kDefaultBufferCount = 4;
constexpr uint32_t kMaxPlanes = VIDEO_MAX_PLANES;

int Xioctl(int fd, unsigned long request, void* arg)
{
    int ret = 0;
    do
    {
        ret = ioctl(fd, request, arg);
    } while (ret < 0 && errno == EINTR);
    return ret;
}

uint32_t FourccFromString(const std::string& value)
{
    if (value.size() != 4)
    {
        return V4L2_PIX_FMT_NV12;
    }

    return v4l2_fourcc(value[0], value[1], value[2], value[3]);
}

std::string FourccToString(uint32_t fourcc)
{
    std::string value(4, ' ');
    value[0] = static_cast<char>(fourcc & 0xff);
    value[1] = static_cast<char>((fourcc >> 8) & 0xff);
    value[2] = static_cast<char>((fourcc >> 16) & 0xff);
    value[3] = static_cast<char>((fourcc >> 24) & 0xff);
    return value;
}

struct PlaneResource
{
    void* start = MAP_FAILED;
    uint32_t length = 0;
    int dmabuf_fd = -1;
    rga_buffer_handle_t rga_handle = 0;
};

struct BufferResource
{
    uint32_t index = 0;
    std::vector<PlaneResource> planes;
};

void Cleanup(int fd, std::vector<BufferResource>& buffers)
{
    for (auto& buffer : buffers)
    {
        for (auto& plane : buffer.planes)
        {
            if (plane.rga_handle != 0)
            {
                (void)releasebuffer_handle(plane.rga_handle);
                plane.rga_handle = 0;
            }
            if (plane.dmabuf_fd >= 0)
            {
                close(plane.dmabuf_fd);
                plane.dmabuf_fd = -1;
            }
            if (plane.start != MAP_FAILED)
            {
                munmap(plane.start, plane.length);
                plane.start = MAP_FAILED;
            }
        }
    }

    if (fd >= 0)
    {
        v4l2_requestbuffers req;
        std::memset(&req, 0, sizeof(req));
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        req.memory = V4L2_MEMORY_MMAP;
        req.count = 0;
        (void)Xioctl(fd, VIDIOC_REQBUFS, &req);
    }
}

} // namespace

int main(int argc, char* argv[])
{
    const std::string device_path = argc > 1 ? argv[1] : "/dev/video22";
    const uint32_t requested_width =
        argc > 2 ? static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10)) : 0;
    const uint32_t requested_height =
        argc > 3 ? static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 10)) : 0;
    const uint32_t requested_fourcc = argc > 4 ? FourccFromString(argv[4]) : V4L2_PIX_FMT_NV12;

    int fd = open(device_path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
    {
        std::fprintf(stderr, "open failed: device=%s errno=%d msg=%s\n",
                     device_path.c_str(), errno, std::strerror(errno));
        return 1;
    }

    v4l2_capability cap;
    std::memset(&cap, 0, sizeof(cap));
    if (Xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0)
    {
        std::fprintf(stderr, "VIDIOC_QUERYCAP failed: errno=%d msg=%s\n",
                     errno, std::strerror(errno));
        close(fd);
        return 1;
    }

    const uint32_t caps = cap.device_caps != 0 ? cap.device_caps : cap.capabilities;
    const bool supports_mplane = (caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0;
    const bool supports_streaming = (caps & V4L2_CAP_STREAMING) != 0;

    std::printf("rga_import_probe_device=%s\n", device_path.c_str());
    std::printf("driver=%s\n", cap.driver);
    std::printf("card=%s\n", cap.card);
    std::printf("bus=%s\n", cap.bus_info);
    std::printf("supports_mplane=%u\n", supports_mplane ? 1U : 0U);
    std::printf("supports_streaming=%u\n", supports_streaming ? 1U : 0U);

    if (!supports_mplane || !supports_streaming)
    {
        std::printf("rga_dmabuf_import_probe_result=FAIL\n");
        close(fd);
        return 1;
    }

    v4l2_format fmt;
    std::memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (Xioctl(fd, VIDIOC_G_FMT, &fmt) < 0)
    {
        std::fprintf(stderr, "VIDIOC_G_FMT failed: errno=%d msg=%s\n",
                     errno, std::strerror(errno));
        close(fd);
        return 1;
    }

    if (requested_width > 0)
    {
        fmt.fmt.pix_mp.width = requested_width;
    }
    if (requested_height > 0)
    {
        fmt.fmt.pix_mp.height = requested_height;
    }
    fmt.fmt.pix_mp.pixelformat = requested_fourcc;
    fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;

    if (Xioctl(fd, VIDIOC_S_FMT, &fmt) < 0)
    {
        std::fprintf(stderr, "VIDIOC_S_FMT failed: errno=%d msg=%s\n",
                     errno, std::strerror(errno));
        close(fd);
        return 1;
    }

    const uint32_t plane_count = fmt.fmt.pix_mp.num_planes;
    std::printf("width=%u\n", fmt.fmt.pix_mp.width);
    std::printf("height=%u\n", fmt.fmt.pix_mp.height);
    std::printf("fourcc=%s\n", FourccToString(fmt.fmt.pix_mp.pixelformat).c_str());
    std::printf("plane_count=%u\n", plane_count);
    for (uint32_t i = 0; i < plane_count && i < kMaxPlanes; ++i)
    {
        const auto& plane = fmt.fmt.pix_mp.plane_fmt[i];
        std::printf("fmt_plane[%u].bytesperline=%u\n", i, plane.bytesperline);
        std::printf("fmt_plane[%u].sizeimage=%u\n", i, plane.sizeimage);
    }

    v4l2_requestbuffers req;
    std::memset(&req, 0, sizeof(req));
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;
    req.count = kDefaultBufferCount;

    if (Xioctl(fd, VIDIOC_REQBUFS, &req) < 0)
    {
        std::fprintf(stderr, "VIDIOC_REQBUFS failed: errno=%d msg=%s\n",
                     errno, std::strerror(errno));
        close(fd);
        return 1;
    }

    std::printf("actual_buffer_count=%u\n", req.count);
    if (req.count == 0 || plane_count == 0 || plane_count > kMaxPlanes)
    {
        std::printf("rga_dmabuf_import_probe_result=FAIL\n");
        close(fd);
        return 1;
    }

    std::vector<BufferResource> buffers(req.count);
    uint32_t exported_fd_count = 0;
    uint32_t rga_import_ok = 0;
    uint32_t rga_import_fail = 0;
    bool ok = true;

    for (uint32_t i = 0; i < req.count; ++i)
    {
        buffers[i].index = i;
        buffers[i].planes.resize(plane_count);

        v4l2_buffer buf;
        v4l2_plane planes[kMaxPlanes];
        std::memset(&buf, 0, sizeof(buf));
        std::memset(planes, 0, sizeof(planes));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.length = plane_count;
        buf.m.planes = planes;

        if (Xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0)
        {
            std::fprintf(stderr, "VIDIOC_QUERYBUF failed: index=%u errno=%d msg=%s\n",
                         i, errno, std::strerror(errno));
            ok = false;
            break;
        }

        for (uint32_t p = 0; p < plane_count; ++p)
        {
            auto& plane_resource = buffers[i].planes[p];
            plane_resource.length = planes[p].length;
            plane_resource.start = mmap(nullptr,
                                        planes[p].length,
                                        PROT_READ | PROT_WRITE,
                                        MAP_SHARED,
                                        fd,
                                        planes[p].m.mem_offset);
            if (plane_resource.start == MAP_FAILED)
            {
                std::fprintf(stderr, "mmap failed: index=%u plane=%u errno=%d msg=%s\n",
                             i, p, errno, std::strerror(errno));
                ok = false;
                break;
            }

            v4l2_exportbuffer expbuf;
            std::memset(&expbuf, 0, sizeof(expbuf));
            expbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            expbuf.index = i;
            expbuf.plane = p;
            expbuf.flags = O_CLOEXEC;
            if (Xioctl(fd, VIDIOC_EXPBUF, &expbuf) < 0)
            {
                std::fprintf(stderr, "VIDIOC_EXPBUF failed: index=%u plane=%u errno=%d msg=%s\n",
                             i, p, errno, std::strerror(errno));
                ok = false;
                break;
            }

            plane_resource.dmabuf_fd = expbuf.fd;
            ++exported_fd_count;

            plane_resource.rga_handle =
                importbuffer_fd(plane_resource.dmabuf_fd, static_cast<int>(plane_resource.length));
            if (plane_resource.rga_handle == 0)
            {
                ++rga_import_fail;
                std::fprintf(stderr, "RGA importbuffer_fd failed: index=%u plane=%u fd=%d size=%u\n",
                             i, p, plane_resource.dmabuf_fd, plane_resource.length);
                ok = false;
                break;
            }

            ++rga_import_ok;
            std::printf("buffer[%u].plane[%u].length=%u\n", i, p, planes[p].length);
            std::printf("buffer[%u].plane[%u].bytesused=%u\n", i, p, planes[p].bytesused);
            std::printf("buffer[%u].plane[%u].mem_offset=%u\n", i, p, planes[p].m.mem_offset);
            std::printf("buffer[%u].plane[%u].dmabuf_fd=%d\n", i, p, expbuf.fd);
            std::printf("buffer[%u].plane[%u].rga_handle=%u\n", i, p, plane_resource.rga_handle);
        }

        if (!ok)
        {
            break;
        }
    }

    Cleanup(fd, buffers);
    close(fd);

    std::printf("exported_fd_count=%u\n", exported_fd_count);
    std::printf("rga_import_ok=%u\n", rga_import_ok);
    std::printf("rga_import_fail=%u\n", rga_import_fail);
    std::printf("rga_dmabuf_import_probe_result=%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
