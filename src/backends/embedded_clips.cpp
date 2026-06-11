// SPDX-License-Identifier: BSD-3-Clause
#include "backends/embedded_clips.hpp"

namespace imx95 {

#ifdef IMX95_HAVE_CLIPS
extern "C" {
extern const uint8_t imx95_clip_720p[], imx95_clip_720p_end[];
extern const uint8_t imx95_clip_1080p[], imx95_clip_1080p_end[];
extern const uint8_t imx95_clip_4k[], imx95_clip_4k_end[];
}
#endif

bool embedded_clip(VideoRes res, const uint8_t*& data, size_t& size) {
#ifdef IMX95_HAVE_CLIPS
    switch (res) {
        case VideoRes::R720p:
            data = imx95_clip_720p;
            size = static_cast<size_t>(imx95_clip_720p_end - imx95_clip_720p);
            return true;
        case VideoRes::R1080p:
            data = imx95_clip_1080p;
            size = static_cast<size_t>(imx95_clip_1080p_end - imx95_clip_1080p);
            return true;
        case VideoRes::R4k:
            data = imx95_clip_4k;
            size = static_cast<size_t>(imx95_clip_4k_end - imx95_clip_4k);
            return true;
        default:
            return false;
    }
#else
    (void)res;
    (void)data;
    (void)size;
    return false;
#endif
}

} // namespace imx95
