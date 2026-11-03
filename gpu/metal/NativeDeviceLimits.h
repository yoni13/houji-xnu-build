// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace houji_device_limits {
// Decode only the scalar record used by the target's _limits ivar. Field names
// come from Objective-C runtime metadata, not an Apple GPU capability profile.
// Validate the whole record before writing either measured texture dimension.
inline bool populate(void *record, size_t bytes, const char *encoding,
                     uint64_t width, uint64_t height) {
    if (!record || !encoding || *encoding++ != '{' || !width || !height ||
        width > UINT32_MAX || height > UINT32_MAX) return false;
    while (*encoding && *encoding != '=' && *encoding != '}') ++encoding;
    if (*encoding++ != '=') return false;
    size_t offset = 0, alignment = 1;
    size_t widthAt = SIZE_MAX, heightAt = SIZE_MAX;
    while (*encoding && *encoding != '}') {
        if (*encoding++ != '"') return false;
        const char *name = encoding;
        while (*encoding && *encoding != '"') ++encoding;
        if (*encoding++ != '"') return false;
        const size_t nameBytes = size_t(encoding - name - 1);
        const char type = *encoding;
        if (type != 'I' && type != 'f' && type != 'Q') return false;
        ++encoding;
        const size_t size = type == 'Q' ? 8 : 4;
        alignment = alignment < size ? size : alignment;
        if (offset > SIZE_MAX - (size - 1)) return false;
        offset = (offset + size - 1) & ~(size - 1);
        if (offset > bytes || size > bytes - offset) return false;
        const auto named = [&](const char *expected) {
            return nameBytes == strlen(expected) && !memcmp(name, expected, nameBytes);
        };
        if (named("maxTextureWidth2D")) {
            if (type != 'I' || widthAt != SIZE_MAX) return false;
            widthAt = offset;
        }
        if (named("maxTextureHeight2D")) {
            if (type != 'I' || heightAt != SIZE_MAX) return false;
            heightAt = offset;
        }
        offset += size;
    }
    if (*encoding != '}' || encoding[1] || offset > SIZE_MAX - (alignment - 1) ||
        ((offset + alignment - 1) & ~(alignment - 1)) != bytes ||
        widthAt == SIZE_MAX || heightAt == SIZE_MAX) return false;
    const uint32_t w = uint32_t(width), h = uint32_t(height);
    memcpy(static_cast<uint8_t *>(record) + widthAt, &w, sizeof w);
    memcpy(static_cast<uint8_t *>(record) + heightAt, &h, sizeof h);
    return true;
}
}
