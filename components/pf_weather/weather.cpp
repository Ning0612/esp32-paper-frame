#include "pf_weather/weather.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>

namespace pf_weather {
namespace {

bool is_space(const char value)
{
    return std::isspace(static_cast<unsigned char>(value)) != 0;
}

bool matches_key(
    const char* const json,
    const std::size_t length,
    const std::size_t quote,
    const char* const key)
{
    if (json == nullptr || key == nullptr || quote >= length ||
        json[quote] != '"') {
        return false;
    }
    const std::size_t key_length = std::strlen(key);
    if (length - quote < key_length + 2U ||
        std::memcmp(json + quote + 1U, key, key_length) != 0 ||
        json[quote + 1U + key_length] != '"') {
        return false;
    }
    return true;
}

bool find_key(
    const char* const json,
    const std::size_t begin,
    const std::size_t end,
    const char* const key,
    std::size_t& value_begin)
{
    if (json == nullptr || begin >= end) {
        return false;
    }
    std::size_t cursor = begin;
    while (cursor < end && is_space(json[cursor])) {
        ++cursor;
    }
    if (cursor >= end || json[cursor] != '{') {
        return false;
    }
    int depth = 1;
    ++cursor;
    while (cursor < end && depth > 0) {
        const char current = json[cursor];
        if (current == '"') {
            const std::size_t quote = cursor;
            ++cursor;
            bool escaped = false;
            while (cursor < end) {
                const char string_byte = json[cursor++];
                if (escaped) {
                    escaped = false;
                } else if (string_byte == '\\') {
                    escaped = true;
                } else if (string_byte == '"') {
                    break;
                }
            }
            if (cursor > end ||
                (cursor == end && json[cursor - 1U] != '"')) {
                return false;
            }
            if (depth == 1 && matches_key(json, end, quote, key)) {
                std::size_t after_key = cursor;
                while (after_key < end && is_space(json[after_key])) {
                    ++after_key;
                }
                if (after_key < end && json[after_key] == ':') {
                    ++after_key;
                    while (after_key < end && is_space(json[after_key])) {
                        ++after_key;
                    }
                    if (after_key < end) {
                        value_begin = after_key;
                        return true;
                    }
                }
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
    return false;
}

bool find_object_end(
    const char* const json,
    const std::size_t begin,
    const std::size_t end,
    std::size_t& object_end)
{
    if (json == nullptr || begin >= end || json[begin] != '{') {
        return false;
    }
    int depth = 0;
    bool escaped = false;
    bool in_string = false;
    for (std::size_t cursor = begin; cursor < end; ++cursor) {
        const char current = json[cursor];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (current == '\\') {
                escaped = true;
            } else if (current == '"') {
                in_string = false;
            }
            continue;
        }
        if (current == '"') {
            in_string = true;
        } else if (current == '{') {
            ++depth;
        } else if (current == '}') {
            --depth;
            if (depth == 0) {
                object_end = cursor + 1U;
                return true;
            }
        }
    }
    return false;
}

bool parse_number(
    const char* const json,
    const std::size_t begin,
    const std::size_t end,
    double& value,
    std::size_t& consumed)
{
    if (json == nullptr || begin >= end) {
        return false;
    }
    char buffer[48]{};
    std::size_t length = 0U;
    std::size_t cursor = begin;
    while (cursor < end && length + 1U < sizeof(buffer)) {
        const char current = json[cursor];
        if (!((current >= '0' && current <= '9') ||
              current == '-' || current == '+' || current == '.' ||
              current == 'e' || current == 'E')) {
            break;
        }
        buffer[length++] = current;
        ++cursor;
    }
    if (length == 0U) {
        return false;
    }
    char* parse_end = nullptr;
    value = std::strtod(buffer, &parse_end);
    if (parse_end == nullptr ||
        static_cast<std::size_t>(parse_end - buffer) != length) {
        return false;
    }
    if (cursor < end && !is_space(json[cursor]) && json[cursor] != ',' &&
        json[cursor] != '}' && json[cursor] != ']') {
        return false;
    }
    consumed = cursor;
    return true;
}

bool parse_integer(
    const char* const json,
    const std::size_t begin,
    const std::size_t end,
    std::int64_t& value,
    std::size_t& consumed)
{
    if (json == nullptr || begin >= end) {
        return false;
    }
    char buffer[32]{};
    std::size_t length = 0U;
    std::size_t cursor = begin;
    if (json[cursor] == '+' || json[cursor] == '-') {
        buffer[length++] = json[cursor++];
    }
    const std::size_t digits_begin = length;
    while (cursor < end && json[cursor] >= '0' && json[cursor] <= '9') {
        if (length + 1U >= sizeof(buffer)) {
            return false;
        }
        buffer[length++] = json[cursor++];
    }
    if (length == digits_begin) {
        return false;
    }
    buffer[length] = '\0';
    errno = 0;
    char* parse_end = nullptr;
    const long long parsed = std::strtoll(buffer, &parse_end, 10);
    if (errno == ERANGE || parse_end == nullptr ||
        static_cast<std::size_t>(parse_end - buffer) != length) {
        return false;
    }
    if (cursor < end && !is_space(json[cursor]) && json[cursor] != ',' &&
        json[cursor] != '}' && json[cursor] != ']') {
        return false;
    }
    value = static_cast<std::int64_t>(parsed);
    consumed = cursor;
    return true;
}

bool parse_string(
    const char* const json,
    const std::size_t begin,
    const std::size_t end,
    char* const output,
    const std::size_t capacity,
    std::size_t& consumed)
{
    if (json == nullptr || output == nullptr || capacity == 0U ||
        begin >= end || json[begin] != '"') {
        return false;
    }
    std::size_t output_length = 0U;
    std::size_t cursor = begin + 1U;
    while (cursor < end) {
        const char current = json[cursor++];
        if (current == '"') {
            output[output_length] = '\0';
            consumed = cursor;
            return true;
        }
        if (static_cast<unsigned char>(current) < 0x20U) {
            return false;
        }
        char decoded = current;
        if (current == '\\') {
            if (cursor >= end) {
                return false;
            }
            const char escaped = json[cursor++];
            switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    decoded = escaped;
                    break;
                case 'b':
                    decoded = '\b';
                    break;
                case 'f':
                    decoded = '\f';
                    break;
                case 'n':
                    decoded = '\n';
                    break;
                case 'r':
                    decoded = '\r';
                    break;
                case 't':
                    decoded = '\t';
                    break;
                case 'u':
                    // Keep the parser bounded. OpenWeather descriptions are
                    // UTF-8 in normal responses; escaped Unicode is rejected
                    // instead of silently fabricating a partial string.
                    if (cursor + 4U > end) {
                        return false;
                    }
                    return false;
                default:
                    return false;
            }
        }
        if (output_length + 1U >= capacity) {
            return false;
        }
        output[output_length++] = decoded;
    }
    return false;
}

bool value_for_key(
    const char* const json,
    const std::size_t begin,
    const std::size_t end,
    const char* const key,
    std::size_t& value_begin)
{
    return find_key(json, begin, end, key, value_begin);
}

bool parse_code(
    const char* const json,
    const std::size_t begin,
    const std::size_t end,
    bool& rejected)
{
    std::size_t value_begin = 0U;
    if (!value_for_key(json, begin, end, "cod", value_begin)) {
        return true;
    }
    std::int64_t code = 0;
    std::size_t consumed = 0U;
    if (json[value_begin] == '"') {
        char code_text[8]{};
        if (!parse_string(
                json,
                value_begin,
                end,
                code_text,
                sizeof(code_text),
                consumed)) {
            return false;
        }
        char* parse_end = nullptr;
        const long parsed = std::strtol(code_text, &parse_end, 10);
        if (parse_end == nullptr || *parse_end != '\0') {
            return false;
        }
        code = parsed;
    } else if (!parse_integer(json, value_begin, end, code, consumed)) {
        return false;
    }
    rejected = code != 200;
    return true;
}

}  // namespace

const char* to_string(const ParseError error)
{
    switch (error) {
        case ParseError::none:
            return "none";
        case ParseError::invalid_argument:
            return "invalid_argument";
        case ParseError::malformed_json:
            return "malformed_json";
        case ParseError::api_rejected:
            return "api_rejected";
        case ParseError::missing_field:
            return "missing_field";
        case ParseError::invalid_value:
            return "invalid_value";
    }
    return "invalid_value";
}

ParseResult parse_current_weather(
    const char* const json,
    const std::size_t length)
{
    ParseResult result{};
    if (json == nullptr || length == 0U) {
        result.error = ParseError::invalid_argument;
        return result;
    }

    bool api_rejected = false;
    if (!parse_code(json, 0U, length, api_rejected)) {
        result.error = ParseError::malformed_json;
        return result;
    }
    if (api_rejected) {
        result.error = ParseError::api_rejected;
        return result;
    }

    std::size_t main_begin = 0U;
    std::size_t weather_begin = 0U;
    std::size_t value_begin = 0U;
    std::size_t timestamp_begin = 0U;
    if (!value_for_key(json, 0U, length, "main", main_begin) ||
        !value_for_key(json, 0U, length, "weather", weather_begin) ||
        !value_for_key(json, 0U, length, "dt", timestamp_begin)) {
        result.error = ParseError::missing_field;
        return result;
    }

    std::size_t main_end = 0U;
    if (!find_object_end(json, main_begin, length, main_end)) {
        result.error = ParseError::malformed_json;
        return result;
    }
    double temperature = 0.0;
    std::size_t consumed = 0U;
    if (!value_for_key(
            json,
            main_begin,
            main_end,
            "temp",
            value_begin) ||
        !parse_number(json, value_begin, main_end, temperature, consumed) ||
        temperature < -200.0 || temperature > 200.0) {
        result.error = ParseError::invalid_value;
        return result;
    }

    std::int64_t observed_at = 0;
    if (!parse_integer(
            json,
            timestamp_begin,
            length,
            observed_at,
            consumed)) {
        result.error = ParseError::invalid_value;
        return result;
    }
    if (observed_at <= 0) {
        result.error = ParseError::invalid_value;
        return result;
    }

    while (weather_begin < length && is_space(json[weather_begin])) {
        ++weather_begin;
    }
    if (weather_begin >= length || json[weather_begin] != '[') {
        result.error = ParseError::malformed_json;
        return result;
    }
    ++weather_begin;
    while (weather_begin < length && is_space(json[weather_begin])) {
        ++weather_begin;
    }
    if (weather_begin >= length || json[weather_begin] != '{') {
        result.error = ParseError::missing_field;
        return result;
    }
    std::size_t weather_object_end = 0U;
    if (!find_object_end(json, weather_begin, length, weather_object_end)) {
        result.error = ParseError::malformed_json;
        return result;
    }

    if (!value_for_key(
            json,
            weather_begin,
            weather_object_end,
            "id",
            value_begin)) {
        result.error = ParseError::missing_field;
        return result;
    }
    std::int64_t weather_id = 0;
    if (!parse_integer(json, value_begin, weather_object_end, weather_id, consumed) ||
        weather_id <= 0 || weather_id > INT32_MAX) {
        result.error = ParseError::invalid_value;
        return result;
    }

    if (!value_for_key(
            json,
            weather_begin,
            weather_object_end,
            "description",
            value_begin) ||
        !parse_string(
            json,
            value_begin,
            weather_object_end,
            result.observation.description,
            sizeof(result.observation.description),
            consumed) ||
        !value_for_key(
            json,
            weather_begin,
            weather_object_end,
            "icon",
            value_begin) ||
        !parse_string(
            json,
            value_begin,
            weather_object_end,
            result.observation.icon,
            sizeof(result.observation.icon),
            consumed)) {
        result.error = ParseError::missing_field;
        return result;
    }

    std::int64_t humidity = -1;
    if (value_for_key(json, main_begin, main_end, "humidity", value_begin) &&
        !parse_integer(json, value_begin, main_end, humidity, consumed)) {
        result.error = ParseError::invalid_value;
        return result;
    }
    if (humidity < -1 || humidity > 100) {
        result.error = ParseError::invalid_value;
        return result;
    }

    result.observation.temperature = static_cast<float>(temperature);
    result.observation.humidity_percent = static_cast<std::int16_t>(humidity);
    result.observation.weather_id = static_cast<std::int32_t>(weather_id);
    result.observation.observed_at_epoch_s =
        static_cast<std::uint64_t>(observed_at);
    std::size_t location_begin = 0U;
    if (value_for_key(json, 0U, length, "name", location_begin)) {
        if (!parse_string(
                json,
                location_begin,
                length,
                result.observation.location,
                sizeof(result.observation.location),
                consumed)) {
            result.error = ParseError::invalid_value;
            return result;
        }
    }
    return result;
}

const char* to_string(const Failure failure)
{
    switch (failure) {
        case Failure::none:
            return "none";
        case Failure::api_key_invalid:
            return "api_key_invalid";
        case Failure::network:
            return "network";
        case Failure::http_error:
            return "http_error";
        case Failure::parse_error:
            return "parse_error";
    }
    return "parse_error";
}

void record_success(
    Cache& cache,
    const Observation& observation,
    const std::uint64_t success_epoch_s,
    const std::uint64_t now_ms,
    const std::uint64_t interval_ms)
{
    cache.observation = observation;
    cache.has_observation = true;
    cache.last_success_epoch_s = success_epoch_s;
    cache.next_attempt_ms = now_ms > UINT64_MAX - interval_ms
                                 ? UINT64_MAX
                                 : now_ms + interval_ms;
    cache.consecutive_failures = 0U;
    cache.last_failure = Failure::none;
}

PerformFailure classify_perform_failure(const int status_code)
{
    // No status line: the request never reached a server (DNS, TCP, TLS).
    // ESP-IDF seeds status_code with -1 (esp_http_client.c) and overwrites it
    // only when the response status is parsed, so the guard covers both that
    // sentinel and a defensive 0.
    if (status_code <= 0) {
        return {Failure::network, false};
    }
    // 4xx/5xx: ESP-IDF gives up on some of these before perform() returns --
    // notably a 401 carrying no WWW-Authenticate header, which is exactly
    // what OpenWeatherMap sends for a bad key. Classifying on the status is
    // the whole point of this function.
    if (status_code >= 400) {
        return {classify_http_status(status_code), true};
    }
    // A 2xx (or a 3xx left over from a redirect ESP-IDF handled internally)
    // together with a failed perform() means the response itself was cut
    // short. That is an HTTP-level problem, matching how a truncated
    // response body is already reported -- and crucially not `network`,
    // since the server plainly answered. Reporting it as a network fault is
    // what made a mistyped API key look like the internet was down.
    return {Failure::http_error, true};
}

Failure classify_http_status(const int status_code)
{
    if (status_code == 200) {
        return Failure::none;
    }
    if (status_code == 401) {
        return Failure::api_key_invalid;
    }
    return Failure::http_error;
}

void record_failure(
    Cache& cache,
    const Failure failure,
    const std::uint64_t now_ms)
{
    if (cache.consecutive_failures < 31U) {
        ++cache.consecutive_failures;
    }
    const std::uint32_t shift =
        std::min<std::uint32_t>(cache.consecutive_failures - 1U, 6U);
    const std::uint64_t delay = std::min<std::uint64_t>(
        kMaximumRetryMs,
        kInitialRetryMs << shift);
    cache.next_attempt_ms =
        now_ms > UINT64_MAX - delay ? UINT64_MAX : now_ms + delay;
    cache.last_failure = failure;
}

bool retry_due(const Cache& cache, const std::uint64_t now_ms)
{
    return now_ms >= cache.next_attempt_ms;
}

bool stale(
    const Cache& cache,
    const std::uint64_t now_epoch_s,
    const std::uint64_t max_age_seconds)
{
    if (!cache.has_observation || cache.last_success_epoch_s == 0U ||
        max_age_seconds == 0U || now_epoch_s < cache.last_success_epoch_s) {
        return true;
    }
    return now_epoch_s - cache.last_success_epoch_s > max_age_seconds;
}

}  // namespace pf_weather
