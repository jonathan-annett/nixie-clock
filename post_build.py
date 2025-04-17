# post_build.py

import os
import shutil
import re

# ---- Config ----
version_header = "./version.h"
firmware_output_dir = "./firmware"  # relative to platformio project dir
version_txt = os.path.join(firmware_output_dir, "version.txt")

# ---- Helper: extract version from header ----
def extract_version():
    with open(version_header, "r") as f:
        for line in f:
            match = re.match(r'#define\s+FIRMWARE_VERSION\s+"(.+)"', line)
            if match:
                return match.group(1)
    raise ValueError("FIRMWARE_VERSION not found in version.h")

# ---- Main: copy and rename bin ----
def copy_firmware_0(env):
    firmware_path = env.subst("$PROG_PATH")
    print(f"[post_build] firmware_path = {firmware_path}")
    version = extract_version()

    if not os.path.exists(firmware_output_dir):
        os.makedirs(firmware_output_dir)

    dest_bin_path = os.path.join(firmware_output_dir, "firmware.bin")

    shutil.copy2(firmware_path, dest_bin_path)
    print(f"[post_build] Copied firmware from {firmware_path} to {dest_bin_path}")

    with open(version_txt, "w") as f:
        f.write(version + "\n")
    print(f"[post_build] Updated version.txt to {version}")


def copy_firmware(env):
    build_dir = env.subst("$BUILD_DIR")
    firmware_name =  "firmware.bin"
    
    firmware_path = os.path.join(build_dir, firmware_name)

    version = extract_version()

    if not os.path.exists(firmware_output_dir):
        os.makedirs(firmware_output_dir)

    dest_bin_path = os.path.join(firmware_output_dir, firmware_name)

    shutil.copy2(firmware_path, dest_bin_path)
    print(f"[post_build] Copied firmware from {firmware_path} to {dest_bin_path}")

    with open(version_txt, "w") as f:
        f.write(version + "\n")
    print(f"[post_build] Updated version.txt to {version}")


# ---- Hook into PlatformIO ----
def after_build(source, target, env):
    print(f"called after_build ( {source} , {target}, {env} )")
    copy_firmware(env)



Import("env")
env.AddPostAction("buildprog", after_build)
