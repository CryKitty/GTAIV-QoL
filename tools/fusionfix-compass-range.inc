// SPDX-License-Identifier: GPL-3.0-only
// Read the current radius loaded before the radar target/current comparison.
// The add/subtract animation-step operands are deliberately not used.
// No additional game hooks or writes. Zero signals an unavailable range.
static float* QolCompassRadarRangeAddress = nullptr;
extern "C" __declspec(dllexport) float QoLCompassRadarRange()
{
    float range = 0.0f;
    SIZE_T bytes = 0;
    if (!QolCompassRadarRangeAddress ||
        !ReadProcessMemory(GetCurrentProcess(), QolCompassRadarRangeAddress, &range, sizeof(range), &bytes) || bytes != sizeof(range))
        return 0.0f;
    return range >= 10.0f && range <= 5000.0f ? range : 0.0f;
}
