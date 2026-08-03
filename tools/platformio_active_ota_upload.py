"""Upload the application image to the OTA slot selected by otadata.

PlatformIO's ESP-IDF upload command uses a fixed application offset. This
wrapper keeps the existing app-only esptool flags, reads the two OTA select
entries, and appends the application offset selected by the bootloader.
"""

from __future__ import annotations

import argparse
import struct
import subprocess
import sys
import tempfile
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence


OTA_DATA_BYTES = 0x2000
OTA_DATA_OFFSET = 0xD000
OTA_SECTOR_BYTES = 0x1000
OTA_STATE_OFFSET = 24
OTA_CRC_OFFSET = 28
OTA_UNDEFINED = 0xFFFFFFFF
OTA_INVALID = 0x3
OTA_ABORTED = 0x4


@dataclass(frozen=True)
class OtaEntry:
    sector_index: int
    sequence: int
    state: int


def ota_select_crc(sequence: int) -> int:
    """Match ESP-IDF bootloader_common_ota_select_crc()."""

    sequence_bytes = struct.pack("<I", sequence)
    return zlib.crc32(sequence_bytes, 0xFFFFFFFF) & 0xFFFFFFFF


def parse_ota_entries(ota_data: bytes) -> tuple[OtaEntry, ...]:
    if len(ota_data) != OTA_DATA_BYTES:
        raise ValueError(
            f"otadata must be exactly {OTA_DATA_BYTES} bytes, "
            f"got {len(ota_data)}"
        )

    entries: list[OtaEntry] = []
    for sector_index in range(2):
        offset = sector_index * OTA_SECTOR_BYTES
        sequence = struct.unpack_from("<I", ota_data, offset)[0]
        state = struct.unpack_from(
            "<I", ota_data, offset + OTA_STATE_OFFSET
        )[0]
        stored_crc = struct.unpack_from(
            "<I", ota_data, offset + OTA_CRC_OFFSET
        )[0]
        if sequence == OTA_UNDEFINED:
            continue
        if state in (OTA_INVALID, OTA_ABORTED):
            continue
        if stored_crc != ota_select_crc(sequence):
            continue
        entries.append(OtaEntry(sector_index, sequence, state))
    return tuple(entries)


def parse_ota_partitions(partition_table: Path) -> tuple[tuple[int, int], ...]:
    partitions: dict[int, tuple[int, int]] = {}
    for raw_line in partition_table.read_text(encoding="utf-8-sig").splitlines():
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        fields = [field.strip() for field in line.split(",")]
        if len(fields) < 5 or fields[1].lower() != "app":
            continue
        subtype = fields[2].lower()
        if not subtype.startswith("ota_"):
            continue
        try:
            slot_index = int(subtype[4:], 10)
            offset = int(fields[3], 0)
            size = int(fields[4], 0)
        except ValueError as error:
            raise ValueError(
                f"invalid OTA partition row: {raw_line!r}"
            ) from error
        if slot_index in partitions:
            raise ValueError(f"duplicate OTA partition ota_{slot_index}")
        partitions[slot_index] = (offset, size)

    if not partitions:
        raise ValueError(f"no OTA app partitions in {partition_table}")
    expected_slots = tuple(range(len(partitions)))
    if tuple(sorted(partitions)) != expected_slots:
        raise ValueError(
            "OTA app partitions must be contiguous from ota_0; "
            f"found {tuple(sorted(partitions))}"
        )
    return tuple(partitions[index] for index in expected_slots)


def select_active_ota_partition(
    ota_data: bytes,
    ota_partitions: Sequence[tuple[int, int]],
) -> tuple[int, int, int]:
    if not ota_partitions:
        raise ValueError("no OTA app partitions available")
    entries = parse_ota_entries(ota_data)
    if not entries:
        raise ValueError("otadata has no valid active OTA entry")
    active = max(entries, key=lambda entry: entry.sequence)
    slot_index = (active.sequence - 1) % len(ota_partitions)
    offset, size = ota_partitions[slot_index]
    return slot_index, offset, size


def _replace_argument_value(
    arguments: list[str], name: str, value: str
) -> None:
    index = arguments.index(name)
    if index + 1 >= len(arguments):
        raise ValueError(f"uploader flags missing value for {name}")
    arguments[index + 1] = value


def _read_otadata(
    uploader: Path,
    upload_arguments: Sequence[str],
    destination: Path,
) -> None:
    read_arguments = list(upload_arguments)
    try:
        write_index = read_arguments.index("write_flash")
    except ValueError as error:
        raise ValueError("uploader flags missing write_flash") from error
    read_arguments = read_arguments[:write_index]
    _replace_argument_value(read_arguments, "--after", "no_reset")
    read_arguments.extend(
        [
            "read_flash",
            hex(OTA_DATA_OFFSET),
            hex(OTA_DATA_BYTES),
            str(destination),
        ]
    )
    subprocess.run(
        [sys.executable, str(uploader), *read_arguments],
        check=True,
    )


def upload_active_slot(
    uploader: Path,
    source: Path,
    partition_table: Path,
    upload_arguments: Sequence[str],
) -> None:
    ota_partitions = parse_ota_partitions(partition_table)
    with tempfile.TemporaryDirectory(prefix="paperframe-otadata-") as temp_dir:
        ota_data_path = Path(temp_dir) / "otadata.bin"
        _read_otadata(uploader, upload_arguments, ota_data_path)
        slot_index, offset, size = select_active_ota_partition(
            ota_data_path.read_bytes(), ota_partitions
        )

    if source.stat().st_size > size:
        raise ValueError(
            f"firmware size {source.stat().st_size} exceeds ota_{slot_index} "
            f"capacity {size}"
        )

    write_arguments = list(upload_arguments)
    write_arguments.extend([hex(offset), str(source)])
    print(
        f"Uploading active ota_{slot_index} app slot at {hex(offset)} "
        f"({size} bytes available)"
    )
    subprocess.run(
        [sys.executable, str(uploader), *write_arguments],
        check=True,
    )


def _parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Upload firmware to the active ESP-IDF OTA app slot."
    )
    parser.add_argument("--uploader", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--partition-table", type=Path, required=True)
    parser.add_argument(
        "upload_arguments",
        nargs=argparse.REMAINDER,
        help="original PlatformIO esptool flags after --",
    )
    arguments = parser.parse_args()
    if arguments.upload_arguments[:1] == ["--"]:
        arguments.upload_arguments = arguments.upload_arguments[1:]
    return arguments


def main() -> int:
    arguments = _parse_arguments()
    try:
        upload_active_slot(
            arguments.uploader,
            arguments.source,
            arguments.partition_table,
            arguments.upload_arguments,
        )
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"active OTA upload failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
