# Factory setup plan

Planned, not built yet. Goal: any number of screens (1 to 3), with or without a hub, sets itself up, shows only the
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

- **BOOT button sequence:** hold BOOT while powering on; after 5 s the screen shows "Factory reset? Press BOOT again
  to confirm" with a 10 s countdown. A second press wipes; no press boots normally. Only at power-up, so it can never
  happen while driving.
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
