#!/usr/bin/env bash

set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
contest_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
workspace=$(CDPATH= cd -- "$contest_dir/.." && pwd)
vendor_dir="$workspace/vendor/allwinnertech"
apps_dir="$workspace/apps"
ai_agent_dir="$workspace/packages/ai_agent"
legacy_patch="$contest_dir/patches/vendor_allwinnertech/r528-dshanpi-ili9341-display.patch"
twi3_patch="$contest_dir/patches/vendor_allwinnertech/r528-twi3-frequency-errors.patch"
dmic_patch="$contest_dir/patches/vendor_allwinnertech/r528-dshanpi-dmic-pins.patch"
led2_patch="$contest_dir/patches/vendor_allwinnertech/r528-dshanpi-led2-default-off.patch"
ai_agent_partition_patch="$contest_dir/patches/vendor_allwinnertech/r528-dshanpi-ai-agent-partition.patch"
audio_patch="$contest_dir/patches/vendor_allwinnertech/r528-audio-enable-and-test.patch"
playback_lifecycle_patch="$contest_dir/patches/vendor_allwinnertech/r528-playback-lifecycle.patch"
codec_playback_patch="$contest_dir/patches/vendor_allwinnertech/r528-dshanpi-codec-playback.patch"
nxplayer_patch="$contest_dir/patches/apps/nxplayer-lifecycle.patch"
nuttx_dir="$workspace/nuttx"
ft5x06_patch="$contest_dir/patches/nuttx/ft5x06-lvgl-compat.patch"
ai_agent_patch="$contest_dir/patches/packages_ai_agent/vela-desk-runtime.patch"

# The custom R528 board delegates late initialization to the vendor chip tree.
# Verify the Micro-TF source chain before enabling KWS capture on /sdcard.
required_sd_files=(
  "$vendor_dir/boards/r528/drivers/micro_sd/Kconfig"
  "$vendor_dir/boards/r528/drivers/micro_sd/Make.defs"
  "$vendor_dir/boards/r528/drivers/micro_sd/micro_sd_driver.c"
  "$vendor_dir/boards/r528/drivers/Make.defs"
  "$vendor_dir/chips/r528/r528_boot.c"
)
for required_file in "${required_sd_files[@]}"; do
  if [[ ! -f "$required_file" ]]; then
    printf 'Missing vendor Micro-TF file: %s\\n' "$required_file" >&2
    exit 1
  fi
done
if ! grep -q 'CONFIG_MICRO_TF' "$vendor_dir/chips/r528/r528_boot.c" ||
   ! grep -q 'micro_sd_initialize' "$vendor_dir/chips/r528/r528_boot.c"; then
  printf '%s\\n' \
    'Vendor r528_boot.c has no CONFIG_MICRO_TF initialization path.' >&2
  exit 1
fi
printf '%s\\n' 'Verified vendor Micro-TF source integration.'

if [[ ! -d "$ai_agent_dir/.git" ]]; then
  printf 'Missing ai_agent project: %s\n' "$ai_agent_dir" >&2
  exit 1
fi

if git -C "$ai_agent_dir" apply --reverse --check "$ai_agent_patch" 2>/dev/null; then
  printf '%s\n' "Vela desktop AI Agent runtime patch is already applied."
elif git -C "$ai_agent_dir" apply --check "$ai_agent_patch"; then
  git -C "$ai_agent_dir" apply "$ai_agent_patch"
  printf '%s\n' "Applied Vela desktop AI Agent runtime patch."
else
  printf '%s\n' \
    "Vela desktop AI Agent patch does not apply cleanly; inspect packages/ai_agent." >&2
  exit 1
fi

if git -C "$apps_dir" apply --reverse --check "$nxplayer_patch" 2>/dev/null; then
  printf '%s\n' "NxPlayer lifecycle patch is already applied."
elif git -C "$apps_dir" apply --check "$nxplayer_patch"; then
  git -C "$apps_dir" apply "$nxplayer_patch"
  printf '%s\n' "Applied NxPlayer lifecycle patch."
else
  printf '%s\n' "NxPlayer lifecycle patch does not apply cleanly; inspect the apps tree." >&2
  exit 1
fi

if git -C "$nuttx_dir" apply --reverse --check "$ft5x06_patch" 2>/dev/null; then
  printf '%s\n' "FT5x06 LVGL compatibility patch is already applied."
elif git -C "$nuttx_dir" apply --check "$ft5x06_patch"; then
  git -C "$nuttx_dir" apply "$ft5x06_patch"
  printf '%s\n' "Applied FT5x06 LVGL compatibility patch."
else
  printf '%s\n' "FT5x06 patch does not apply cleanly; inspect the NuttX tree." >&2
  exit 1
fi

if git -C "$vendor_dir" apply --reverse --check "$ai_agent_partition_patch" 2>/dev/null; then
  printf '%s\n' "AI Agent NAND partition-size patch is already applied."
elif git -C "$vendor_dir" apply --check "$ai_agent_partition_patch"; then
  git -C "$vendor_dir" apply "$ai_agent_partition_patch"
  printf '%s\n' "Applied AI Agent NAND partition-size patch."
else
  printf '%s\n' \
    "AI Agent NAND partition-size patch does not apply cleanly; inspect vendor/allwinnertech." >&2
  exit 1
fi

if git -C "$vendor_dir" apply --reverse --check "$twi3_patch" 2>/dev/null; then
  printf '%s\n' "R528 TWI3/frequency/error patch is already applied."
elif git -C "$vendor_dir" apply --check "$twi3_patch"; then
  git -C "$vendor_dir" apply "$twi3_patch"
  printf '%s\n' "Applied R528 TWI3/frequency/error patch."
else
  printf '%s\n' "R528 TWI3 patch does not apply cleanly; inspect vendor/allwinnertech." >&2
  exit 1
fi

if git -C "$vendor_dir" apply --reverse --check "$dmic_patch" 2>/dev/null; then
  printf '%s\n' "R528 DshanPi DMIC pin patch is already applied."
elif git -C "$vendor_dir" apply --check "$dmic_patch"; then
  git -C "$vendor_dir" apply "$dmic_patch"
  printf '%s\n' "Applied R528 DshanPi DMIC pin patch."
else
  printf '%s\n' "R528 DshanPi DMIC pin patch does not apply cleanly; inspect vendor/allwinnertech." >&2
  exit 1
fi

if git -C "$vendor_dir" apply --reverse --check "$led2_patch" 2>/dev/null; then
  printf '%s\n' "R528 DshanPi LED2 default-off patch is already applied."
elif git -C "$vendor_dir" apply --check "$led2_patch"; then
  git -C "$vendor_dir" apply "$led2_patch"
  printf '%s\n' "Applied R528 DshanPi LED2 default-off patch."
else
  printf '%s\n' \
    "R528 DshanPi LED2 patch does not apply cleanly; inspect vendor/allwinnertech." >&2
  exit 1
fi

if git -C "$vendor_dir" apply --reverse --check "$audio_patch" 2>/dev/null; then
  printf '%s\n' "R528 audio support/test patch is already applied."
elif git -C "$vendor_dir" apply --check "$audio_patch"; then
  git -C "$vendor_dir" apply "$audio_patch"
  printf '%s\n' "Applied R528 audio support/test patch."
else
  printf '%s\n' "R528 audio support/test patch does not apply cleanly; inspect vendor/allwinnertech." >&2
  exit 1
fi

if git -C "$vendor_dir" apply --reverse --check "$playback_lifecycle_patch" \
     2>/dev/null; then
  printf '%s\n' "R528 playback lifecycle patch is already applied."
elif git -C "$vendor_dir" apply --check "$playback_lifecycle_patch"; then
  git -C "$vendor_dir" apply "$playback_lifecycle_patch"
  printf '%s\n' "Applied R528 playback lifecycle patch."
else
  printf '%s\n' \
    "R528 playback lifecycle patch does not apply cleanly; inspect vendor/allwinnertech." >&2
  exit 1
fi

if git -C "$vendor_dir" apply --reverse --check "$codec_playback_patch" \
     2>/dev/null; then
  printf '%s\n' "R528 DshanPi Codec playback patch is already applied."
elif git -C "$vendor_dir" apply --check "$codec_playback_patch"; then
  git -C "$vendor_dir" apply "$codec_playback_patch"
  printf '%s\n' "Applied R528 DshanPi Codec playback patch."
else
  printf '%s\n' \
    "R528 DshanPi Codec playback patch does not apply cleanly; inspect vendor/allwinnertech." >&2
  exit 1
fi

if git -C "$vendor_dir" apply --reverse --check "$legacy_patch" 2>/dev/null; then
  cat <<'EOF'
The legacy R528 ILI9341/DISP2 patch is still applied in vendor/allwinnertech.
The ST7796U2 software-SPI path does not use it. To preserve unrelated vendor
work, this script will not reverse it automatically. Reverse only this patch
after inspecting the vendor working tree:

  git -C vendor/allwinnertech apply --reverse \
    contest2026_113_veladuxingke/patches/vendor_allwinnertech/r528-dshanpi-ili9341-display.patch
EOF
  exit 0
fi

printf '%s\n' \
  "Display uses contest board code; no active vendor display patch is needed."
