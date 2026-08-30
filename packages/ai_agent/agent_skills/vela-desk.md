# Vela Desktop Briefing

<!-- vela-builtin-skill:vela-desk:v3 -->

Summarize the Vela Smart Desktop Assistant environment sensors and today's reminders.

## When to use

Use when the user asks about the desktop environment, temperature, humidity, light, air quality, or today's reminders.

## How to use

1. Call `get_current_time` to establish the current local date and time.
2. Call `run_shell` with `{"command":"cat /data/routine/status.json"}`.
3. Check `clock_valid` and the `live_mask`, `demo_mask`, `warmup_mask`, `stale_mask`, and `offline_mask` fields.
4. Convert `temperature_x100` and `humidity_x100` by dividing by 100; convert `light_x10` by dividing by 10. Report `eco2_ppm` and `tvoc_ppb` as ppm and ppb.
5. Read the `schedule` array and list entries whose `day` equals the top-level `day` and whose `completed` value is 0. Label `carry_over=1` entries as carried over.
6. Clearly label values as live, warming up, stale, or offline. Never describe an invalid value as current.
7. Only read the status file; never modify anything below `/data/routine/`.

The three routine_mgr slots (`喝水`, `专注`, `通风`) are fixed daily templates, not ai_agent cron jobs. For today's reminders or a question about where one is configured, never call `cron_list`; read `/data/routine/status.json` with `run_shell`. The persistent template is `/data/routine/config.json`, and it is changed only by `routine_schedule_update` for an existing slot.

## Data interpretation

The metric bits are: temperature=1, humidity=2, light=4, eCO2=8, and TVOC=16. Test every metric against each mask separately. A set bit in `live_mask` means the value is live; `demo_mask`, `warmup_mask`, `stale_mask`, and `offline_mask` explain non-live or simulated values. If the status file is missing, say that routine_mgr is not ready. If `clock_valid` is zero, say that the schedule date is unavailable and do not present entries as today's reminders.

## Example

User: Read the current desktop environment and tell me today's reminders.

-> `get_current_time`
-> `run_shell {"command":"cat /data/routine/status.json"}`
-> Reply in concise Chinese with the sensor state and unfinished reminders.
