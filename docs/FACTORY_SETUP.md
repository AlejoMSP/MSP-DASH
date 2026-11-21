# Factory setup plan

Built in hub 62 / build 226 (see the notes under each step for what differs from the first plan). Goal: any number of screens (1 to 3), with or without a hub, sets itself up, shows only the
options that exist, can be backed up, and can be put back to new without flashing. Built in this order.

## 1. Automatic roles and the hub flag

- The hub marks its packets as coming from a hub (a flag older screens ignore). Today a screen cannot tell a hub's
  data from a main screen's.
- First start, a screen picks its role:
  - hears a hub: **remote** (full system);
  - hears another screen sending data: **remote** (screens only, 2 or 3 screens);
  - hears neither, but sees CAN traffic on its own transceiver (GPIO 43 / 44): **main**.
- Still changeable by hand on Board settings.
- The current car (with a hub) keeps working as it is: all three screens remote.

## 2. First-boot setup

- A new (or reset) screen starts its own Wi-Fi network with a setup page.
- The buyer picks its position (Left, Center, Right, or Single) and, optionally, their home Wi-Fi.
- Removes the home network name and password built into the firmware today (wifi_config.h).
- Show only what exists:
  - the gauge editor lists only signals with a source (the ECU, the hub if there is one, the screen's own sensors);
  - hub-only settings (shift light, buttons, outputs, ...) are hidden when there is no hub.

## 3. Factory reset without flashing

- **BOOT button sequence:** in the first 15 s after power-up, hold BOOT for 5 s; the screen asks "Factory reset? Let
  go, then press BOOT again", then counts down 10 s. A second press wipes; no press boots normally. Only right after
  power-up, so it can never happen while driving. (Built differently from the first plan: BOOT is GPIO 0, and holding
  it WHILE powering on starts the chip's USB loader instead of the firmware.) The hub: the same with its own BOOT
  button, the screens and the serial port asking.
- Also: a Factory reset button (with a confirm box) on Board settings, and `factory` in the USB serial monitor.
- Wipes: name, role, network settings, the hub's Wi-Fi details, pages, gauges, colours, logging channels, display
  settings. Keeps: the firmware and the SD card's log files. Restarts into the first-boot setup.
- The hub gets the same on its own button, so a whole system can go back to new.

## 4. Settings backup

- Hub **Backup** page: Export downloads one file with the hub's settings (buttons and ladder voltages, inputs,
  outputs, pin moves, OLED, shift light, ECU maps, Wi-Fi, brightness, clock, accelerometer zero, custom sensors) and
  each screen's (pages, gauges, colours, warnings, name, logging channels, display). The page collects the screens'
  parts directly from them.
- Restore: shows what the file holds (boards, builds, date); tick all, hub only or one screen. Each board checks its
  part before saving and refuses a broken one. A screen's part can go onto a different screen (copy a layout).
- Each screen's Board settings page also gets its own Export / Restore, for systems without a hub.
- Wi-Fi passwords left out unless "include passwords" is ticked.

## As built

- Hub flag: the top bit of LinkPacket.count (LINK_COUNT_FROM_HUB). Older screens read min(count, SIG_COUNT): fine.
- Automatic role: no "remote" key in NVS "link" = automatic. Listens to CAN in listen-only mode (sends nothing) and to
  ESP-NOW; switches to main without a restart (autoRoleStep in the .ino, espnow_link_become_main). Board settings has
  Automatic / Main / Remote.
- First-boot setup: no name saved = setup mode. Network "MSP-DASH-Setup-XXXX", password mspdash123, page
  http://192.168.4.1/setup; the screen pops up how to reach it. Home Wi-Fi in NVS "wifi"; wifi_config.h is now only an
  optional fallback (the firmware builds without it).
- Only what exists: every signal that has ever had data is kept (NVS "seen"); the gauge editor lists only those,
  with "Show signals with no source yet" to see all. Hub-only settings live only on the hub's pages.
- Backup: settings_backup.h (same file on hub and screens). Lines of NVS entries in each board's own namespaces, plus
  the screen's cfg_display.json, all hex. Hub Backup page: Export everything (hub + online screens) to one .json;
  Restore per part, screens first, the hub last. Screen Board settings: Export / Restore for that screen.
- Factory reset: the whole NVS erased (and the screen's gauge file); the screen restarts into its setup.
- Restore and factory reset on the screens need the X-MSP header (like /reboot). The hub's routes have no such check
  (none of its routes do): anyone on the car's network can reach them.
