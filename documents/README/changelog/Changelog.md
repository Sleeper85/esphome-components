# JK RS485 components changelog

[![Badge License: GPLv3](https://img.shields.io/badge/License-GPLv3-brightgreen.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Badge Version](https://img.shields.io/github/v/release/Sleeper85/esphome-components?include_prereleases&color=yellow&logo=DocuSign&logoColor=white)](https://github.com/Sleeper85/esphome-components/releases/latest)
![GitHub stars](https://img.shields.io/github/stars/Sleeper85/esphome-components)
![GitHub forks](https://img.shields.io/github/forks/Sleeper85/esphome-components)
![GitHub watchers](https://img.shields.io/github/watchers/Sleeper85/esphome-components)

Changes to the `jk_rs485_sniffer` and `jk_rs485_bms` ESPHome components. Each file carries its own version in its header ; the release number below is the version of the components as a whole.

* JK RS485 1.2 :
  * New documentation :
    * Jikong register map : [Jikong BMS RS485 modbus protocol V1.1.pdf](../../RS485/Jikong%20BMS%20RS485%20modbus%20protocol%20V1.1.pdf)
    * [Code audit of version 1.1](../Audit_version_1.1.md) : every defect found in 1.1, how it was fixed in 1.2 (with the commit), what was confirmed on a bench, and what is still open

  * **Breaking change** - Removed the `protocol_version` key of `jk_rs485_sniffer` : the JK-PB RS485 protocol has only one version. Remove it from your YAML, otherwise ESPHome reports `[protocol_version] is an invalid option`
  * **Breaking change** - Removed the entities that were declared but never published by the BMS : remove them from your YAML, otherwise ESPHome reports them as invalid options
    * switch : `precharging`
    * text sensors : `battery_type`, `operation_status`
    * sensors : `actual_battery_capacity`, `alarm_low_volume`, `balancing_opening_pressure_difference`, `battery_capacity_remaining_derived`, `battery_strings`, `cell_pressure_difference_protection`, the `charging_` / `discharging_` `high` / `low_temperature_protection` and `..._recovery` sensors, `current_calibration`, `device_address`, `manufacturing_date`, `max_discharging_current`, `operation_mode_bitmask`, `protocol_version`, `sleep_wait_time`, `start_current_calibration`, `temperature_sensor_temperature_*`, `temperature_sensors`
    * the `current_calibration` and `max_discharging_current` numbers remain
  * **Breaking change** - `rs485_address` is now validated in the `0x00` to `0x0F` range : a value above `0x0F` is refused by `esphome config` instead of being silently ignored at runtime

  * Simplified the talk pin handling : the pin is configured before its first write and every frame is sent by a single routine ; `talk_pin` stays optional, without it the converter must switch direction by itself (automatic direction control)
  * Precharging time (`precharging_time_from_discharge`) is now written to register `0x110C`, in seconds ; it was written to the wrong register and with the wrong scale
  * Short circuit protection delay is now read and written in µs, as in the Jikong register map ; it was shown divided by 1000
  * Numbers are now rounded instead of truncated before being written : `3.350` V was written as `3.349` V (confirmed on a bench)
  * The charge / float times that share register `0x1504` are only written once the device info frame has been read : before that, changing one of them could reset the other to `0`
  * Sensors now publish `NaN` when the BMS becomes unavailable, instead of keeping their last value
  * Fixed the compilation with ESPHome 2026.11 at log level DEBUG or lower : `format_hex_pretty()`, removed from ESPHome, is replaced. The `INFO` level was not affected
  * Fixed the 10 switches that share register `0x1114` (heating, temperature sensors, GPS heartbeat, port selection, display always on, special charger, smart sleep, PCL module, timed stored data, float mode) : with the YamBMS JK-B package, which declares 4 of them, toggling one **crashed and rebooted the ESP** in 1.1 (confirmed on a bench). Only the toggled switch is now changed, the others keep the value read from the BMS, the switches a BMS model does not offer no longer need to be declared, and nothing is written before the settings frame has been read
  * Fixed all alarms turning on when a BMS goes offline : the status and alarm binary sensors received `NaN`, which a binary sensor reads as `on` ; they now become unknown until the BMS answers again
  * The powertube temperature protection sensors are now published : they mirror the MOS overtemperature protection settings
  * Fixed the "network nodes available" text sensor, which published a trailing NUL byte and broke the Home Assistant recorder on PostgreSQL (PR #2)
  * Robustness fixes ported from [txubelaxu/esphome-jk-bms](https://github.com/txubelaxu/esphome-jk-bms) :
    * frames shorter than expected are ignored instead of being decoded past their end
    * the sniffer forwards exactly one frame to the BMS components
    * the receive buffer has a real maximum size
    * the cell count is capped at 32
    * addresses are bounds-checked before indexing the node table
    * optional entities are checked before use, and all members have default values
    * re-polling the same node uses the cell info interval
    * the frame parser was rewritten : bytes without a frame preamble are dropped at once instead of hiding the master requests that follow them, and `dump_config()` shows diagnostic counters (frames OK, preamble drops, checksum failures)
  * Fixed the 8-byte short frame (answer of a BMS to a write), which was removed from the buffer one byte short and left its last CRC byte in front of the next frame
  * Broadcast mode no longer applies to the `charging` and `discharging` switches : with batteries in parallel, turning off charging or discharging on one BMS turned it off on the whole bank. The other settings, `balancing` included, are still broadcast
  * Writes to the BMS (numbers, switches) are now queued and sent during bus silences, one frame at a time : in Mode 1 they no longer interrupt the master's traffic, and broadcasting a change to all BMSs no longer blocks the ESP for up to 750 ms
  * Mode 1 : the device info of the slaves (name, versions, serial number, charge / float times) is now requested as soon as they appear, then once an hour ; before, nothing was requested during the first hour, and afterwards the requests went to the master BMS, which never answers them
  * Mode 2 : a BMS that stops answering is now set unavailable after 3 polls without answer, and found again by the network scan when it comes back ; before, it stayed available forever and kept being polled
  * Code cleanup : removed the dead code inherited from the JK-BMS BLE / UART components, the commented-out code and the protocol version handling ; all code comments are now in English

* JK RS485 1.1 :
  * First tagged release, it marks the state of the 1.1.x line before the 1.2 fixes : `github://Sleeper85/esphome-components@1.1`
  * Fixed the 32-bit `errors_bitmask`
  * Fixed a `device_class` warning
  * Fixed the `*.*_SCHEMA` deprecations for recent ESPHome versions
  * Raised the maximum of the total battery capacity setting (`battery_capacity_total_settings`) to `3000`
  * Replaced `%u` with `%lu` in log format strings
  * Known issue : the "network nodes available" text sensor publishes a trailing NUL byte, which breaks the Home Assistant recorder on PostgreSQL, fixed in 1.2
  * Initial import of the `jk_rs485_bms` and `jk_rs485_sniffer` components from [txubelaxu/esphome-jk-bms](https://github.com/txubelaxu/esphome-jk-bms) (January 2025)
