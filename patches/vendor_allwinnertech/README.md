# Vendor patch policy for the ST7796U2 display

The active display implementation is board-local code under
`contest2026_113_veladuxingke/board/r528s3-dshanpi/src/` and uses GPIO software
SPI plus NuttX `lcd_framebuffer`. It does not require a vendor patch.

`r528-dshanpi-ili9341-display.patch` is retained only as history. It repaired
framebuffer/G2D behavior for an assumed ILI9341/DISP2 path, but the supplied
panel documentation identifies the physical controller as a 320x480 ST7796U2
using four-wire SPI. Applying that patch cannot create the missing physical
SPI pixel path and it is no longer part of the default build instructions.

`r528-twi3-frequency-errors.patch` is an active, reproducible dependency for
the J3 environment-sensor bus. It adds R528 TWI3 registration, honors NuttX
per-message 100/200/400 kHz frequencies under the HAL bus lock, removes
per-transfer allocation, and preserves NACK/timeout/busy/arbitration error
classes. `tools/apply_vendor_patches.sh` detects whether this patch is already
applied and applies it to a fresh vendor tree when needed.

`r528-dshanpi-dmic-pins.patch` corrects the board-specific second DMIC data
pin from PB18 to PD18, matching the schematic and R528 mux table.

`r528-dshanpi-led2-default-off.patch` preloads the active-low board LED2 data
latch HIGH before PD22 is switched to output mode. This prevents USER_LED2
from remaining lit between RTOS GPIO initialization and `lan_panel` startup.

`r528-audio-enable-and-test.patch` links the shared tiny-ALSA helpers required
by `sunxi_alsa.c` and makes `audio_test` default to `/dev/audio/pcm1c` capture
and `/dev/audio/pcm0p` playback while allowing explicit device overrides.

The active audio patch order is fixed and non-overlapping:

1. `r528-audio-enable-and-test.patch` — build plumbing and `audio_test` support;
2. `r528-playback-lifecycle.patch` — the complete `sunxi_alsa.c/.h` playback
   lifecycle, including the safe 0–100% volume mapping, final-buffer completion,
   natural drain versus explicit drop, startup unwind, and single-owner
   MQ/task/synchronization cleanup;
3. `r528-dshanpi-codec-playback.patch` — DshanPi HPOUT/AW8010 configuration,
   the hardware-tested raw `HP_GAIN=0`, 160 ms PA startup delay, analog unmute,
   four-phase diagnostics, and the R528 mixed RW/W1C RAMP-register fix.

`SUNXI_RAMP_ANA_CTL` bits 30 (`RAMP_RISE_INT`) and 28 (`RAMP_FALL_INT`) are
write-one-to-clear completion bits, while `RMCEN` and `RDEN` are ordinary
controls. The active Codec patch separates control updates from explicit W1C
acknowledgement, waits for and acknowledges each rise/fall completion, and uses
the Tina Linux non-A sequence: HPOUT starts with `RMCEN=0` then `RDEN=1`, and
stops by ramping `RDEN=0` before disabling the DAC. Lineout controls `RMCEN`
only while `RDEN=0`. The patch does not treat direct-MMIO assignment return
values as errno and does not add a Codec reset or PA pulse. The final image
(NAND SHA-256 `3344c2a297d1829cbf3d9ce1f0c13454b0b7cb3b0553f5551d4bb5aae761fcd5`)
was hardware-verified on 2026-08-12: sensor playback, the due reminder, its
10-minute deferral, and subsequent sensor playback all remained audible.

The following files are historical snapshots and are deliberately excluded
from `tools/apply_vendor_patches.sh`: `r528-safe-playback-volume.patch`,
`r528-playback-completion.patch`, `r528-immediate-playback-stop.patch`, and
`r528-dshanpi-playback-diagnostics.patch`. Their changes are superseded by the
two consolidated active patches above; applying them as well would stack
partially overlapping diffs and is unsupported.

The same script also applies `patches/apps/nxplayer-lifecycle.patch`, which makes
NxPlayer expose synchronized STARTING/PLAYING/STOPPING state so an asynchronous
playback start cannot be mistaken for natural completion.

The same script only reports whether the legacy display patch remains applied;
it never applies that historical patch automatically.
