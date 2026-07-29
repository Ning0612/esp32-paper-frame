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
    # initial OTA metadata after the flash-size value. The app image and its
    # partition offset are appended separately by UPLOADCMD, so retaining this
    # prefix makes routine uploads app-only.
    env.Replace(UPLOADERFLAGS=upload_flags[: flash_size_index + 2])
