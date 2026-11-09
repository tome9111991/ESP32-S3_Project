// Spezielle Formularfelder fuer Standort und Zeitzone:
//  - "location-search": Stadt suchen (Open-Meteo Geocoding) und damit
//    Breitengrad, Laengengrad und Zeitzone automatisch ausfuellen.
//    Virtuelles Feld: erzeugt selbst kein #define.
//  - "timezone": IANA-Zeitzone (z.B. Europe/Berlin) per Suchliste waehlen,
//    gespeichert/ausgegeben wird der passende POSIX-String. Ein eigener
//    POSIX-String kann weiterhin direkt eingetippt werden.

import { t, tx, getLang, getLocale } from "./i18n.js";
import { POSIX_ZONES } from "./tz-zones.js";

const GEOCODING_URL = "https://geocoding-api.open-meteo.com/v1/search";
const SEARCH_DEBOUNCE_MS = 450;
const SEARCH_RESULT_COUNT = 6;

// Alte bzw. alternative Namen, die Browser (v.a. Chrome) oder Geocoder liefern,
// auf Namen aus der Tabelle abbilden.
const ZONE_ALIASES = {
  "Asia/Calcutta": "Asia/Kolkata",
  "Asia/Saigon": "Asia/Ho_Chi_Minh",
  "Asia/Katmandu": "Asia/Kathmandu",
  "Asia/Rangoon": "Asia/Yangon",
  "Asia/Dacca": "Asia/Dhaka",
  "Asia/Ulan_Bator": "Asia/Ulaanbaatar",
  "Asia/Chongqing": "Asia/Shanghai",
  "Asia/Chungking": "Asia/Shanghai",
  "Asia/Harbin": "Asia/Shanghai",
  "Asia/Thimbu": "Asia/Thimphu",
  "Asia/Istanbul": "Europe/Istanbul",
  "Europe/Kyiv": "Europe/Kiev",
  "Africa/Asmera": "Africa/Asmara",
  "Atlantic/Faeroe": "Atlantic/Faroe",
  "Pacific/Truk": "Pacific/Chuuk",
  "Pacific/Ponape": "Pacific/Pohnpei",
  "Pacific/Kanton": "Pacific/Enderbury",
  "America/Buenos_Aires": "America/Argentina/Buenos_Aires",
  "America/Catamarca": "America/Argentina/Catamarca",
  "America/Cordoba": "America/Argentina/Cordoba",
  "America/Jujuy": "America/Argentina/Jujuy",
  "America/Mendoza": "America/Argentina/Mendoza",
  "America/Indianapolis": "America/Indiana/Indianapolis",
  "America/Louisville": "America/Kentucky/Louisville",
  "America/Coral_Harbour": "America/Atikokan",
  "Australia/Canberra": "Australia/Sydney",
  "Australia/ACT": "Australia/Sydney",
  "Australia/NSW": "Australia/Sydney",
  UTC: "Etc/UTC",
  GMT: "Etc/GMT",
  "Etc/Universal": "Etc/UTC",
  "Etc/Zulu": "Etc/UTC",
};

const ZONE_NAMES = Object.keys(POSIX_ZONES);
// Kleingeschriebene Suche ueber Tabelle und Aliase ("asia/calcutta" -> Asia/Kolkata).
const ZONE_NAMES_LOWER = new Map([
  ...Object.entries(ZONE_ALIASES).map(([alias, target]) => [alias.toLowerCase(), target]),
  ...ZONE_NAMES.map((n) => [n.toLowerCase(), n]),
]);

// Lockere Pruefung eines POSIX-TZ-Strings: Name, Offset, optional Sommerzeit-Regel.
const POSIX_NAME = "(?:[A-Za-z]{3,}|<[A-Za-z0-9+-]+>)";
const POSIX_OFFSET = "[+-]?\\d{1,2}(?::\\d{2}){0,2}";
const POSIX_RULE = "(?:M\\d{1,2}\\.\\d\\.\\d|J?\\d{1,3})(?:/[+-]?\\d{1,3}(?::\\d{2}){0,2})?";
const POSIX_RE = new RegExp(
  `^${POSIX_NAME}${POSIX_OFFSET}(?:${POSIX_NAME}(?:${POSIX_OFFSET})?(?:,${POSIX_RULE},${POSIX_RULE})?)?$`,
);

// Zusatz-Key im Werte-Objekt, der den gewaehlten IANA-Namen merkt
// (nur fuer die Anzeige/den Cache, landet nicht im Header).
export function zoneMetaKey(key) {
  return `${key}__zone`;
}

// IANA-Name -> Name aus der Tabelle (exakt, Alias oder Gross/Klein egal). Sonst null.
export function resolveZoneName(name) {
  if (!name) return null;
  const trimmed = String(name).trim();
  if (POSIX_ZONES[trimmed]) return trimmed;
  const alias = ZONE_ALIASES[trimmed];
  if (alias && POSIX_ZONES[alias]) return alias;
  return ZONE_NAMES_LOWER.get(trimmed.toLowerCase()) ?? null;
}

export function browserZoneName() {
  try {
    return resolveZoneName(Intl.DateTimeFormat().resolvedOptions().timeZone);
  } catch {
    return null;
  }
}

// Namen in spitzen Klammern ("<-03>3") durch feste Buchstaben ersetzen ("STD3").
// Das Kuerzel zeigt die Firmware nie an, so ist der String sicher fuer newlib tzset().
export function toNewlibPosix(posix) {
  let index = 0;
  return String(posix).replace(/<[^>]*>/g, () => (index++ === 0 ? "STD" : "DST"));
}

export function posixForZone(zoneName) {
  const zone = resolveZoneName(zoneName);
  return zone ? toNewlibPosix(POSIX_ZONES[zone]) : "";
}

export function isPlausiblePosix(text) {
  return POSIX_RE.test(String(text).trim());
}

// Startwerte fuer ein Zeitzonen-Feld. Ohne gespeicherten Wert wird die
// Zeitzone des Browsers vorausgewaehlt (fast immer die richtige).
export function initTimezoneValues(field, cached, out) {
  const metaKey = zoneMetaKey(field.key);
  if (cached[field.key] === undefined) {
    const zone = browserZoneName();
    if (zone) {
      out[field.key] = posixForZone(zone);
      out[metaKey] = zone;
      return;
    }
  }
  let zone = cached[metaKey] ?? "";
  // Aeltere Caches ohne Zonen-Namen: passt der String zur Browser-Zone, diese anzeigen.
  if (!zone) {
    const browserZone = browserZoneName();
    if (browserZone && posixForZone(browserZone) === out[field.key]) zone = browserZone;
  }
  out[metaKey] = zone;
}

// "14:32 (GMT+1)" fuer eine IANA-Zone, live aus dem Browser berechnet.
function zoneLocalTimeText(zone) {
  try {
    const time = new Intl.DateTimeFormat(getLocale(), {
      timeZone: zone,
      hour: "2-digit",
      minute: "2-digit",
    }).format(new Date());
    let offset = "";
    try {
      const parts = new Intl.DateTimeFormat("en-US", {
        timeZone: zone,
        timeZoneName: "shortOffset",
      }).formatToParts(new Date());
      offset = parts.find((p) => p.type === "timeZoneName")?.value ?? "";
    } catch {
      // shortOffset gibt es in sehr alten Browsern nicht, dann ohne Offset.
    }
    return offset ? `${time} (${offset})` : time;
  } catch {
    return "";
  }
}

function createFieldLabel(field) {
  const labelRow = document.createElement("span");
  labelRow.className = "field-label";
  labelRow.textContent = tx(field.label);
  if (field.required) {
    const star = document.createElement("span");
    star.className = "required";
    star.textContent = "*";
    labelRow.appendChild(star);
  }
  return labelRow;
}

function createStaticHint(field) {
  if (!field.hint) return null;
  const hint = document.createElement("span");
  hint.className = "field-hint";
  hint.textContent = tx(field.hint);
  return hint;
}

// ---------- Zeitzone ----------

// ctx: { field, values, onChange(), setters: Map<key, (value) => void> }
export function renderTimezoneField({ field, values, onChange, setters }) {
  const key = field.key;
  const metaKey = zoneMetaKey(key);

  const wrap = document.createElement("div");
  wrap.className = "field";

  const inputId = `field-${key}`;
  const label = document.createElement("label");
  label.htmlFor = inputId;
  label.appendChild(createFieldLabel(field));
  wrap.appendChild(label);

  // Datalist = eingebaute Suchliste des Browsers, filtert beim Tippen.
  const listId = `tz-list-${key}`;
  const datalist = document.createElement("datalist");
  datalist.id = listId;
  for (const name of ZONE_NAMES) {
    const option = document.createElement("option");
    option.value = name;
    datalist.appendChild(option);
  }

  const input = document.createElement("input");
  input.type = "text";
  input.id = inputId;
  input.setAttribute("list", listId);
  input.autocomplete = "off";
  input.spellcheck = false;
  input.placeholder = t("tz.placeholder");
  input.dataset.key = key;
  input.dataset.type = field.type;
  if (field.required) input.required = true;
  // Anzeige: gemerkter IANA-Name, sonst der (eigene) POSIX-String.
  input.value = values[metaKey] || values[key] || "";

  const info = document.createElement("span");
  info.className = "field-hint tz-info";

  function updateInfo({ showErrors = false } = {}) {
    const zone = values[metaKey];
    info.classList.remove("error");
    if (zone) {
      const local = zoneLocalTimeText(zone);
      info.textContent = local
        ? t("tz.info.zone", { time: local, posix: values[key] })
        : t("tz.info.custom", { posix: values[key] });
    } else if (values[key]) {
      info.textContent = t("tz.info.custom", { posix: values[key] });
    } else if (input.value.trim() === "") {
      info.textContent = t("tz.info.empty");
    } else {
      info.textContent = t("tz.info.invalid");
      if (showErrors) info.classList.add("error");
    }
  }

  // Eingabe auswerten: IANA-Name aus der Liste oder eigener POSIX-String.
  function applyText(text) {
    const trimmed = text.trim();
    const zone = resolveZoneName(trimmed);
    if (zone) {
      values[key] = posixForZone(zone);
      values[metaKey] = zone;
    } else if (trimmed && !trimmed.includes("/") && isPlausiblePosix(trimmed)) {
      values[key] = toNewlibPosix(trimmed);
      values[metaKey] = "";
    } else {
      // Unvollstaendig/unbekannt: leer lassen, damit die Pflichtfeld-Pruefung greift.
      values[key] = "";
      values[metaKey] = "";
    }
  }

  input.addEventListener("input", () => {
    applyText(input.value);
    updateInfo();
    onChange();
  });
  input.addEventListener("blur", () => {
    const zone = values[metaKey];
    if (zone && input.value !== zone) input.value = zone; // Schreibweise vereinheitlichen
    updateInfo({ showErrors: true });
  });

  // Von der Stadt-Suche aufgerufen: Zone setzen, als waere sie gewaehlt worden.
  setters.set(key, (zoneOrPosix) => {
    input.value = zoneOrPosix;
    applyText(zoneOrPosix);
    updateInfo();
  });

  wrap.appendChild(input);
  wrap.appendChild(datalist);
  wrap.appendChild(info);
  const hint = createStaticHint(field);
  if (hint) wrap.appendChild(hint);

  updateInfo();
  return wrap;
}

// ---------- Stadt-Suche ----------

function resultTitle(result) {
  const region = [result.admin1, result.country].filter(Boolean);
  // admin1 doppelt vermeiden (z.B. "Berlin, Berlin, Deutschland").
  if (region[0] === result.name) region.shift();
  return [result.name, ...region].join(", ");
}

function roundCoord(value) {
  return Math.round(Number(value) * 10000) / 10000;
}

// ctx: { field, values, onChange(), setters }
// field.targets: { latitude, longitude, timezone } -> Keys der Zielfelder.
export function renderLocationSearchField({ field, values, onChange, setters }) {
  const key = field.key;
  const targets = field.targets ?? {};

  const wrap = document.createElement("div");
  wrap.className = "field loc-search";

  const inputId = `field-${key}`;
  const label = document.createElement("label");
  label.htmlFor = inputId;
  label.appendChild(createFieldLabel(field));
  wrap.appendChild(label);

  const row = document.createElement("div");
  row.className = "loc-row";

  const input = document.createElement("input");
  input.type = "search";
  input.id = inputId;
  input.autocomplete = "off";
  input.placeholder = t("loc.placeholder");
  input.value = values[key] ?? "";

  const button = document.createElement("button");
  button.type = "button";
  button.className = "btn";
  button.textContent = t("loc.search");

  row.appendChild(input);
  row.appendChild(button);
  wrap.appendChild(row);

  // Status ("Ort auswaehlen:", Fehler, "Uebernommen") steht ueber der Trefferliste.
  const status = document.createElement("span");
  status.className = "field-hint loc-status";
  wrap.appendChild(status);

  const results = document.createElement("ul");
  results.className = "loc-results";
  results.hidden = true;
  wrap.appendChild(results);

  const hint = createStaticHint(field);
  if (hint) wrap.appendChild(hint);

  function setStatus(text, kind = "info") {
    status.textContent = text;
    status.classList.toggle("error", kind === "error");
    status.classList.toggle("ok", kind === "ok");
  }

  if (values[key]) setStatus(t("loc.selected", { name: values[key] }), "ok");

  function applyResult(result) {
    const title = resultTitle(result);
    if (targets.latitude) setters.get(targets.latitude)?.(roundCoord(result.latitude));
    if (targets.longitude) setters.get(targets.longitude)?.(roundCoord(result.longitude));

    let zoneNote = "";
    if (targets.timezone) {
      const zone = resolveZoneName(result.timezone);
      if (zone) {
        setters.get(targets.timezone)?.(zone);
      } else {
        zoneNote = t("loc.zone.unknown");
      }
    }

    values[key] = title;
    input.value = title;
    results.hidden = true;
    results.innerHTML = "";
    setStatus(t("loc.selected", { name: title }) + zoneNote, zoneNote ? "error" : "ok");
    onChange();
  }

  function showResults(list) {
    results.innerHTML = "";
    for (const result of list) {
      const li = document.createElement("li");
      const btn = document.createElement("button");
      btn.type = "button";
      btn.className = "loc-result";

      const title = document.createElement("span");
      title.className = "loc-result-title";
      title.textContent = resultTitle(result);
      btn.appendChild(title);

      const meta = document.createElement("span");
      meta.className = "loc-result-meta";
      meta.textContent = [
        result.timezone,
        `${roundCoord(result.latitude)}, ${roundCoord(result.longitude)}`,
      ].filter(Boolean).join(" · ");
      btn.appendChild(meta);

      btn.addEventListener("click", () => applyResult(result));
      li.appendChild(btn);
      results.appendChild(li);
    }
    results.hidden = list.length === 0;
  }

  let debounceTimer = null;
  let activeRequest = null;

  async function search() {
    clearTimeout(debounceTimer);
    const query = input.value.trim();
    if (query.length < 2) {
      results.hidden = true;
      return;
    }
    activeRequest?.abort();
    const request = new AbortController();
    activeRequest = request;

    setStatus(t("loc.searching"));
    const url = `${GEOCODING_URL}?name=${encodeURIComponent(query)}` +
      `&count=${SEARCH_RESULT_COUNT}&language=${encodeURIComponent(getLang())}&format=json`;
    try {
      const resp = await fetch(url, { signal: request.signal });
      if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
      const data = await resp.json();
      if (request !== activeRequest) return; // inzwischen neue Suche gestartet
      const list = Array.isArray(data.results) ? data.results : [];
      showResults(list);
      setStatus(list.length ? t("loc.pick") : t("loc.none"), list.length ? "info" : "error");
    } catch (err) {
      if (err.name === "AbortError") return;
      console.error(err);
      results.hidden = true;
      setStatus(t("loc.error"), "error");
    }
  }

  input.addEventListener("input", () => {
    clearTimeout(debounceTimer);
    debounceTimer = setTimeout(search, SEARCH_DEBOUNCE_MS);
  });
  // Enter soll suchen, nicht das ganze Formular absenden.
  input.addEventListener("keydown", (e) => {
    if (e.key === "Enter") {
      e.preventDefault();
      search();
    } else if (e.key === "Escape") {
      results.hidden = true;
    }
  });
  button.addEventListener("click", search);

  return wrap;
}
