# ESP32 Web Flasher (GitHub Pages)

Statische Seite, die per [ESP Web Tools](https://esphome.github.io/esp-web-tools/)
ESP32-Firmware direkt aus dem Browser auf das Board flashen kann (Chrome/Edge
oder Firefox 151.0.1+, WebSerial). Versionen und Sprachen werden zur Laufzeit
aus den GitHub Releases gezogen.

## Aktivieren

1. GitHub → Repo → **Settings → Pages**
2. *Source*: `Deploy from a branch`
3. *Branch*: `main`, *Folder*: `/docs`
4. Speichern. Nach ~1 Minute liegt die Seite unter
   `https://<user>.github.io/<repo>/`.

## Aufbau

```
docs/
├── index.html              UI (Board/Projekt/Version/Sprache + Buttons)
├── style.css
├── app.js                  Releases laden, Manifest dynamisch erzeugen
├── config-header.js        config_private.h aus Formularwerten erzeugen
├── location-fields.js      Formularfelder "location-search" (Stadt-Suche) und "timezone"
├── tz-zones.js             IANA-Zeitzone -> POSIX-String (posix_tz_db, MIT)
├── projects.json           Index: globale Settings + Liste der Board-Dateien
├── boards/                 Eine JSON-Datei pro Board (Projekte, Form-Felder, …)
└── manifests/              Statische Manifeste fuer Projekte ohne Release
```

## Datenfluss

1. Beim Laden zieht `app.js` die `projects.json` (Index), laedt parallel die
   in `boards/` referenzierten Board-Dateien und fuellt die Board-Dropdown.
2. Nach Board-Auswahl wird die Projekt-Dropdown gefuellt.
3. Bei `source.type = "github-release"` ruft die Seite
   `https://api.github.com/repos/<owner>/<repo>/releases` ab,
   filtert auf Tags mit `tagPrefix` und liest die Asset-Namen gegen
   `assetTemplate` ein. `{lang}` wird zum Sprach-Code (DE, EN, …).
4. Nach Versions- und Sprachwahl wird im Browser ein **Manifest als Blob-URL**
   gebaut und am `<esp-web-install-button>` gesetzt. Web Flash holt die
   `.bin` dann direkt aus dem Release (CORS bei GitHub-Release-Downloads ok).
5. Daneben werden Direkt-Download-Buttons fuer alle Sprachen der gewaehlten
   Version gerendert.

## Neues Projekt mit Release-Source

In der passenden `boards/<board-id>.json` einen Projekt-Eintrag anlegen
(oder ein neues Board als eigene Datei und in `projects.json` referenzieren):

```json
{
  "id": "kcwpv2",
  "name": "Klipper Crypto Weather Panel V2",
  "description": "…",
  "chipFamily": "ESP32-S3",
  "source": {
    "type": "github-release",
    "owner": "tome9111991",
    "repo": "ESP32-S3_Project",
    "tagPrefix": "KCWPv2-",
    "assetTemplate": "KCWPv2-ESP32-8048S043C-{lang}-full.bin"
  }
}
```

- `tagPrefix` filtert die Releases (alles mit anderem Prefix wird ignoriert),
  der Rest des Tags wird als Versions-Label angezeigt.
- `assetTemplate` muss exakt zum Asset-Namen aus dem Build-Workflow passen,
  mit `{lang}` als Platzhalter. Hat eine Version mehrere Sprach-Assets,
  erscheint eine Sprach-Dropdown.
- Nur `-full.bin` (Bootloader+Partitions+App merged) wird verwendet — das ist
  was ESP Web Tools fuer einen Erstflash ab Offset 0 braucht.

## Neues Projekt mit statischem Manifest

Falls kein GitHub-Release zur Verfuegung steht, kann ein klassisches
ESP-Web-Tools-Manifest unter `docs/manifests/` abgelegt und in der jeweiligen
`boards/<board-id>.json` verlinkt werden:

```json
{
  "id": "kcwp",
  "name": "…",
  "chipFamily": "ESP32-S3",
  "source": { "type": "manifest", "manifest": "manifests/foo.json" }
}
```

Manifest-Format: <https://esphome.github.io/esp-web-tools/#manifest>. Bei einem
gemergten Binary ist `offset: 0`.

## Formularfelder fuer Build-on-Demand

Felder stehen in `source.groups[].fields` der Board-Datei. Neben `string`,
`password`, `float`, `boolean` und `select` gibt es zwei Sonderfelder:

- `"type": "location-search"` mit `"virtual": true`: Stadt-Suche ueber die
  Open-Meteo Geocoding API (kein Key, CORS offen). Ein Klick auf einen Treffer
  fuellt die in `targets` genannten Felder (`latitude`, `longitude`,
  `timezone`). Virtuelle Felder erzeugen kein `#define`.
- `"type": "timezone"`: Suchliste mit IANA-Namen (`Europe/Berlin`). Ausgegeben
  wird der POSIX-String aus `tz-zones.js`. Ohne gespeicherten Wert wird die
  Browser-Zeitzone vorausgewaehlt. Ein eigener POSIX-String kann direkt
  eingetippt werden. Namen in spitzen Klammern (`<-03>3`) werden zu `STD3`
  umgeschrieben, damit newlib `tzset()` sie sicher versteht.

`tz-zones.js` aktualisieren: `zones.json` aus
https://github.com/nayarsystems/posix_tz_db laden und als
`export const POSIX_ZONES = { ... };` neu schreiben.

## TODO

- Guition-`.bin` ist aktuell nur als CI-Artifact verfuegbar. Optionen, sobald
  das Projekt geflasht werden koennen soll:
  - Release-Workflow analog zu `release-kcwpv2.yml`.
  - `.merged.bin` einmalig nach `docs/firmware/` committen.

## Nächste Schritte (Ideen)

- **Private-Config-Editor:** UI-Maske, in der WLAN-/API-Werte eingetragen
  werden; daraus per `workflow_dispatch`-Trigger einen CI-Build anstossen, der
  ein personalisiertes Image baut. Vollstaendiges Compile im Browser ist mit
  der ESP-IDF-Toolchain unrealistisch.
