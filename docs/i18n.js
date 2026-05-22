const LANG_STORAGE_KEY = "kcwp-lang-v1";
const SUPPORTED_LANGS = ["de", "en"];

const STRINGS = {
  de: {
    "hint.browser": "Funktioniert in Chrome, Edge oder Firefox 151.0.1+ (WebSerial). ESP32 per USB anschließen, dann den Schritten unten folgen.",
    "footer.source": "Quellcode & Firmware-Builds:",

    "step.board": "Board",
    "step.project": "Projekt",
    "step.version": "Version",
    "step.lang": "Sprache",
    "step.config": "Konfiguration",
    "step.edit": "Ändern",
    "step.edit.disabled": "Während ein Build läuft, kann der Wizard nicht geändert werden.",
    "step.project.count.one": "1 Projekt",
    "step.project.count.other": "{n} Projekte",
    "step.empty": "Keine Einträge verfügbar.",
    "step.versions.loading": "Versionen werden geladen …",
    "step.config.summary": "{n} Werte gesetzt",
    "project.directory": "Projektverzeichnis: ",

    "form.min": "Mindestens {n} muss aktiviert sein.",
    "form.required.missing": "Pflichtfeld fehlt: {label}",
    "form.min.violated": "{label}: mindestens {n} aktivieren.",
    "form.invalid": "Bitte Eingaben prüfen.",
    "form.preview": "Vorschau: {file}",
    "form.apply": "Konfiguration übernehmen",

    "install.webflash": "Web Flash (Browser → USB)",
    "install.direct": "Direkt-Download (.bin)",
    "install.direct.short": "Direkt-Download",

    "build.heading": "Firmware bauen",
    "build.intro": "Dein Build wird in der GitHub-Cloud erstellt und dauert ca. 3–4 Minuten.",
    "build.start": "🛠 Firmware bauen",
    "build.captcha.required": "Bitte zuerst die Bot-Prüfung abschließen.",
    "build.captcha.load.error": "Bot-Prüfung konnte nicht geladen werden.",
    "build.captcha.error": "Bot-Prüfung fehlgeschlagen, bitte erneut versuchen.",
    "build.captcha.expired": "Bot-Prüfung abgelaufen, bitte erneut.",
    "build.running": "Firmware wird gebaut …",
    "build.phase.canceling": "Build wird bei GitHub abgebrochen …",
    "build.phase.dispatching": "Build wird gestartet …",
    "build.phase.queued": "In Warteschlange …",
    "build.phase.in_progress": "Compile läuft …",
    "build.phase.waiting": "Warte auf GitHub Actions …",
    "build.elapsed": "{mm}:{ss} vergangen",
    "build.id": "Build-ID: {id}",
    "build.cancel": "Abbrechen",
    "build.cancel.title": "Bricht den GitHub-Actions-Build ab.",
    "build.cancel.title.waiting": "Build wird noch gestartet, Abbrechen ist gleich möglich.",
    "build.cancelled.heading": "Build abgebrochen",
    "build.cancelled.body": "Der GitHub-Actions-Build wurde abgebrochen.",
    "build.details": "Details: <a href=\"{url}\" target=\"_blank\" rel=\"noopener\">GitHub Actions Run</a>",
    "build.edit.config": "Konfiguration bearbeiten",
    "build.retry": "Erneut bauen",
    "build.success.note": "Build erfolgreich. Diese Firmware wird in ~24h automatisch von GitHub gelöscht.",
    "build.success.new": "Neuen Build starten",
    "build.failed.heading": "Build fehlgeschlagen",
    "build.error.unknown": "Unbekannter Fehler.",
    "build.retry.alt": "Erneut versuchen",
    "build.error.no_worker": "Worker-URL fehlt in projects.json.",
    "build.error.no_correlation": "Worker antwortete ohne correlation_id.",
    "build.cancelled.short": "Build wurde abgebrochen.",
    "build.ended_with": "Build endete mit '{conclusion}'.",
    "build.cancel.too_late": "Build konnte nicht mehr abgebrochen werden.",
    "build.cancel.failed": "Abbrechen fehlgeschlagen: {message}",

    "badge.latest": "Latest",
    "badge.pre": "Pre",
    "status.loading.releases": "Lade Releases …",
    "status.error.releases": "Releases konnten nicht geladen werden: {message}",
    "status.error.config": "Konfiguration konnte nicht geladen werden: {message}",

    "lang.toggle.label": "Sprache wählen",
    "lang.de": "Deutsch",
    "lang.en": "English",
  },
  en: {
    "hint.browser": "Works in Chrome, Edge, or Firefox 151.0.1+ (WebSerial). Connect the ESP32 via USB, then follow the steps below.",
    "footer.source": "Source code & firmware builds:",

    "step.board": "Board",
    "step.project": "Project",
    "step.version": "Version",
    "step.lang": "Language",
    "step.config": "Configuration",
    "step.edit": "Edit",
    "step.edit.disabled": "The wizard can't be changed while a build is running.",
    "step.project.count.one": "1 project",
    "step.project.count.other": "{n} projects",
    "step.empty": "No entries available.",
    "step.versions.loading": "Loading versions …",
    "step.config.summary": "{n} values set",
    "project.directory": "Project directory: ",

    "form.min": "At least {n} must be enabled.",
    "form.required.missing": "Required field missing: {label}",
    "form.min.violated": "{label}: enable at least {n}.",
    "form.invalid": "Please check your input.",
    "form.preview": "Preview: {file}",
    "form.apply": "Apply configuration",

    "install.webflash": "Web Flash (browser → USB)",
    "install.direct": "Direct download (.bin)",
    "install.direct.short": "Direct download",

    "build.heading": "Build firmware",
    "build.intro": "Your build runs in the GitHub cloud and takes about 3–4 minutes.",
    "build.start": "🛠 Build firmware",
    "build.captcha.required": "Please complete the bot check first.",
    "build.captcha.load.error": "Bot check could not be loaded.",
    "build.captcha.error": "Bot check failed, please try again.",
    "build.captcha.expired": "Bot check expired, please retry.",
    "build.running": "Building firmware …",
    "build.phase.canceling": "Cancelling build at GitHub …",
    "build.phase.dispatching": "Starting build …",
    "build.phase.queued": "Queued …",
    "build.phase.in_progress": "Compiling …",
    "build.phase.waiting": "Waiting for GitHub Actions …",
    "build.elapsed": "{mm}:{ss} elapsed",
    "build.id": "Build ID: {id}",
    "build.cancel": "Cancel",
    "build.cancel.title": "Aborts the GitHub Actions build.",
    "build.cancel.title.waiting": "Build is still starting, cancel will be available shortly.",
    "build.cancelled.heading": "Build cancelled",
    "build.cancelled.body": "The GitHub Actions build was cancelled.",
    "build.details": "Details: <a href=\"{url}\" target=\"_blank\" rel=\"noopener\">GitHub Actions run</a>",
    "build.edit.config": "Edit configuration",
    "build.retry": "Build again",
    "build.success.note": "Build successful. GitHub will delete this firmware automatically in about 24h.",
    "build.success.new": "Start new build",
    "build.failed.heading": "Build failed",
    "build.error.unknown": "Unknown error.",
    "build.retry.alt": "Try again",
    "build.error.no_worker": "Worker URL missing in projects.json.",
    "build.error.no_correlation": "Worker responded without correlation_id.",
    "build.cancelled.short": "Build was cancelled.",
    "build.ended_with": "Build ended with '{conclusion}'.",
    "build.cancel.too_late": "Build could no longer be cancelled.",
    "build.cancel.failed": "Cancel failed: {message}",

    "badge.latest": "Latest",
    "badge.pre": "Pre",
    "status.loading.releases": "Loading releases …",
    "status.error.releases": "Could not load releases: {message}",
    "status.error.config": "Could not load configuration: {message}",

    "lang.toggle.label": "Choose language",
    "lang.de": "Deutsch",
    "lang.en": "English",
  },
};

const LOCALES = { de: "de-DE", en: "en-GB" };

function detectInitialLang() {
  const urlLang = new URLSearchParams(location.search).get("ui");
  if (urlLang && SUPPORTED_LANGS.includes(urlLang)) return urlLang;
  try {
    const stored = localStorage.getItem(LANG_STORAGE_KEY);
    if (stored && SUPPORTED_LANGS.includes(stored)) return stored;
  } catch { /* localStorage gesperrt — egal */ }
  const nav = (navigator.language || "de").toLowerCase();
  if (nav.startsWith("de")) return "de";
  return "en";
}

let currentLang = detectInitialLang();
const langChangeListeners = new Set();

export function getLang() {
  return currentLang;
}

export function getLocale() {
  return LOCALES[currentLang] ?? "en-GB";
}

export function setLang(lang) {
  if (!SUPPORTED_LANGS.includes(lang) || lang === currentLang) return;
  currentLang = lang;
  try { localStorage.setItem(LANG_STORAGE_KEY, lang); } catch { /* ignore */ }
  document.documentElement.lang = lang;
  for (const fn of langChangeListeners) {
    try { fn(lang); } catch (err) { console.error(err); }
  }
}

export function onLangChange(fn) {
  langChangeListeners.add(fn);
  return () => langChangeListeners.delete(fn);
}

function format(template, vars) {
  if (!vars) return template;
  return template.replace(/\{(\w+)\}/g, (_, k) => (k in vars ? String(vars[k]) : `{${k}}`));
}

export function t(key, vars) {
  const dict = STRINGS[currentLang] ?? STRINGS.de;
  const raw = dict[key] ?? STRINGS.de[key] ?? key;
  return format(raw, vars);
}

// Wert aus projects.json: entweder String (Fallback) oder { de, en, ... } Objekt.
export function tx(value, fallback = "") {
  if (value == null) return fallback;
  if (typeof value === "string") return value;
  if (typeof value === "object") {
    return value[currentLang] ?? value.de ?? value.en ?? fallback;
  }
  return String(value);
}

const FLAG_SVG = {
  de: `<svg viewBox="0 0 5 3" xmlns="http://www.w3.org/2000/svg" aria-hidden="true"><rect width="5" height="1" fill="#000"/><rect y="1" width="5" height="1" fill="#D00"/><rect y="2" width="5" height="1" fill="#FFCE00"/></svg>`,
  en: `<svg viewBox="0 0 60 30" xmlns="http://www.w3.org/2000/svg" aria-hidden="true"><clipPath id="kcwp-uk-clip"><path d="M30,15h30v15zv15H0zH0V0zV0h30z"/></clipPath><path d="M0,0v30h60V0z" fill="#012169"/><path d="M0,0L60,30M60,0L0,30" stroke="#fff" stroke-width="6"/><path d="M0,0L60,30M60,0L0,30" clip-path="url(#kcwp-uk-clip)" stroke="#C8102E" stroke-width="4"/><path d="M30,0v30M0,15h60" stroke="#fff" stroke-width="10"/><path d="M30,0v30M0,15h60" stroke="#C8102E" stroke-width="6"/></svg>`,
};

export function mountLangToggle(container) {
  const wrap = document.createElement("div");
  wrap.className = "lang-toggle";
  wrap.setAttribute("role", "group");
  wrap.setAttribute("aria-label", t("lang.toggle.label"));

  const buttons = new Map();
  for (const lang of SUPPORTED_LANGS) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "lang-flag";
    btn.dataset.lang = lang;
    btn.title = t(`lang.${lang}`);
    btn.setAttribute("aria-label", t(`lang.${lang}`));
    btn.innerHTML = FLAG_SVG[lang];
    btn.addEventListener("click", () => setLang(lang));
    wrap.appendChild(btn);
    buttons.set(lang, btn);
  }

  function syncActive() {
    for (const [lang, btn] of buttons) {
      const active = lang === currentLang;
      btn.setAttribute("aria-pressed", active ? "true" : "false");
      btn.classList.toggle("is-active", active);
      btn.title = t(`lang.${lang}`);
      btn.setAttribute("aria-label", t(`lang.${lang}`));
    }
    wrap.setAttribute("aria-label", t("lang.toggle.label"));
  }

  syncActive();
  onLangChange(syncActive);
  container.appendChild(wrap);
}

document.documentElement.lang = currentLang;
