# Guition JC4827W543 PowerDashboard

ESP-IDF v6 Dashboard für einen **Shelly Plug (Gen2/Plus)**, das per LAN/HTTP
alle 2 Sekunden den Live-Status abfragt und auf dem 480×272 NV3041A-Display
darstellt.

## Anzeige

- Aktuelle Leistung (W oder kW automatisch)
- Spannung (V) und Strom (A)
- Verbrauch **heute** in kWh und € (Reset bei Tageswechsel via SNTP-Zeit)
- Verbrauch **seit Boot** in kWh und €
- Plug-Lebenszähler in kWh
- WiFi-Status, IP des Plugs, lokale Uhrzeit
- Tarif-Anzeige im Footer

Kosten = `(Wh_delta / 1000) * (TARIFF_CT_PER_KWH / 100)`.

## Konfiguration

Alle Einstellungen liegen im NVS und werden zur Laufzeit per Web-UI
gepflegt. Compile-Time-Defaults aus `config_private.h` / `config.h` greifen,
solange im NVS nichts steht.

**Lookup-Reihenfolge pro Wert:**
1. NVS-Eintrag (über Web-UI gesetzt)
2. Sonst: Makro aus `config_private.h` (falls vorhanden)
3. Sonst: Default in `config.h`

### Mit vorhandenen WLAN-Daten (compile-time)

`config_private.h` mit `WIFI_SSID` + `WIFI_PASSWORD` befüllen (Vorlage:
`config_private.example.h`). Beim Flash bootet das Gerät direkt im STA-Modus
in dieses WLAN — **kein QR-Setup nötig**. Andere Werte (Shelly-IP, Tarif)
können dort ebenfalls überschrieben werden.

### Ohne Compile-Time-Creds (QR-Provisioning)

Ist `WIFI_SSID` leer (Datei fehlt oder leerer String):

1. Beim Boot startet ein SoftAP (`PowerDash-XXXX` mit Random-Passwort).
2. Display zeigt **einen** WLAN-QR-Code. Scannen → Phone verbindet sich.
3. Ein Captive-DNS-Hijack auf dem Gerät beantwortet jede DNS-Anfrage mit der
   AP-IP. Der Connectivity-Check des Smartphones schlägt deshalb fehl, das OS
   blendet automatisch die Setup-Seite ein (Android: "Bei Netzwerk anmelden",
   iOS: Captive-Portal-Popup).
4. Speichern + Neustart.

### Späteres Bearbeiten

Der HTTP-Server läuft auch im Normalbetrieb. Im LAN auf die STA-IP des
Geräts gehen (`http://<device-ip>/`) — gleiche Setup-Seite. Werkreset
über den Button löscht alle NVS-Einträge.

### Neues Setting hinzufügen

Eine Zeile in `main/settings_schema.h` ergänzen — Web-UI und Storage ziehen
es automatisch nach. Typen: `SETTING_STR`, `SETTING_INT`, `SETTING_FLT`.

### Fallback

Verbindet sich das Gerät nach 30 s nicht mit dem gespeicherten WLAN, wird
die SSID geleert und neugestartet → automatisch zurück in den AP-Modus.

## Shelly-Endpoint

```text
GET http://<SHELLY_IP>/rpc/Switch.GetStatus?id=0
```

Verwendete Felder: `apower`, `voltage`, `current`, `aenergy.total`,
`temperature.tC`. Wenn der Plug nur Gen1 spricht, muss `fetch_shelly_status()`
in `main/main.cpp` auf `/meter/0` umgestellt werden.

## Icons

Quell-SVGs liegen in `assets/icons/`, die generierten LVGL-C-Arrays in
`main/ui_assets/`. Neu rendern nach SVG-Änderung:

```powershell
python tools\svg2lvgl.py `
  Guition_JC4827W543\ESP-IDF\PowerDashboard\assets\icons `
  -o   Guition_JC4827W543\ESP-IDF\PowerDashboard\main\ui_assets `
  --prefix icon_ `
  --header Guition_JC4827W543\ESP-IDF\PowerDashboard\main\ui_assets\ui_assets.h
```

CMake globt alle `ui_assets/*.c` in den Build. **Nach Anlegen einer neuen
Icon-Datei einmal `idf.py reconfigure` aufrufen**, damit CMake den Glob neu
auswertet (ESP-IDF unterstützt `CONFIGURE_DEPENDS` nicht).

## Build

```bash
idf.py set-target esp32s3
idf.py build flash monitor
```

Beim ersten Build lädt der Component-Manager LVGL automatisch.

## Hinweise

- `today_start_wh` lebt nur im RAM – nach Reboot fängt die Tageszählung beim
  aktuellen Plug-Total wieder an. Plug-Gesamtcounter bleibt persistent.
- `session_start_wh` wird beim ersten erfolgreichen Poll gesetzt.
- Reset des Plug-Counters (Total springt zurück) wird erkannt und Referenzen
  werden neu gesetzt.
- Topbar-WiFi-Symbol grün = verbunden, rot = nicht verbunden. Leistungs-Wert
  wird rot, wenn länger als 15 s kein erfolgreicher Poll.

## Hardware

| Item | Value |
|---|---|
| Board | Guition JC4827W543 |
| MCU | ESP32-S3-WROOM-1 N4R8 |
| Flash | 4 MB QIO @ 80 MHz |
| PSRAM | 8 MB Octal @ 80 MHz |
| Display | 4.3" 480×272 NV3041A via LovyanGFX/QSPI |
| Touch | nicht verwendet |

## Partition Table

NVS ist für WiFi-Init Pflicht, deshalb wieder dabei:

| Name | Offset | Size |
|---|---:|---:|
| `nvs` | `0x9000` | `0x6000` |
| `phy_init` | `0xf000` | `0x1000` |
| `factory` | `0x10000` | `0x300000` |
| `storage` | `0x310000` | `0xf0000` |
