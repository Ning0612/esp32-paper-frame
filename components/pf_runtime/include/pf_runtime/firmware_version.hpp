#pragma once

#include <cstddef>

namespace pf_runtime {

// Single source of truth for the running firmware's version string. Used by
// pf_web's device/status serializers and by pf_ota's update-check comparison
// against a GitHub Release tag_name. Bump this alongside the git tag pushed
// to GitHub Releases (see the release checklist) or "check for update" will
// mismatch.
inline constexpr char kFirmwareVersion[] = "v0.9.2";

// NOTE: this is deliberately not a full SemVer 2.0.0 parser/comparator --
// it exists solely to answer "is this GitHub release tag an update over the
// running firmware" for pf_ota. Known, accepted simplifications (do not
// "fix" these without reconsidering whether a full parser is actually
// warranted for that purpose):
//   - Two pre-release tags with the same major.minor.patch always compare
//     equal regardless of their suffix content (e.g. "v1.2.3-rc1" and
//     "v1.2.3-rc2" are "equal", not ordered) -- pre-release identifier
//     precedence rules (SemVer §11.4 sub-cases) are not implemented.
//   - An empty suffix after '-' (e.g. "v1.2.3-") is accepted as
//     has_prerelease=true with no further validation; this still yields
//     the safe outcome (never ranked above the plain "v1.2.3" release).
// What IS guaranteed: a plain release always outranks a same-numbered
// pre-release and vice versa (SemVer §11.4's headline rule), and any tag
// that isn't at least "[v]MAJOR.MINOR.PATCH[-x][+y]" is rejected outright
// (comparable=false) rather than guessed at.
struct VersionCompareResult {
    // false when either string could not be parsed as a MAJOR.MINOR.PATCH
    // version; callers must treat this as "unable to determine", never as
    // "no update available" (an unparsable candidate tag must not silently
    // hide a real new release).
    bool comparable = false;
    // -1 candidate is older than running, 0 equal, 1 candidate is newer.
    // Only meaningful when comparable is true.
    int order = 0;
};

namespace detail {

struct ParsedVersion {
    bool ok = false;
    // True when a '-' pre-release suffix followed the patch number (e.g.
    // "v1.2.3-rc1"). Per SemVer 2.0.0 §11.4, a build WITHOUT a pre-release
    // suffix has higher precedence than one with, given equal
    // major.minor.patch -- tracked so compare_semver never treats a
    // pre-release as an equal-or-better candidate than a plain release.
    bool has_prerelease = false;
    unsigned int major = 0U;
    unsigned int minor = 0U;
    unsigned int patch = 0U;
};

constexpr bool is_digit(const char c)
{
    return c >= '0' && c <= '9';
}

// Parses an unsigned decimal number starting at text[index], advancing
// index past the consumed digits. Returns false if no digit was consumed
// or if the value would overflow unsigned int -- an overflowing component
// makes the whole tag unparsable rather than silently wrapping into a
// small, wrong version number.
constexpr bool parse_uint(
    const char* const text,
    std::size_t& index,
    unsigned int& value)
{
    if (!is_digit(text[index])) {
        return false;
    }
    unsigned long long accumulator = 0ULL;
    while (is_digit(text[index])) {
        accumulator = (accumulator * 10ULL) +
                      static_cast<unsigned long long>(text[index] - '0');
        if (accumulator > 0xFFFFFFFFULL) {
            return false;
        }
        ++index;
    }
    value = static_cast<unsigned int>(accumulator);
    return true;
}

// Parses "[v]MAJOR.MINOR.PATCH[-suffix|+suffix]". A leading 'v'/'V' is
// optional (GitHub tag convention); a '+' build-metadata suffix after the
// patch number is ignored entirely (never affects precedence, per SemVer);
// a '-' pre-release suffix is detected (see ParsedVersion::has_prerelease)
// but its content is not compared field-by-field -- sufficient for this
// project's release process, which does not chain multiple pre-release tags.
constexpr ParsedVersion parse_version(const char* const text)
{
    ParsedVersion parsed{};
    if (text == nullptr) {
        return parsed;
    }

    std::size_t index = 0U;
    if (text[index] == 'v' || text[index] == 'V') {
        ++index;
    }

    if (!parse_uint(text, index, parsed.major)) {
        return parsed;
    }
    if (text[index] != '.') {
        return parsed;
    }
    ++index;
    if (!parse_uint(text, index, parsed.minor)) {
        return parsed;
    }
    if (text[index] != '.') {
        return parsed;
    }
    ++index;
    if (!parse_uint(text, index, parsed.patch)) {
        return parsed;
    }

    // What follows must be end-of-string, or a pre-release/build suffix
    // introduced by '-' or '+'; anything else means the tag isn't a plain
    // semver we can trust (e.g. "1.2.3abc").
    if (text[index] == '-') {
        parsed.has_prerelease = true;
    } else if (text[index] != '\0' && text[index] != '+') {
        return parsed;
    }

    parsed.ok = true;
    return parsed;
}

}  // namespace detail

constexpr VersionCompareResult compare_semver(
    const char* const running,
    const char* const candidate_tag)
{
    const detail::ParsedVersion running_version =
        detail::parse_version(running);
    const detail::ParsedVersion candidate_version =
        detail::parse_version(candidate_tag);

    VersionCompareResult result{};
    if (!running_version.ok || !candidate_version.ok) {
        return result;
    }

    result.comparable = true;
    if (candidate_version.major != running_version.major) {
        result.order = candidate_version.major > running_version.major ? 1 : -1;
    } else if (candidate_version.minor != running_version.minor) {
        result.order = candidate_version.minor > running_version.minor ? 1 : -1;
    } else if (candidate_version.patch != running_version.patch) {
        result.order = candidate_version.patch > running_version.patch ? 1 : -1;
    } else if (candidate_version.has_prerelease !=
               running_version.has_prerelease) {
        // Equal major.minor.patch: the side WITHOUT a pre-release suffix
        // has higher precedence (SemVer 2.0.0 §11.4). This also means a
        // same-numbered pre-release is never reported as an update over
        // the plain release it precedes.
        result.order = running_version.has_prerelease ? 1 : -1;
    } else {
        result.order = 0;
    }
    return result;
}

}  // namespace pf_runtime
