#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

struct WavInfo {
  uint32_t offset = 0;
  uint32_t bytes = 0;
  uint16_t channels = 0;
};

inline uint16_t le16(const uint8_t *p) { return p[0] | uint16_t(p[1]) << 8; }
inline uint32_t le32(const uint8_t *p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 |
         uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

// Reader supplies readAt(offset, destination, length). Also used in host tests.
template <typename Reader>
bool parseWav(uint32_t size, Reader readAt, WavInfo &info) {
  uint8_t h[16];
  if (size < 12 || !readAt(0, h, 12) || memcmp(h, "RIFF", 4) ||
      memcmp(h + 8, "WAVE", 4)) return false;
  const uint64_t end = uint64_t(le32(h + 4)) + 8;
  if (end > size || end < 12) return false;
  bool fmt = false, data = false;
  WavInfo result;
  for (uint64_t pos = 12; pos + 8 <= end;) {
    if (!readAt(uint32_t(pos), h, 8)) return false;
    const uint32_t n = le32(h + 4);
    const uint64_t start = pos + 8;
    if (start + n > end) return false;
    if (!memcmp(h, "fmt ", 4)) {
      if (fmt || n < 16 || !readAt(uint32_t(start), h, 16)) return false;
      const uint16_t channels = le16(h + 2);
      if (le16(h) != 1 || (channels != 1 && channels != 2) ||
          le32(h + 4) != 44100 || le16(h + 14) != 16 ||
          le16(h + 12) != channels * 2 ||
          le32(h + 8) != 44100U * channels * 2) return false;
      result.channels = channels;
      fmt = true;
    } else if (!memcmp(h, "data", 4)) {
      if (data || !n) return false;
      result.offset = uint32_t(start);
      result.bytes = n;
      data = true;
    }
    pos = start + n + (n & 1U); // RIFF chunks are padded to an even boundary.
    if (pos > end) return false;
  }
  if (!fmt || !data || result.bytes % (result.channels * 2)) return false;
  info = result;
  return true;
}
