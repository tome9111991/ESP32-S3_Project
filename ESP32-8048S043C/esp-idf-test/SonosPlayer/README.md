# ESP32-8048S043C SonosPlayer

ESP-IDF v6 project for the **ESP32-8048S043C** (4.3" 800x480 RGB panel
with GT911 touch). The app shows a landscape Sonos-style now-playing panel
and talks to local Sonos players over UPnP/SOAP on port `1400`.

## Current state

- Initializes the RGB panel with double framebuffer + bounce buffer.
- Reads GT911 touch with the known board calibration.
- Connects to WiFi using `main/config_private.h`.
- Uses the Wohnzimmer coordinator by default: `192.168.178.28`
- Polls transport state, title, artist, album, position and volume.
- Sends Play/Pause, Previous, Next, Volume and Rescan commands from the UI.
- Uses a cover placeholder for now; real album-art decoding is still open.

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

The app does not use Sonos cloud APIs. It talks directly to the local
coordinator. The checked Wohnzimmer topology currently looks like this:

- `192.168.178.28` - Sonos Playbase, coordinator, use this IP
- `192.168.178.31` - Sonos One satellite
- `192.168.178.30` - Sonos One satellite
- `192.168.178.29` - Sonos Sub satellite

The room label shown in the UI is read from the Sonos device description
(`<roomName>`), so it follows the name configured in the Sonos app.
