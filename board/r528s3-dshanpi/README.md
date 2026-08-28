# R528S3-DshanPi board support

[ English | [简体中文](README_zh-cn.md) ]

This directory is the board support package used by the **Vela Smart Desktop
Assistant** contest submission. It targets the 100ask R528S3-DshanPi (Allwinner
R528, dual-core Cortex-A7) with the ST7796U2 display, FT5x06 touch controller,
on-board DMIC/audio codec, Realtek Wi-Fi and the external TWI3 environment
sensors.

## What is included

- Custom R528 board initialization and the 480x320 RGB565 ST7796U2 framebuffer;
- FT5x06 touch input at `/dev/input0`;
- ROMFS startup scripts, Chinese font, Wi-Fi/BT resources and routine voice PCM
  assets;
- The `nsh` configuration used for the final image, including `R528_ST7796`,
  TWI3, audio/DMIC, Micro-TF and the four contest applications;
- Packaging helpers under `build/`.

The contest manifest links the four applications from this repository into the
openvela build tree. The board itself is selected through
`CONFIG_ARCH_BOARD_CUSTOM_DIR`; no vendor board directory needs to be copied or
edited by hand.

## Hardware connections

| Function | Connection |
| --- | --- |
| ST7796U2 SPI1 | CS=PD10, SCK=PD11, MOSI=PD12, DC=PD14, RESET=PD15, backlight=PD16 |
| FT5x06 touch | TWI2, address `0x38`, SDA=PE13, SCL=PE12, RESET=PE0, INT=PE1 |
| TWI3 sensors | SCL=PE6 (J3 pin 11), SDA=PE7 (J3 pin 10), 3.3 V on pin 1/17, GND on pin 9/14/20 |
| Sensors | SHT40 `0x44`, BH1750 `0x23` (fallback `0x5c`), SGP30 `0x58` |
| DMIC | R528 board DMIC pins, including the DshanPi PD18 data-line fix in the vendor patch |

The supplied board configuration uses TWI3 at 100 kHz and keeps TWI2 at the
FT5x06-compatible speed. Check the board schematic before connecting external
modules; all sensor signals are 3.3 V.

## Reproduce the build

Use Ubuntu 20.04 or later with the openvela build prerequisites, `repo`, and an
ARM toolchain. The complete dependency list is in the Chinese guide linked from
the repository root.

From a new workspace:

```bash
mkdir openvela-workspace
cd openvela-workspace
repo init -u https://github.com/open-vela/contest2026_113_veladuxingke \
  -b dev-ai-contest-2026 -m contest2026_113_veladuxingke.xml
repo sync -c -j8
```

The result has `contest2026_113_veladuxingke/`, `nuttx/`, `apps/`, and
`vendor/` at the same level. Apply the reproducible, repository-owned patches
from the workspace root:

```bash
./contest2026_113_veladuxingke/tools/apply_vendor_patches.sh
```

The script verifies the vendor Micro-TF source chain and applies the NuttX,
NxPlayer, TWI3, DMIC, GPIO, audio and codec fixes. It is idempotent and stops
on a version mismatch.

Build the final `nsh` image from the workspace root:

```bash
source build/envsetup.sh
./build.sh contest2026_113_veladuxingke/board/r528s3-dshanpi/configs/nsh/ -j8
```

When changing configuration or after a failed full build, run a clean build:

```bash
./build.sh contest2026_113_veladuxingke/board/r528s3-dshanpi/configs/nsh/ -j8 distclean
./build.sh contest2026_113_veladuxingke/board/r528s3-dshanpi/configs/nsh/ -j8
```

## Package and flash

After a successful build, package the NAND image:

```bash
# Load the openvela environment first. It supplies the host build-tool PATH.
cd /path/to/openvela
source build/envsetup.sh

cd vendor/allwinnertech/lichee
source envsetup.sh
lunch_nuttx r528s3-dshanpi
pack
```

Do not start another `bash` after sourcing the lichee environment:
`lunch_nuttx` and `pack` are shell functions and would not be available in the
child shell. If the lichee setup reports a missing `prebuilts/kconfig-frontends`,
return to the openvela root and source `build/envsetup.sh` before lichee's
environment.

The expected output is:

```text
vendor/allwinnertech/lichee/out/r528s3/dshanpi_nand/rtos_nuttx_r528s3-dshanpi_uart0_256Mnand.img
```

Treat the image as valid only when the pack log contains both `Dragon execute
image.cfg SUCCESS !` and `pack finish`, and the image timestamp and SHA-256
match the current build. The image is a workspace output and is not committed
to this source repository.

To flash, select the image in PhoenixSuit, power off the board, hold FEL while
connecting USB and powering on, start the upgrade, then power-cycle after it
finishes. The first boot mounts ROMFS and starts `wifi_manager`, `lan_panel`,
and `routine_mgr` automatically; no NSH command is required for normal use.

For serial diagnostics use the configured console at **1500000 baud, 8N1**
(`UART3` in `configs/nsh/defconfig`). The physical connector depends on the
board revision and should be checked against its silkscreen.

## Related files

- [`configs/nsh/defconfig`](configs/nsh/defconfig) — final feature and console
  configuration;
- [`src/etc/init.d/rcS`](src/etc/init.d/rcS) — ROMFS startup and service order;
- [`../../tools/apply_vendor_patches.sh`](../../tools/apply_vendor_patches.sh) —
  patch application entry point;
- [`../../docs/编译指南.md`](../../docs/编译指南.md) — Chinese build and acceptance
  checklist.

Files in this directory retain the licenses stated in their individual headers.
