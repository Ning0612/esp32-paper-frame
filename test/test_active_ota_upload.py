import struct
import unittest
from pathlib import Path

from tools.platformio_active_ota_upload import (
    OTA_ABORTED,
    OTA_DATA_BYTES,
    OTA_INVALID,
    OTA_SECTOR_BYTES,
    ota_select_crc,
    parse_ota_entries,
    parse_ota_partitions,
    select_active_ota_partition,
)


PARTITION_TABLE = Path(__file__).parents[1] / "partitions" / "paperframe-dev.csv"


def make_ota_data(*entries: tuple[int, int, int]) -> bytes:
    data = bytearray(b"\xff" * OTA_DATA_BYTES)
    for sector_index, sequence, state in entries:
        offset = sector_index * OTA_SECTOR_BYTES
        struct.pack_into("<I", data, offset, sequence)
        struct.pack_into("<I", data, offset + 24, state)
        struct.pack_into("<I", data, offset + 28, ota_select_crc(sequence))
    return bytes(data)


class ActiveOtaUploadTest(unittest.TestCase):
    def test_partition_table_contains_contiguous_slots(self) -> None:
        self.assertEqual(
            parse_ota_partitions(PARTITION_TABLE),
            ((0x10000, 0x280000), (0x290000, 0x280000)),
        )

    def test_selects_highest_valid_sequence_and_maps_to_slot(self) -> None:
        ota_data = make_ota_data((0, 1, 2), (1, 2, 2))

        self.assertEqual(
            select_active_ota_partition(
                ota_data,
                ((0x10000, 0x280000), (0x290000, 0x280000)),
            ),
            (1, 0x290000, 0x280000),
        )

    def test_ignores_invalid_state_and_bad_crc(self) -> None:
        ota_data = bytearray(
            make_ota_data(
                (0, 3, OTA_INVALID),
                (1, 4, OTA_ABORTED),
            )
        )
        struct.pack_into("<I", ota_data, OTA_SECTOR_BYTES + 28, 0)

        self.assertEqual(parse_ota_entries(bytes(ota_data)), ())
        with self.assertRaisesRegex(ValueError, "no valid active"):
            select_active_ota_partition(bytes(ota_data), ((0x10000, 0x280000),))

    def test_rejects_missing_ota_partitions(self) -> None:
        with self.assertRaisesRegex(ValueError, "no OTA app partitions"):
            select_active_ota_partition(b"\xff" * OTA_DATA_BYTES, ())


if __name__ == "__main__":
    unittest.main()
