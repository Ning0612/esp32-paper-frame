import os

from SCons.Script import COMMAND_LINE_TARGETS

Import("env")

targets = set(COMMAND_LINE_TARGETS)
filesystem_targets = {"uploadfs", "uploadfsota"}

if "upload" in targets and filesystem_targets.intersection(targets):
    raise RuntimeError("Run app and filesystem uploads as separate PlatformIO commands")

if "upload" in targets and env.subst("$UPLOAD_PROTOCOL") == "esptool":
    upload_flags = list(env["UPLOADERFLAGS"])
    try:
        flash_size_index = upload_flags.index("--flash_size")
    except ValueError as error:
        raise RuntimeError(
            "Unexpected esptool flags; refusing to construct an app-only upload"
        ) from error

    # PlatformIO's ESP-IDF uploader appends the bootloader, partition table, and
    # initial OTA metadata after the flash-size value. The wrapper retains this
    # prefix as app-only esptool flags, reads otadata, and appends the active
    # partition offset and firmware image at upload time.
    env.Replace(UPLOADERFLAGS=upload_flags[: flash_size_index + 2])
    wrapper = os.path.join(
        env.subst("$PROJECT_DIR"),
        "tools",
        "platformio_active_ota_upload.py",
    )
    env.Replace(
        UPLOADCMD=(
            '"$PYTHONEXE" "{wrapper}" '
            '--uploader "$UPLOADER" '
            '--source "$SOURCE" '
            '--partition-table "$PARTITIONS_TABLE_CSV" '
            '-- $UPLOADERFLAGS'
        ).format(wrapper=wrapper)
    )
