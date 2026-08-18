"""Regenerate the embedded WebUI translation unit before every build.

The WebUI is compiled into the app image so that one OTA download updates
firmware and frontend together (docs/adr/0016-embed-webui-assets-in-firmware.md).
components/pf_web/CMakeLists.txt also runs the generator, but only at CMake
configure time -- and PlatformIO reconfigures solely when the root or src
CMakeLists.txt changes, so an edit to data/web/* would otherwise ship stale
assets. Running here closes that gap: the generator rewrites
components/pf_web/generated/* only when the content actually changes, so an
untouched frontend does not trigger a rebuild.
"""

import os
import subprocess
import sys

Import("env")

project_dir = env.subst("$PROJECT_DIR")
generator = os.path.join(project_dir, "tools", "generate_web_assets.py")
result = subprocess.run(
    [
        sys.executable,
        generator,
        "--source-dir", os.path.join(project_dir, "data", "web"),
        "--output-dir", os.path.join(
            project_dir, "components", "pf_web", "generated"),
    ],
    check=False,
)

if result.returncode != 0:
    raise RuntimeError(
        "generate_web_assets.py failed (%d); refusing to build firmware with "
        "stale or missing WebUI assets" % result.returncode)
