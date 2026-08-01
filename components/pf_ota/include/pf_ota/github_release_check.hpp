#pragma once

#include <cstddef>

namespace pf_ota {

inline constexpr std::size_t kGithubTagNameCapacity = 24U;

struct GithubTagExtractResult {
    bool ok = false;
    char tag_name[kGithubTagNameCapacity] = {};
};

namespace detail {

constexpr bool is_json_space(const char value)
{
    return value == ' ' || value == '\t' || value == '\n' || value == '\r';
}

}  // namespace detail

// Extracts the top-level "tag_name" string field from a GitHub Releases API
// response body (e.g. GET /repos/<owner>/<repo>/releases/latest), without a
// general JSON parser: tracks brace/bracket depth so a "tag_name" key
// nested inside "assets" or any other nested object/array is ignored, and
// only the flat top-level field is matched (same bounded flat-key-scan
// approach as pf_weather::weather.cpp's OpenWeatherMap parser). json need
// not be null-terminated; length bounds the scan.
constexpr GithubTagExtractResult extract_tag_name(
    const char* const json,
    const std::size_t length)
{
    GithubTagExtractResult result{};
    if (json == nullptr || length == 0U) {
        return result;
    }

    constexpr char kKey[] = "tag_name";
    constexpr std::size_t kKeyLength = 8U;

    std::size_t cursor = 0U;
    while (cursor < length && detail::is_json_space(json[cursor])) {
        ++cursor;
    }
    if (cursor >= length || json[cursor] != '{') {
        return result;
    }
    ++cursor;

    int depth = 1;
    while (cursor < length && depth > 0) {
        const char current = json[cursor];
        if (current == '"') {
            const std::size_t quote = cursor;
            ++cursor;
            bool escaped = false;
            while (cursor < length) {
                const char byte = json[cursor++];
                if (escaped) {
                    escaped = false;
                } else if (byte == '\\') {
                    escaped = true;
                } else if (byte == '"') {
                    break;
                }
            }
            if (cursor > length) {
                return result;
            }
            const std::size_t string_end = cursor;

            bool key_matches = depth == 1 &&
                                (string_end - quote) == kKeyLength + 2U;
            if (key_matches) {
                for (std::size_t index = 0U; index < kKeyLength; ++index) {
                    if (json[quote + 1U + index] != kKey[index]) {
                        key_matches = false;
                        break;
                    }
                }
            }

            if (key_matches) {
                std::size_t after_key = cursor;
                while (after_key < length &&
                       detail::is_json_space(json[after_key])) {
                    ++after_key;
                }
                if (after_key >= length || json[after_key] != ':') {
                    return result;
                }
                ++after_key;
                while (after_key < length &&
                       detail::is_json_space(json[after_key])) {
                    ++after_key;
                }
                if (after_key >= length || json[after_key] != '"') {
                    // tag_name must be a JSON string; anything else means
                    // this response doesn't look like what we expect.
                    return result;
                }
                ++after_key;

                std::size_t write_index = 0U;
                while (after_key < length) {
                    const char byte = json[after_key++];
                    if (byte == '\\') {
                        // GitHub tag names never legitimately contain
                        // escape sequences; treat one as unparsable rather
                        // than guessing at unescaping rules.
                        return result;
                    }
                    if (byte == '"') {
                        if (write_index == 0U) {
                            return result;
                        }
                        result.tag_name[write_index] = '\0';
                        result.ok = true;
                        return result;
                    }
                    if (write_index >= kGithubTagNameCapacity - 1U) {
                        return result;
                    }
                    result.tag_name[write_index++] = byte;
                }
                return result;
            }
            continue;
        }
        if (current == '{' || current == '[') {
            ++depth;
        } else if (current == '}' || current == ']') {
            --depth;
        }
        ++cursor;
    }
    return result;
}

}  // namespace pf_ota
