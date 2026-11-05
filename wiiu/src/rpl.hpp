#pragma once

/**
 * Reads the text inside a Wii U plugin (.wps) or module (.wms).
 *
 * They are RPL files whose sections are zlib-compressed, so searching the
 * raw file never finds anything. This unpacks the sections first. Used to
 * tell Protarium's Inkay ("Protarium Network Patcher") from Pretendo's.
 */

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG       // the PNG decoder brings the zlib decoder we need
#define STBI_NO_STDIO
#define STBI_SUPPORT_ZLIB
#define STBI_NO_FAILURE_STRINGS
#include "stb_image.h"

namespace RPL {

inline uint32_t BE32(const uint8_t *p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
inline uint16_t BE16(const uint8_t *p) { return uint16_t((p[0] << 8) | p[1]); }

constexpr uint32_t SHT_NOBITS     = 8;
constexpr uint32_t SHF_RPL_ZLIB   = 0x08000000;
constexpr uint32_t MAX_FILE       = 8 * 1024 * 1024;
constexpr uint32_t MAX_SECTION    = 16 * 1024 * 1024;

inline bool ReadFile(const std::string &path, std::vector<uint8_t> &out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return false;
    std::streamoff size = f.tellg();
    if (size <= 0 || size > MAX_FILE) return false;
    out.resize(static_cast<size_t>(size));
    f.seekg(0);
    f.read(reinterpret_cast<char *>(out.data()), size);
    return f.gcount() == size;
}

/**
 * Whether any section of the file contains `needle` (case-insensitive).
 * Sections are unpacked one at a time to keep memory low.
 */
inline bool Contains(const std::string &path, const char *needle) {
    std::vector<uint8_t> file;
    if (!ReadFile(path, file)) return false;

    const size_t size = file.size();
    const uint8_t *d  = file.data();
    if (size < 0x34 || memcmp(d, "\x7F" "ELF", 4) != 0 || d[4] != 1 || d[5] != 2) return false;

    uint32_t shoff     = BE32(d + 0x20);
    uint16_t shentsize = BE16(d + 0x2E);
    uint16_t shnum     = BE16(d + 0x30);
    if (shentsize < 40 || uint64_t(shoff) + uint64_t(shnum) * shentsize > size) return false;

    std::string lowNeedle = needle;
    for (char &c : lowNeedle) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

    std::vector<uint8_t> section;
    for (uint16_t i = 0; i < shnum; i++) {
        const uint8_t *h = d + shoff + size_t(i) * shentsize;
        uint32_t type = BE32(h + 4), flags = BE32(h + 8), offset = BE32(h + 16), length = BE32(h + 20);
        if (type == SHT_NOBITS || length == 0 || uint64_t(offset) + length > size) continue;

        const uint8_t *src = d + offset;
        if (flags & SHF_RPL_ZLIB) {
            if (length < 4) continue;
            uint32_t inflated = BE32(src);
            if (inflated == 0 || inflated > MAX_SECTION) continue;
            section.resize(inflated);
            int got = stbi_zlib_decode_buffer(reinterpret_cast<char *>(section.data()), int(inflated),
                                              reinterpret_cast<const char *>(src + 4), int(length - 4));
            if (got <= 0) continue;
            section.resize(size_t(got));
        }
        else {
            section.assign(src, src + length);
        }

        for (uint8_t &c : section) c = static_cast<uint8_t>(tolower(c));
        if (std::search(section.begin(), section.end(), lowNeedle.begin(), lowNeedle.end()) != section.end()) return true;
    }
    return false;
}

} // namespace RPL
