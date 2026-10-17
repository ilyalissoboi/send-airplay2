// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_HEVC_VIDEO_RANGE_H
#define SEND_AIRPLAY2_HEVC_VIDEO_RANGE_H
#include "mp4_demux.h"

namespace send_airplay2::detail {
enum class VideoRange { unspecified, sdr, pq, hlg };

/** Read base-layer SPS VUI transfer characteristics from an hvcC payload.
 * Bounded metadata parsing only: no slices, decoding, source I/O or mutations.
 * Missing SPS/VUI/colour descriptions return unspecified. Conflicting explicit
 * ranges are unsupported; truncated syntax and out-of-bound fields are malformed.
 * This reads only through the VUI colour description, not a complete SPS validator.
 */
[[nodiscard]] VideoRange hevc_video_range(const Bytes& configuration);
} // namespace send_airplay2::detail
#endif
