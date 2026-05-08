#include "camera_subsystem/core/camera_stream_identity.h"

#include <algorithm>
#include <cstring>

namespace camera_subsystem
{
namespace core
{

namespace
{

void CopyStreamId(std::array<char, kCameraStreamIdMaxLength>* target,
                  const std::string& stream_id)
{
    target->fill('\0');
    const std::string& effective_id = stream_id.empty() ? kDefaultCameraStreamId : stream_id;
    const size_t copy_size = std::min(effective_id.size(), kCameraStreamIdMaxLength - 1);
    std::memcpy(target->data(), effective_id.data(), copy_size);
}

} // namespace

CameraStreamIdentity MakeDefaultCameraStreamIdentity(uint32_t camera_id)
{
    return MakeCameraStreamIdentity(kDefaultCameraStreamId, camera_id);
}

CameraStreamIdentity MakeCameraStreamIdentity(const std::string& stream_id,
                                              uint32_t camera_id,
                                              uint32_t bus_type,
                                              uint32_t bus_index)
{
    CameraStreamIdentity identity;
    identity.camera_id = camera_id;
    identity.bus_type = bus_type;
    identity.bus_index = bus_index;
    CopyStreamId(&identity.stream_id, stream_id);
    return identity;
}

std::string GetCameraStreamId(const CameraStreamIdentity& identity)
{
    return std::string(identity.stream_id.data());
}

void SetCameraStreamId(CameraStreamIdentity* identity, const std::string& stream_id)
{
    if (identity == nullptr)
    {
        return;
    }
    CopyStreamId(&identity->stream_id, stream_id);
}

bool IsCameraStreamIdentityValid(const CameraStreamIdentity& identity)
{
    return identity.stream_id[0] != '\0';
}

} // namespace core
} // namespace camera_subsystem
