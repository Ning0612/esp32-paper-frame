"""Validate a release tag against the firmware's single version source."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


VERSION_FILE = (
    Path(__file__).resolve().parents[1]
    / "components"
    / "pf_runtime"
    / "include"
    / "pf_runtime"
    / "firmware_version.hpp"
)
TAG_PATTERN = re.compile(
    r"v(?P<core>\d+\.\d+\.\d+)(?:-(?P<prerelease>[0-9A-Za-z.-]+))?\Z"
)
VERSION_PATTERN = re.compile(
    r'kFirmwareVersion\[\]\s*=\s*"(?P<version>[^"]+)"'
)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate a release tag against kFirmwareVersion."
    )
    parser.add_argument("tag", help="Git tag to validate, for example v0.8.0")
    args = parser.parse_args()

    tag_match = TAG_PATTERN.fullmatch(args.tag)
    if tag_match is None:
        raise SystemExit(
            f"invalid release tag {args.tag!r}; expected vMAJOR.MINOR.PATCH"
            " or a prerelease suffix"
        )

    if any(
        component != "0" and component.startswith("0")
        for component in tag_match.group("core").split(".")
    ):
        raise SystemExit(f"invalid release tag {args.tag!r}; numeric parts have leading zero")

    prerelease = tag_match.group("prerelease")
    if prerelease is not None:
        identifiers = prerelease.split(".")
        if any(not identifier for identifier in identifiers):
            raise SystemExit(f"invalid release tag {args.tag!r}; empty prerelease identifier")
        for identifier in identifiers:
            if identifier.isdigit() and len(identifier) > 1 and identifier.startswith("0"):
                raise SystemExit(
                    f"invalid release tag {args.tag!r}; numeric prerelease has leading zero"
                )

    source = VERSION_FILE.read_text(encoding="utf-8")
    match = VERSION_PATTERN.search(source)
    if match is None:
        raise SystemExit(f"kFirmwareVersion not found in {VERSION_FILE}")

    firmware_version = match.group("version")
    if firmware_version != args.tag:
        raise SystemExit(
            "release tag does not match kFirmwareVersion: "
            f"tag={args.tag!r}, firmware={firmware_version!r}"
        )

    print(f"release version verified: {args.tag}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
