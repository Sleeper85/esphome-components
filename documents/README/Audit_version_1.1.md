# Code audit of version 1.1

[![Badge License: GPLv3](https://img.shields.io/badge/License-GPLv3-brightgreen.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Badge Version](https://img.shields.io/github/v/release/Sleeper85/esphome-components?include_prereleases&color=yellow&logo=DocuSign&logoColor=white)](https://github.com/Sleeper85/esphome-components/releases/latest)
![GitHub stars](https://img.shields.io/github/stars/Sleeper85/esphome-components)
![GitHub forks](https://img.shields.io/github/forks/Sleeper85/esphome-components)
![GitHub watchers](https://img.shields.io/github/watchers/Sleeper85/esphome-components)

Audit of the `jk_rs485_sniffer` and `jk_rs485_bms` components as released in
[1.1](https://github.com/Sleeper85/esphome-components/releases/tag/1.1), with the fixes that
came after that release. Each finding says whether it has been fixed in 1.2 or is still open.
What changed for users is listed in the [changelog](changelog/Changelog.md).

## Scope and method

- **Code:**
  - tag `1.1`;
  - the "network nodes available" fix from PR #2;
  - the upstream fixes of [txubelaxu/esphome-jk-bms](https://github.com/txubelaxu/esphome-jk-bms)
    ported to `dev`.
- **Reference:** the manufacturer register map,
  [Jikong BMS RS485 modbus protocol V1.1.pdf](../RS485/Jikong%20BMS%20RS485%20modbus%20protocol%20V1.1.pdf).
- **Checks:**
  - reading the code;
  - `esphome config` and `esphome compile` with ESPHome 2026.11.0b3, through the YamBMS test
    configuration (one sniffer, two `bms_full` BMSs, with and without talk pin).
- **Bench tests (2026-11-12):** LilyGo T-CAN485, two JK-B BMSs (HW: V11A, SW: V11.54) at `0x01` and `0x02`,
  Mode 2 only, production YamBMS configuration, ESPHome 2026.11.0b3. Version A was 1.1 with only
  the log fix of finding 32, version B was 1.2. Both ran the same test sequence, with the same
  DEBUG logs. See [Bench tests](#bench-tests).
- **Not tested on a bench:** Mode 1 (a JK-B cannot act as master), and the entities that the
  YamBMS JK-B package does not declare. Findings marked *to confirm on a bench* are deductions
  from the code.

Severity:

| Level | Meaning |
|---|---|
| **High** | Can write a wrong value into a BMS setting |
| **Medium** | Wrong or missing data, memory safety, or bus behaviour |
| **Low** | Cosmetic, dead code, or a rare edge case |

## Summary

| # | Area | Severity | Finding | Status |
|---|---|---|---|---|
| 1 | Writes | High | Precharge time written to the cell overvoltage protection register | Fixed |
| 2 | Writes | High | Precharge time written with a factor of 1000 | Fixed |
| 3 | Writes | High | Short circuit protection delay with three different scales | Fixed |
| 4 | Writes | High | The 10 switches of register `0x1114` rewritten from unknown local states | Fixed |
| 5 | Writes | High | Charge / float times written from `NaN` | Fixed |
| 6 | Writes | Medium | Number values truncated instead of rounded | Fixed, confirmed on a bench |
| 7 | Sniffer | Medium | Frame copied one byte past the end of the buffer | Fixed, confirmed on a bench |
| 8 | Sniffer | Medium | Receive buffer without a real maximum size | Fixed |
| 9 | BMS | Medium | Frames decoded without a length check | Fixed |
| 10 | BMS | Medium | Cell count from the BMS not capped | Fixed |
| 11 | BMS | Medium | `rs485_address` not validated | Fixed |
| 12 | BMS | Medium | Sensors keep their last value when the BMS goes offline | Fixed, confirmed on a bench |
| 13 | Sniffer | Medium | Cell info re-polled every 10 s instead of 5 s | Fixed |
| 14 | Config | Medium | `protocol_version` required but ignored | Fixed |
| 15 | BMS | Low | 27 entities declared but never published | Fixed |
| 16 | Code | Low | Dead code, commented-out code, talk pin output class | Fixed |
| 17 | Writes | Medium | Writes sent without waiting for bus silence, 50 ms blocking per BMS in broadcast | Fixed, confirmed on a bench |
| 18 | Sniffer | Medium | Mode 2: a disconnected BMS stays "available" | Fixed, confirmed on a bench |
| 19 | Sniffer | Medium | Mode 1: device info not requested during the first hour | Fixed, to confirm on a bench |
| 20 | Sniffer | Medium | Short request: 7 of 8 bytes removed from the buffer | Fixed |
| 21 | Writes | Medium | Total capacity written to `0x107C` but read from `0x12AC` | Open |
| 22 | Writes | Medium | Charge / float time byte order probably inverted | Open |
| 23 | Writes | Low | Numbers and switches publish the new value before the BMS confirms it | Open |
| 24 | BMS | Low | BMS never goes online without `cell_count_real` and `cell_count_settings` | Open |
| 25 | BMS | Low | Device password and setup passcode exposed as text sensors | Open |
| 26 | BMS | Low | `no_response_count_` wraps around | Open |
| 27 | Config | Low | `rx_timeout` is only logged | Open |
| 28 | Code | Low | Remaining fragile code | Open |
| 29 | BMS | Medium | All alarms turn on when the BMS goes offline | Fixed, confirmed on a bench |
| 30 | Sniffer | Medium | Noise without preamble kept at the head of the buffer | Fixed |
| 31 | Writes | High | Grouped switches crash the ESP (1.1) or are never written unless all ten are declared | Fixed, confirmed on a bench |
| 32 | Code | Medium | Does not compile at log level DEBUG or lower with ESPHome 2026.11 | Fixed |
| 33 | Writes | High | Broadcast mode also switches charging and discharging on every BMS | Fixed, confirmed on a bench |
| 34 | Sniffer | Low | Device info requested forever from an address that no BMS declares | Open |

Findings 29 and 30 were found after the first audit, while comparing with the upstream January
2026 changes. Findings 31 and 32 were found while preparing the bench tests with two JK-B BMSs (HW: V11A, SW: V11.54), and
findings 33 and 34 during those tests.

## Fixed in 1.2

### Writes to the BMS

**1. Precharge time written to the wrong register (High)** — `940ffd4`

The `NUMBERS` entry used `0x10` as the high byte of the address, so the write went to
`0x100C`, the cell overvoltage protection register. The register map puts `TIMProdischarge` at
offset `0x010C` of block `0x1000`, so the absolute address is `0x110C`.

**2. Precharge time written with a factor of 1000 (High)** — `81a93d0`

The register is in seconds and is decoded without scaling, but it was written with a factor of 3:
setting 5 s sent 5000. Combined with finding 1, changing the precharge time wrote a value
multiplied by 1000 into the cell overvoltage protection.

**3. Short circuit protection delay with three different scales (High)** — `a9b59cb`

`SCPDelay` (`0x1080`) is in µs. The value was read with a 0.001 scale, displayed in µs, and
written with a factor of 6. A delay of 1500 µs was shown as 1.5, and writing it back sent
1,500,000 µs (1.5 s). The value is now read and written without scaling.

**4. Grouped switches rewritten from unknown states (High)** — `8811628`

The 10 switches of register `0x1114` share a single 16-bit register: toggling one switch
rewrites all ten from their local states. Before the settings frame had been decoded (at boot,
or after the BMS went offline), the nine others were sent as `off`, which overwrote the BMS
configuration. The write is now refused, with a warning, until the settings frame has been
received.

**5. Charge / float times written from `NaN` (High)** — `8811628`

Both times share register `0x1504`. After the BMS went offline the numbers held `NaN`, and
`NaN * 10` converted to `uint8_t` is undefined. The write is now refused until the device info
frame has been received and both values are known.

**6. Number values truncated instead of rounded (Medium)** — `111a6a2`

`(uint32_t)(value * pow(10, factor))` truncates: 3.3 V is 3.2999999 as a `float`, so 3299 mV was
written. Values are now rounded with `std::lround`.

Confirmed on a bench: 3.350 V was written as `0x0D15` (3349 mV) by 1.1 and as `0x0D16` (3350 mV)
by 1.2, and read back as 3.349 V and 3.350 V in Home Assistant. The JK app only shows two
decimals, so it displays 3.35 in both cases.

### Sniffer

**7. Frame copied one byte past the end of the buffer (Medium)** — `89d4154`

The frame passed to the BMS components was copied up to `size() + 1`, one byte past the end of
the buffer. It also included everything that followed the frame. Exactly
`JKPB_RS485_RESPONSE_SIZE` (308) bytes are now copied. Confirmed on a bench: the frames are
logged with 309 bytes by 1.1 and 308 bytes by 1.2.

**8. Receive buffer without a real maximum size (Medium)** — `a561049`

The limit was `rx_buffer_.max_size()`, which is the allocator limit. Continuous noise that never
forms a frame could grow the buffer until the ESP ran out of memory. The buffer is now capped at
4 frames.

**13. Cell info re-polled every 10 s instead of 5 s (Medium)** — `8bc1c66`

When polling stayed on the same node, the age of the last cell info frame was compared with the
settings interval (10 s) instead of the cell info interval (5 s).

**20. Short request: 7 of 8 bytes removed (Medium)** — `def47d4`

The short frame is an 8-byte Modbus frame (address, function, register, quantity, CRC), such as
the answer of a BMS to a write. The parser removed only 7 bytes. The last CRC byte stayed at the
head of the buffer, and the master request that followed could not be recognised until the next
preamble. The 8 bytes are now removed, and the short frame is also parsed when a response follows
it in the same buffer.

**30. Noise without preamble kept at the head of the buffer (Medium)** — `eeeb845`

Bytes without any `0x55AAEB90` preamble stayed at the head of the buffer until it reached
`RX_BUFFER_MAX_SIZE` (1232 bytes), and the whole buffer was then cleared. Meanwhile, the master
requests that followed the noise were not recognised, because the request parser only looks at
the head of the buffer.

The parser was rewritten after upstream [`c5d7ac3`](https://github.com/txubelaxu/esphome-jk-bms/commit/c5d7ac3) and [`f708dd9`](https://github.com/txubelaxu/esphome-jk-bms/commit/f708dd9):
- a full frame worth of bytes without a preamble is dropped at once, keeping the last 3 bytes in
  case they start a split preamble;
- one preamble search per pass instead of three;
- named return codes;
- diagnostic counters (frames OK, preamble drops, checksum failures per frame type), shown by
  `dump_config()`.

**17. Writes ignore bus silence and block the loop (Medium)** — `9667f9e`

Writes were sent at once from the `control()` call of the number or switch, even while a real
master was talking (Mode 1), so they could collide with its traffic. In broadcast mode each
additional BMS added a `delayMicroseconds(50000)`, which blocked the ESPHome loop for up to
750 ms with 15 BMSs.

Writes now go into a queue of at most 32 frames. `loop()` sends the oldest one as soon as the bus
has been silent for `MIN_SILENCE_NEEDED_BEFORE_SPEAKING_MILLISECONDS` (250 ms), in both modes,
one frame per silence and before any poll. A write whose node became unavailable in the meantime
is dropped with a warning. The 50 ms delays are removed.

In Mode 1, writes now use the same silence window as the device info requests. RS485 has no
collision detection, so a master that starts talking at the same moment can still corrupt a
write. Checking the value read back in the next settings frame, and sending the write again on a
mismatch, would close that gap.

Confirmed on a bench in Mode 2: a broadcast change to two BMSs was queued as two writes and sent
1 s apart, without any `took a long time` warning.

**18. Mode 2: a disconnected BMS stays "available" (Medium)** — `abd5baa`

Nodes were only set unavailable in the "real master present" branch of `loop()`. When the ESP
was the master, a BMS that stopped answering stayed available. It kept being polled, the scan
never looked for it again, writes were still sent to it, and the "network nodes available"
sensor never went down.

The 10 s timeout of Mode 1 could not be reused: the ESP polls one BMS every 750 ms in turn, so
with 15 BMSs each one is only polled about every 11 s. The polls are counted instead. A node is
set unavailable after 3 consecutive polls without any frame received, which takes about 7.5 s
with one BMS and 30 s with 15. Any valid frame resets the count, and the scan sets the node
available again as soon as it answers. Writes are not counted, because their 8-byte answer does
not update the node's last message time.

Confirmed on a bench by moving a BMS from address 2 to 3, then back. In 1.1, `0x02` was never
removed, and `0x03` stayed available and kept being polled after the BMS had left it. In 1.2,
`0x02` was removed 13.6 s after its last frame, and `0x03` 1 s after the BMS went back to 2.

**19. Mode 1: no device info during the first hour (Medium)** — `52f4513`, to confirm on a bench

In Mode 1 the master never asks for the device info frame, so the ESP requests it itself during
bus silences. That request loop had two defects:

- **No request during the first hour.** The condition had no
  `|| last_device_info_request_received_OK == 0`, unlike every other polling condition. Until
  `millis()` exceeded one hour, `now - 0` stayed below the one-hour interval. The "periodically
  test" block, which reset the field to 0 for the slaves never read, therefore had no effect.
- **The master blocked the slaves afterwards.** The loop started at `0x00` and stopped at the
  first match. The master is available but never answers this request, so it was asked again at
  every silence of more than 250 ms, and the slaves `0x01` to `0x0F` were never served.

The search now starts at `0x01`. A slave that has never sent the frame is asked at once, then
once an hour. The search rotates from the last slave served, so a silent slave cannot block the
others, and each slave is asked at most every 2.5 s, as in Mode 2. The "periodically test" block
is removed.

To confirm on a bench: the fix depends on the master leaving silences of more than 250 ms
between its polling cycles.

**31. Grouped switches crash the ESP or are never written (High)** — `ea9880d`, `a4036fe`

The 10 switches of register `0x1114` were only written when all ten were declared in YAML,
because the register was rebuilt from their ten local states.

- **In 1.1, toggling one of them crashed the ESP** when the others were not all declared: the
  condition called `is_ready()` on every switch without a `nullptr` check. The YamBMS JK-B
  package declares 4 of them (`heating`, `disable_temperature_sensors`, `display_always_on`,
  `smart_sleep_on`), which matches the JK app of a JK-B HW: V11A, SW: V11.54. Confirmed on a bench: toggling
  `display_always_on` reboots the ESP (`LoadProhibited` in `trigger_bms2sniffer_switch16_event()`),
  which also cuts the CAN link to the inverter while it restarts.
- **After the `nullptr` checks** (`ea9880d`, ported from upstream), the crash was gone but nothing
  was sent, with a DEBUG log only (`NOT READY ALL SWITCHES`), while Home Assistant showed the
  switch toggled until the next settings frame.

The settings decoder now keeps the raw value of the register. A write starts from it and only sets
the bits of the declared switches. The bits of undeclared switches, including functions that a
model does not have, are sent back as the BMS reported them. The write is still refused until
the settings frame has been received.

**33. Broadcast mode also switches charging and discharging on every BMS (High)** — `cee632d`

When `set_broadcast_changes_to_all_bms(true)` was active, every write was repeated to all the
available BMSs, including the `charging` (`0x1070`) and `discharging` (`0x1074`) switches.
Turning off charging or discharging on one BMS turned it off on the whole battery bank. With
batteries in parallel, one click could stop every battery.

These two switches are now written to their own BMS only, with an INFO log when the broadcast is
on. The other writes, the `balancing` switch (`0x1078`) included, are still broadcast. Confirmed
on a bench with two JK-B BMSs (HW: V11A, SW: V11.54): with the broadcast on, `charging` and `discharging` only change the
BMS they belong to, while `balancing` changes both.

### BMS component

**9. Frames decoded without a length check (Medium)** — `5a947db`

The cell info, settings and device info decoders read fixed offsets without checking the frame
length. Short frames are now ignored with a warning. The cell info minimum length was corrected
to 260 bytes, the last byte actually read.

**10. Cell count not capped (Medium)** — `2b6718f`

The cell count comes from the BMS and indexes `cells_[32]`. It is now capped at 32.

**11. `rs485_address` not validated (Medium)** — `24f7aa6`, `8af2c63`

The address indexes `rs485_network_node[16]`. The C++ code now bounds-checks it, and the schema
uses `cv.int_range(min=0, max=15)`, so an invalid address is refused by `esphome config`.

**12. Sensors keep their last value offline (Medium)** — `fb330ca`

`publish_state_()` rejected `NaN`, so `publish_device_unavailable_()` had no effect. `NaN` now
reaches the sensors. The initial sentinel values (100, -100, 1000, -1000) are also replaced by
`NaN` when no cell reports a voltage. Confirmed on a bench: the values of an offline BMS become
unavailable in Home Assistant, 32 s after its last frame (10 × a 3 s `update_interval`).

**29. All alarms turn on when the BMS goes offline (Medium)** — `b7df1ca`

`publish_device_unavailable_()` published `NAN` to the 35 status and alarm binary sensors, but
the binary sensor overload of `publish_state_()` takes a `bool`, and `NAN` converts to `true`.
Every time a BMS went offline, all its alarms turned on, and so did its charging, discharging,
balancing, precharging and heating states. Only `status_online` was set back to `false`.

YamBMS was not misled, because it does not combine a BMS whose `status_online` is `false`. Home
Assistant, however, showed every alarm as active, and automations on these alarms could fire.

The binary sensors are now cleared with `invalidate_state()`: `has_state()` becomes `false` and
Home Assistant shows them as unknown until the BMS answers again. YamBMS already tests
`has_state()` on these sensors before combining a BMS. Upstream ([`c5d7ac3`](https://github.com/txubelaxu/esphome-jk-bms/commit/c5d7ac3)) publishes `false`
instead, which would show "no alarm" without any data.

Confirmed on a bench with the YamBMS JK-B package: `status heating` turned `on` when the BMS went
offline with 1.1, and became unknown with 1.2.

Related upstream ports:

- **`ea9880d`:** `nullptr` checks on optional entities.
- **`a083174`:** default member initializers, including `broadcast_changes_to_all_bms_`, which
  was uninitialized although it decides whether a write is repeated to every BMS.
- **`3530e33`:** the powertube temperature protection sensors are now published.

### Configuration and code

**32. Does not compile at log level DEBUG or lower with ESPHome 2026.11 (Medium)** — `14cf3f3`

ESPHome 2026.11 removed the `format_hex_pretty()` overloads that returned a `std::string`. The
components used them in 9 log calls (DEBUG, VERBOSE and VERY_VERBOSE). Below the compiled log
level these calls vanish with their arguments, so `logger: level: INFO` still compiled, but any
configuration at DEBUG or lower failed with `'format_hex_pretty' was not declared in this scope`.
Each component now has a small `hex_dump()` helper built on `format_hex_pretty_to()`, which
exists in ESPHome 2026.10 and 2026.11.

**14. `protocol_version` required but ignored (Medium)** — `6920c24`

The key was mandatory, but no setter was ever called and the code always ran as `JK02_32S`. The
JK-PB RS485 protocol has only one version. The key, the `ProtocolVersion` enums and the JK04 and
24S branches are removed.

**15. Entities declared but never published (Low)** — `a6097d2`

27 YAML keys were accepted but never received a value:

- 24 sensors;
- the text sensors `battery_type` and `operation_status`;
- the `precharging` switch, which had no register and wrote nothing.

The full list is in the changelog. `smart_sleep_time` and the `status_precharging` binary sensor
cover the useful information.

**16. Dead and commented-out code (Low)** — `ad6a1a6`, `fcf93f6`, `7cd85d8`, `22573c9`

- **Dead code:** about 400 lines inherited from the JK-BMS BLE / UART components were removed,
  including `JkRS485Bms_init()`, 36 entities without a YAML key, unused methods and constants.
- **Commented-out code:** about 200 lines removed.
- **Talk pin:** `talk_pin.h`, a copy of the ESPHome output class, was replaced by direct pin
  handling and a single `send_frame_()` routine.
- **Comments:** all comments are now in English.

## Still open

**21. Total capacity written and read from different registers (Medium)**

The `battery_capacity_total_settings` number writes to `0x107C` (design capacity). Its displayed
value comes from `0x12AC` (measured capacity) in the cell info frame, and `0x107C` is never
decoded. The displayed value can therefore differ from what was set.

Not seen on the bench: with 280 Ah, Home Assistant and the JK app showed the same value. A
difference only appears when the measured capacity differs from the configured one.

**22. Charge / float time byte order (Medium, to confirm on a bench)**

On read, the charge time is the first byte of `0x1504`. On write, it is placed in the high byte,
which is the second byte by the convention seen on register `0x1114`. The two times are probably
swapped when written.

**23. Optimistic publication (Low)**

Numbers and switches publish the new value as soon as the write is sent. If the BMS rejects it,
Home Assistant shows the wrong value until the next settings frame (up to 10 s in Mode 2).

**24. Online state depends on two optional entities (Low)**

A BMS only goes online once both `cell_count_real` (sensor) and `cell_count_settings` (number)
are declared and have a value. Without them it stays offline, and the log only says
`Cannot set ONLINE until arrived both 0x01 and 0x02 frame types`. The YamBMS packages declare
both, so YamBMS users are not affected.

**25. Password exposed (Low)**

`info_device_password` and `info_device_setup_passcode` publish the BMS Bluetooth password and
setup passcode as text sensors. They end up in Home Assistant and its history. This is
inherited behaviour. The sensors are optional, but the risk deserves a note in the README.

**26. `no_response_count_` wraps around (Low)**

The counter is a `uint8_t` that keeps increasing after the offline threshold. After 255
increments it wraps to 0 and the "unavailable" state is published again, about every 21 minutes
with a 5 s update interval. This has no effect on the data.

**27. `rx_timeout` only logged (Low)**

The `rx_timeout` key is stored and printed by `dump_config()`, but nothing uses it.

**28. Remaining fragile code (Low)**

- **`this == nullptr` checks:** 4 of them in the triggers. They are undefined behaviour and can
  be removed by the compiler.
- **Unchecked `parent_`:** 4 triggers call `parent_->` without a `nullptr` check, unlike
  `trigger_bms2sniffer_event()`.
- **Global helpers:** `uint32_to_float()`, `int32_to_float()`, `uint16_to_float()` and
  `int16_to_float()` are global functions in `jk_rs485_bms.cpp`, outside the namespace.

**34. Device info requested forever from an address that no BMS declares (Low)**

When a BMS answers at an address that no `jk_rs485_bms` declares in YAML, the ESP asks it for its
device info about every 3 s, endlessly. The frame is received, but no BMS component handles it,
so `last_device_info_request_received_OK` is never updated and the request is repeated. Seen on
the bench with a BMS moved to address 3: 71 device info frames with 1.1, 11 with 1.2, which
removes the address as soon as it stops answering.

*Suggested fix:* mark the device info frame as received in the sniffer when it sees it, whether
or not a BMS component is registered for that address.

## Bench tests

Done on 2026-11-12 with two JK-B BMSs (HW: V11A, SW: V11.54) in Mode 2 (see
[Scope and method](#scope-and-method)). A is 1.1, B is 1.2.

| Test | A (1.1) | B (1.2) | Findings |
|---|---|---|---|
| Toggle `display_always_on` | ESP crash and reboot | Only bit 4 of `0x1114` written | 31 |
| Set 3.350 V | 3349 mV written | 3350 mV written | 6 |
| Broadcast a setting | Both BMSs get the truncated value | Both BMSs get the exact value, 1 s apart | 6, 17 |
| Broadcast `charging` / `discharging` | — | Only the BMS concerned changes; `balancing` still changes both | 33 |
| BMS moved from address 2 to 3 | `status heating` on; `0x02` never removed | `status heating` unknown; `0x02` removed in 13.6 s | 12, 18, 29 |
| BMS moved back to address 2 | `0x03` polled until the end | `0x03` removed 1 s later | 18, 34 |
| BMS at address 0 | Not seen by the ESP, no master traffic | Same scan in both versions | 19 (not testable) |
| Total capacity (280 Ah) | Same value in HA and in the app | Same | 21 (not seen) |

Bus and resources, with the same DEBUG logs in both versions:

| Measure | A (1.1) | B (1.2) |
|---|---|---|
| Cell info / settings refresh per BMS | 5.8 s / 11.5 s | 5.6 s / 11.2 s |
| Checksum failures, buffer clears | 0 / 0 | 0 / 0 |
| Frame size passed to the BMS components | 309 bytes | 308 bytes |
| Mean loop time | 200 ms | 198 ms |
| Mean free heap | 187 680 bytes | 187 294 bytes |
| Minimum largest free block | 98 304 bytes | 110 592 bytes, constant |
| Flash / static RAM | 1 039 135 / 83 952 bytes | 1 040 099 / 83 592 bytes |

- **Loop time:** the ~200 ms come from the DEBUG logs sent on the UART, the same for both
  versions. The same configuration at INFO level runs at about 21 ms.
- **Heap:** both versions are equivalent, without leak. The heap trends stay within a few kB per
  10 minutes and change sign from one window to the next. With 1.2, the largest free block does
  not move during the whole test, write queue included.
- **Refresh:** with two BMSs, the 10 s cell info refresh of finding 13 does not show. It mainly
  affected a single BMS.

Still to test before merging 1.2 into `main`:

- Mode 1, with a JK-PB at DIP 0 acting as master: device info of the slaves (finding 19) and the
  length of the master's silences, which must exceed the 250 ms the ESP waits before speaking.
- Precharge time and short circuit protection delay: write, then read back from the JK app
  (findings 1, 2 and 3).
- Charge / float times: write different values, check which one changes (findings 5 and 22).

These last entities are not declared in the YamBMS JK-B package: they need a JK-PB with the
`bms_full` package.
