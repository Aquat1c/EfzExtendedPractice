// Units: DirectSound hundredths of a decibel. Valid native level: -10000..0.
// Table generated offline; no runtime floating point.
#pragma once
static_assert(sizeof(int) == 4, "32-bit int required");
namespace efz_audio_gain_detail {
inline constexpr int gain[101] = {
    -10000, -4000, -3398, -3046, -2796, -2602, -2444, -2310, -2194, -2092,
    -2000, -1917, -1842, -1772, -1708, -1648, -1592, -1539, -1489, -1442,
    -1398, -1356, -1315, -1277, -1240, -1204, -1170, -1137, -1106, -1075,
    -1046, -1017, -990, -963, -937, -912, -887, -864, -840, -818,
    -796, -774, -754, -733, -713, -694, -674, -656, -638, -620,
    -602, -585, -568, -551, -535, -519, -504, -488, -473, -458,
    -444, -429, -415, -401, -388, -374, -361, -348, -335, -322,
    -310, -297, -285, -273, -262, -250, -238, -227, -216, -205,
    -194, -183, -172, -162, -151, -141, -131, -121, -111, -101,
    -92, -82, -72, -63, -54, -45, -35, -26, -18, -9,
    0
};
}
inline int EfzAudioGainForPercent(int percent) noexcept {
    if (percent <= 0) return -10000;
    if (percent >= 100) return 0;
    return efz_audio_gain_detail::gain[percent];
}
inline int EfzAudioApplyGain(int nativeLevel, int gain) noexcept {
    if (nativeLevel < -10000) nativeLevel = -10000;
    if (nativeLevel > 0) nativeLevel = 0;
    if (gain <= -10000) return -10000;
    if (gain > 0) gain = 0;
    const int value = nativeLevel + gain;
    return value < -10000 ? -10000 : value;
}
