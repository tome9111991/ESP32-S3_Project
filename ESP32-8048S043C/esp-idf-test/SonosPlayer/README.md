# ESP32-8048S043C Sonos Controller

ESP-IDF v6 project for the **ESP32-8048S043C** (4.3" 800x480 RGB panel
with GT911 touch). The app shows a landscape now-playing panel and talks to
local Sonos speakers over UPnP/SOAP on port `1400`.

This is an unofficial DIY controller/display project. It is not affiliated
with, endorsed by, certified by, or sponsored by Sonos. The name Sonos is used
only to describe compatibility with Sonos speakers on the local network. This
project does not include Sonos logos, certification badges, music-service
credentials, or bundled album artwork.

The firmware is intended to control speakers that you own and have already set
up in your own network. It does not play audio on non-Sonos hardware, bypass
music-service access controls, record streams, or redistribute music/content.

## Current state

- Initializes the RGB panel with double framebuffer + bounce buffer.
- Reads GT911 touch with the known board calibration.
- Connects to WiFi using `main/config_private.h`.
- Uses the Wohnzimmer coordinator by default: `192.168.178.28`
- Polls transport state, title, artist, album, position, volume and cover URL.
- Sends Play/Pause, Previous, Next, Volume and Rescan commands from the UI.
- Downloads and decodes the current album art in RAM for local display.

## Private config

`main/config_private.example.h` is the template for `main/config_private.h`.
The private file holds WiFi data and is ignored by Git.

```c
#define WIFI_SSID "YOUR_WIFI_NAME"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

#define SONOS_PLAYER_IPS "192.168.178.28"
```

## Module layout

```text
main/
├── main.c                 # startup / wiring
├── board_8048s043c.c/.h   # RGB panel, GT911, LVGL port
├── wifi_sta.c/.h          # WiFi STA connection
├── sonos_client.c/.h      # Sonos SOAP polling + commands
├── ui_sonos.c/.h          # LVGL Sonos-style landscape UI
└── config_private.example.h
```

## Notes

The app does not use the official Sonos cloud Control API. It talks directly to
the local coordinator using local network endpoints. That keeps the device
simple for DIY use, but it also means this is not an officially supported Sonos
integration and the behavior may change with future Sonos firmware updates.

The checked Wohnzimmer topology currently looks like this:

- `192.168.178.28` - Sonos Playbase, coordinator, use this IP
- `192.168.178.31` - Sonos One satellite
- `192.168.178.30` - Sonos One satellite
- `192.168.178.29` - Sonos Sub satellite

The room label shown in the UI is read from the Sonos device description
(`<roomName>`), so it follows the name configured in the Sonos app.

Album art is downloaded from the URL reported by the speaker, decoded into an
RGB565 buffer, shown locally, and freed again when it is replaced. Do not commit
downloaded artwork, private credentials, or user-specific music-service data to
this repository.

## Publishing and Monetization

If you publish a build guide, hardware shopping list, video, or project page for
this firmware, describe it as an unofficial DIY controller/display for Sonos
speakers. Do not use wording such as "official", "certified", "Works with
Sonos", or similar certification language unless you have written permission or
certification from Sonos.

Affiliate links and donation links are fine for project support when they are
clearly disclosed near the links. Example disclosure:

```text
Some hardware links are affiliate links. If you buy through them, I may receive
a small commission at no extra cost to you.
```
