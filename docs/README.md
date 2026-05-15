# ESP32 Web Flasher (GitHub Pages)

Statische Seite, die per [ESP Web Tools](https://esphome.github.io/esp-web-tools/)
ESP32-Firmware direkt aus dem Browser auf das Board flashen kann (Chrome/Edge,
WebSerial).

## Aktivieren

1. GitHub → Repo → **Settings → Pages**
2. *Source*: `Deploy from a branch`
3. *Branch*: `main`, *Folder*: `/docs`
4. Speichern. Nach ~1 Minute liegt die Seite unter
   `https://<user>.github.io/<repo>/`.

## Aufbau

```
docs/
├── index.html              UI (zwei Dropdowns + esp-web-install-button)
├── style.css
├── app.js                  Lädt projects.json und setzt das Manifest am Button
├── projects.json           Liste der Boards + Projekte → zeigt auf Manifeste
└── manifests/
    ├── esp32-8048s043c-kcwpv2.json
    └── guition-jc4827w543-kcwp.json
```

## Neues Projekt hinzufügen

1. In `docs/projects.json` einen neuen Eintrag unter `boards[].projects` anlegen
   und auf ein neues Manifest unter `manifests/` zeigen lassen.
2. Manifest anlegen. Format siehe
   <https://esphome.github.io/esp-web-tools/#manifest>. Bei einem **merged
   binary** ist `offset: 0`. Falls separate Parts (bootloader/partitions/app)
   geflasht werden sollen, müssen die Offsets entsprechend gesetzt werden.

## TODO – Binary-URLs eintragen

Die beiden Manifeste enthalten aktuell `TODO`-Platzhalter. Die Binary-URL muss
auf eine **öffentlich erreichbare** `.bin`-Datei zeigen, von gleicher Origin
oder mit CORS-Header `Access-Control-Allow-Origin: *`. GitHub Releases
(`github.com/<user>/<repo>/releases/download/...`) liefern den Header und sind
die einfachste Variante.

Beispiel für KCWPv2 (Release `KCWPv2-YYYYMMDD` existiert bereits):

```
https://github.com/tome9111991/ESP32-S3_Project/releases/download/KCWPv2-20251115/KCWPv2-ESP32-8048S043C-DE-full.bin
```

Für das Guition-Projekt produziert der CI-Workflow aktuell nur Artifacts (keine
öffentliche URL). Optionen:

- Release-Workflow analog zu `release-kcwpv2.yml` ergänzen.
- `.merged.bin` einmalig nach `docs/firmware/` committen (einfach, aber bläht
  die Git-History bei jedem Build auf).

## Nächste Schritte (Ideen)

- **Private-Config-Editor:** UI-Maske, in der der User WLAN-/API-Werte einträgt;
  daraus wird per JS ein `config_private.h` generiert und auf einen
  CI-Trigger (`workflow_dispatch` mit Inputs) geschickt, der dann auf
  `dispatch`-Event hin baut. Variante: alles client-seitig kompilieren ist auf
  ESP32 unrealistisch (Toolchain zu groß für Browser).
- **Sprachauswahl:** dritte Dropdown vor Install-Button, sobald ein Projekt
  mehrere Sprach-Builds anbietet (KCWPv2 hat schon DE/EN-Assets im Release).
