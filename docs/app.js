const wizardEl = document.getElementById("wizard");
const installSlot = document.getElementById("install-slot");
const statusBox = document.getElementById("status");

let config;
const releaseCache = new Map();
const versionsCache = new Map();
let activeManifestUrl = null;

const state = {
  board: null,
  project: null,
  version: null,
  lang: null,
  config: null, // null = nicht uebernommen; sonst Werte-Objekt
  build: null,  // null | { phase, correlationId, startedAt, lastStatus, assetUrl, runUrl, message }
};

let pollAbort = null; // markiert die aktuelle Polling-Schleife
let captchaToken = null; // Cloudflare Turnstile, gilt nur fuer den naechsten /build-Aufruf
let captchaWidgetId = null;

// ---------- Helpers ----------

function setStatus(msg, kind = "info") {
  if (!msg) {
    statusBox.hidden = true;
    statusBox.textContent = "";
    return;
  }
  statusBox.hidden = false;
  statusBox.dataset.kind = kind;
  statusBox.textContent = msg;
}

function getBoard(id = state.board) {
  return config.boards.find((b) => b.id === id) ?? null;
}

function getProject(boardId = state.board, projectId = state.project) {
  return getBoard(boardId)?.projects.find((p) => p.id === projectId) ?? null;
}

function projectKey(boardId, projectId) {
  return `${boardId}/${projectId}`;
}

function getCurrentVersion(project) {
  return project._versions?.find((v) => v.tag === state.version) ?? null;
}

function clearFrom(stepId) {
  const order = ["board", "project", "version", "lang", "config"];
  const start = order.indexOf(stepId);
  for (let i = start; i < order.length; i++) state[order[i]] = null;
  // Build-State gehoert logisch zu config. Wird mit verworfen.
  if (start <= order.indexOf("config")) {
    state.build = null;
    pollAbort = {};
  }
}

// ---------- URL state ----------

function readUrlState() {
  const p = new URLSearchParams(location.search);
  return {
    board: p.get("board"),
    project: p.get("project"),
    version: p.get("version"),
    lang: p.get("lang"),
  };
}

function writeUrlState() {
  const p = new URLSearchParams();
  if (state.board) p.set("board", state.board);
  if (state.project) p.set("project", state.project);
  if (state.version) p.set("version", state.version);
  if (state.lang) p.set("lang", state.lang);
  const qs = p.toString();
  const newUrl = `${location.pathname}${qs ? "?" + qs : ""}${location.hash}`;
  if (newUrl !== location.pathname + location.search + location.hash) {
    history.replaceState(null, "", newUrl);
  }
}

// ---------- Form working-cache (localStorage) ----------

const FORM_CACHE_VERSION = "v1";

function formCacheKey(boardId, projectId) {
  return `kcwp-form-${FORM_CACHE_VERSION}:${boardId}/${projectId}`;
}

function loadFormCache(boardId, projectId) {
  try {
    const raw = localStorage.getItem(formCacheKey(boardId, projectId));
    return raw ? JSON.parse(raw) : null;
  } catch {
    return null;
  }
}

function saveFormCache(boardId, projectId, values) {
  try {
    localStorage.setItem(formCacheKey(boardId, projectId), JSON.stringify(values));
  } catch {
    // Quota o.ae. — egal, ist nur Komfort.
  }
}

function initialFormValues(project) {
  const cached = loadFormCache(state.board, project.id) ?? {};
  const out = {};
  for (const group of project.source.groups ?? []) {
    for (const f of group.fields) {
      out[f.key] = cached[f.key] ?? f.default ?? "";
    }
  }
  return out;
}

// ---------- GitHub Releases ----------

async function fetchReleases(owner, repo) {
  const key = `${owner}/${repo}`;
  if (releaseCache.has(key)) return releaseCache.get(key);
  const url = `https://api.github.com/repos/${owner}/${repo}/releases?per_page=50`;
  const res = await fetch(url, { headers: { Accept: "application/vnd.github+json" } });
  if (!res.ok) throw new Error(`GitHub API ${res.status}: ${res.statusText}`);
  const data = await res.json();
  releaseCache.set(key, data);
  return data;
}

function parseReleases(releases, project) {
  const { tagPrefix, assetTemplate } = project.source;
  const escaped = assetTemplate.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const assetRe = new RegExp("^" + escaped.replace("\\{lang\\}", "([A-Za-z0-9]+)") + "$");

  const out = [];
  for (const rel of releases) {
    if (!rel.tag_name.startsWith(tagPrefix)) continue;
    if (rel.draft) continue;
    const langs = new Map();
    for (const asset of rel.assets ?? []) {
      const m = assetRe.exec(asset.name);
      if (!m) continue;
      langs.set(m[1].toUpperCase(), {
        name: asset.name,
        url: asset.browser_download_url,
        size: asset.size,
      });
    }
    if (langs.size === 0) continue;
    out.push({
      tag: rel.tag_name,
      version: rel.tag_name.slice(tagPrefix.length),
      publishedAt: rel.published_at,
      prerelease: rel.prerelease,
      langs,
    });
  }
  out.sort((a, b) => (b.publishedAt ?? "").localeCompare(a.publishedAt ?? ""));
  return out;
}

async function loadVersions(project) {
  const key = projectKey(state.board, project.id);
  if (versionsCache.has(key)) return versionsCache.get(key);
  const promise = (async () => {
    const releases = await fetchReleases(project.source.owner, project.source.repo);
    const versions = parseReleases(releases, project);
    project._versions = versions;
    return versions;
  })();
  versionsCache.set(key, promise);
  return promise;
}

// ---------- Step model ----------

function activeStepId() {
  if (!state.board) return "board";
  if (!state.project) return "project";
  const project = getProject();
  if (project.source.type === "github-release") {
    if (!state.version) return "version";
    const v = getCurrentVersion(project);
    if (v && v.langs.size > 1 && !state.lang) return "lang";
  } else if (project.source.type === "build-on-demand") {
    if (!state.config) return "config";
  }
  return null;
}

function stepState(stepId) {
  if (activeStepId() === stepId) return "active";
  const val = state[stepId];
  if (val) return "completed";
  return "locked";
}

// ---------- Generic option-list renderer ----------

function makeOption({ value, title, meta = "", badge = "" }, onClick) {
  const li = document.createElement("li");
  li.className = "option";
  li.dataset.value = value;
  li.tabIndex = 0;
  li.innerHTML =
    `<span class="option-title"></span>` +
    (meta ? `<span class="option-meta"></span>` : "") +
    (badge ? `<span class="badge"></span>` : "");
  li.querySelector(".option-title").textContent = title;
  if (meta) li.querySelector(".option-meta").textContent = meta;
  if (badge) li.querySelector(".badge").textContent = badge;
  li.addEventListener("click", () => onClick(value));
  li.addEventListener("keydown", (e) => {
    if (e.key === "Enter" || e.key === " ") {
      e.preventDefault();
      onClick(value);
    }
  });
  return li;
}

function renderStepShell({ stepNum, stepId, label, valueLabel }) {
  const li = document.createElement("li");
  li.className = "step";
  li.dataset.state = stepState(stepId);

  const header = document.createElement("div");
  header.className = "step-header";
  header.innerHTML =
    `<span class="step-num"></span>` +
    `<span class="step-label"></span>` +
    `<span class="step-value"></span>`;
  header.querySelector(".step-num").textContent = String(stepNum);
  header.querySelector(".step-label").textContent = label;

  if (li.dataset.state === "completed" && valueLabel) {
    header.querySelector(".step-value").textContent = valueLabel;
    const editBtn = document.createElement("button");
    editBtn.className = "step-edit";
    editBtn.type = "button";
    editBtn.textContent = "Ändern";
    editBtn.addEventListener("click", () => {
      clearFrom(stepId);
      render();
    });
    header.appendChild(editBtn);
  }
  li.appendChild(header);
  return li;
}

function renderOptionsStep({ stepNum, stepId, label, valueLabel, optionsBuilder, emptyMsg }) {
  const li = renderStepShell({ stepNum, stepId, label, valueLabel });
  if (li.dataset.state !== "active") return li;

  const body = document.createElement("div");
  body.className = "step-body";
  const opts = optionsBuilder();
  if (!opts || opts.length === 0) {
    const empty = document.createElement("div");
    empty.className = "empty";
    empty.textContent = emptyMsg ?? "Keine Eintraege verfuegbar.";
    body.appendChild(empty);
  } else {
    const ul = document.createElement("ul");
    ul.className = "options";
    for (const opt of opts) ul.appendChild(opt);
    body.appendChild(ul);
  }
  li.appendChild(body);
  return li;
}

// ---------- Config form step ----------

function renderConfigStep({ stepNum, project }) {
  const stepId = "config";
  const completedSummary = state.config
    ? `${Object.keys(state.config).length} Werte gesetzt`
    : null;
  const li = renderStepShell({ stepNum, stepId, label: "Konfiguration", valueLabel: completedSummary });
  if (li.dataset.state !== "active") return li;

  const body = document.createElement("div");
  body.className = "step-body config-form";

  const values = initialFormValues(project);
  const form = document.createElement("form");
  form.className = "form";
  form.noValidate = true;

  for (const group of project.source.groups ?? []) {
    const fs = document.createElement("fieldset");
    const legend = document.createElement("legend");
    legend.textContent = group.label;
    fs.appendChild(legend);

    for (const field of group.fields) {
      const wrap = document.createElement("label");
      wrap.className = "field";

      const labelRow = document.createElement("span");
      labelRow.className = "field-label";
      labelRow.textContent = field.label;
      if (field.required) {
        const star = document.createElement("span");
        star.className = "required";
        star.textContent = "*";
        labelRow.appendChild(star);
      }
      wrap.appendChild(labelRow);

      let input;
      if (field.type === "select") {
        input = document.createElement("select");
        for (const opt of field.options ?? []) {
          const o = document.createElement("option");
          o.value = String(opt);
          o.textContent = String(opt);
          input.appendChild(o);
        }
      } else {
        input = document.createElement("input");
        if (field.type === "password") input.type = "password";
        else if (field.type === "float") input.type = "number";
        else input.type = "text";
        if (field.type === "float") input.step = "any";
        if (field.placeholder) input.placeholder = field.placeholder;
      }
      input.value = values[field.key] ?? "";
      input.dataset.key = field.key;
      input.dataset.type = field.type;
      if (field.required) input.required = true;
      input.addEventListener("input", () => {
        values[field.key] = input.value;
        saveFormCache(state.board, project.id, values);
        updatePreview();
        updateSubmitState();
      });
      wrap.appendChild(input);

      if (field.hint) {
        const hint = document.createElement("span");
        hint.className = "field-hint";
        hint.textContent = field.hint;
        wrap.appendChild(hint);
      }
      fs.appendChild(wrap);
    }
    form.appendChild(fs);
  }

  // Preview
  const previewWrap = document.createElement("details");
  previewWrap.className = "preview";
  previewWrap.open = true;
  const summary = document.createElement("summary");
  summary.textContent = `Vorschau: ${project.source.headerName ?? "config_private.h"}`;
  const pre = document.createElement("pre");
  pre.className = "preview-code";
  previewWrap.appendChild(summary);
  previewWrap.appendChild(pre);

  function updatePreview() {
    pre.textContent = generateConfigHeader(project, values);
  }

  // Submit
  const actions = document.createElement("div");
  actions.className = "form-actions";

  const submitBtn = document.createElement("button");
  submitBtn.type = "submit";
  submitBtn.className = "btn btn-primary";
  submitBtn.textContent = "Konfiguration übernehmen";

  const dlBtn = document.createElement("button");
  dlBtn.type = "button";
  dlBtn.className = "btn";
  dlBtn.textContent = `⬇ ${project.source.headerName ?? "config_private.h"}`;
  dlBtn.addEventListener("click", () => {
    downloadConfigHeader(project, values);
  });

  actions.appendChild(submitBtn);
  actions.appendChild(dlBtn);

  function validate() {
    for (const group of project.source.groups ?? []) {
      for (const f of group.fields) {
        if (!f.required) continue;
        const v = values[f.key];
        if (v === undefined || v === null || String(v).trim() === "") return false;
      }
    }
    return true;
  }

  function updateSubmitState() {
    submitBtn.disabled = !validate();
  }

  form.addEventListener("submit", (e) => {
    e.preventDefault();
    if (!validate()) return;
    state.config = { ...values };
    render();
  });

  form.appendChild(previewWrap);
  form.appendChild(actions);
  body.appendChild(form);
  li.appendChild(body);

  updatePreview();
  updateSubmitState();
  return li;
}

// ---------- Config header generation ----------

function escapeCString(s) {
  return String(s).replace(/\\/g, "\\\\").replace(/"/g, '\\"');
}

function formatFloat(v) {
  const num = Number(v);
  if (!Number.isFinite(num)) return "0.0f";
  // Sechs Nachkommastellen + f-Suffix, kompatibel zur Beispiel-Datei.
  return num.toFixed(6) + "f";
}

function generateConfigHeader(project, values) {
  const lines = [];
  lines.push((project.source.preamble ?? "#pragma once\n").trimEnd());
  lines.push("");
  for (const group of project.source.groups ?? []) {
    lines.push(`// ${group.label}`);
    for (const f of group.fields) {
      const raw = values[f.key];
      let val;
      if (f.type === "float") {
        val = formatFloat(raw);
      } else {
        val = `"${escapeCString(raw ?? "")}"`;
      }
      lines.push(`#define ${f.key} ${val}`);
    }
    lines.push("");
  }
  return lines.join("\n");
}

function downloadConfigHeader(project, values) {
  const text = generateConfigHeader(project, values);
  const blob = new Blob([text], { type: "text/plain" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = project.source.headerName ?? "config_private.h";
  document.body.appendChild(a);
  a.click();
  a.remove();
  URL.revokeObjectURL(url);
}

// ---------- Install slot (per source type) ----------

// GitHub Release Asset URLs werden auf release-assets.githubusercontent.com
// umgeleitet, und der Host setzt keine CORS-Header. esp-web-tools-Fetches
// scheitern dann mit "Failed to fetch". Wenn ein assetProxyUrl konfiguriert
// ist, schicken wir die URL durch unseren Cloudflare-Worker, der CORS setzt.
function proxifyAssetUrl(rawUrl) {
  const proxy = config?.assetProxyUrl;
  if (!proxy) return rawUrl;
  return `${proxy}/asset?url=${encodeURIComponent(rawUrl)}`;
}

function clearInstallSlot() {
  installSlot.innerHTML = "";
  installSlot.hidden = true;
  if (activeManifestUrl?.startsWith("blob:")) {
    URL.revokeObjectURL(activeManifestUrl);
    activeManifestUrl = null;
  }
}

function installSlotHeading(text) {
  const h = document.createElement("h3");
  h.textContent = text;
  installSlot.appendChild(h);
}

function renderInstallRelease(project, version, lang) {
  const asset = version.langs.get(lang);
  if (!asset) return;
  const manifest = {
    name: project.name,
    version: version.version,
    new_install_prompt_erase: true,
    builds: [
      { chipFamily: project.chipFamily ?? "ESP32-S3", parts: [{ path: proxifyAssetUrl(asset.url), offset: 0 }] },
    ],
  };
  const blob = new Blob([JSON.stringify(manifest)], { type: "application/json" });
  activeManifestUrl = URL.createObjectURL(blob);

  installSlotHeading("Web Flash (Browser → USB)");
  const btn = document.createElement("esp-web-install-button");
  btn.manifest = activeManifestUrl;
  installSlot.appendChild(btn);

  installSlotHeading("Direkt-Download (.bin)");
  const dl = document.createElement("div");
  dl.className = "downloads";
  const a = document.createElement("a");
  a.href = asset.url;
  a.className = "download-btn";
  a.download = asset.name;
  const sizeMb = asset.size ? ` (${(asset.size / 1024 / 1024).toFixed(2)} MB)` : "";
  a.textContent = `⬇ ${lang}: ${asset.name}${sizeMb}`;
  dl.appendChild(a);
  installSlot.appendChild(dl);

  installSlot.hidden = false;
}

function renderInstallManifest(project) {
  installSlotHeading("Web Flash (Browser → USB)");
  const btn = document.createElement("esp-web-install-button");
  btn.manifest = project.source.manifest;
  activeManifestUrl = project.source.manifest;
  installSlot.appendChild(btn);
  installSlot.hidden = false;
}

function renderInstallBuildOnDemand(project) {
  installSlot.hidden = false;
  const build = state.build;

  if (!build) {
    renderBuildIdle(project);
    return;
  }
  if (build.phase === "dispatching" || build.phase === "polling") {
    renderBuildProgress(project);
    return;
  }
  if (build.phase === "success") {
    renderBuildSuccess(project);
    return;
  }
  if (build.phase === "error") {
    renderBuildError(project);
    return;
  }
}

function renderBuildIdle(project) {
  installSlotHeading("Firmware bauen");
  const info = document.createElement("p");
  info.className = "muted";
  info.textContent =
    "Dein Build wird in der GitHub-Cloud erstellt und dauert ca. 3–4 Minuten.";
  installSlot.appendChild(info);

  const siteKey = project.source.turnstileSiteKey;
  const needCaptcha = !!siteKey;
  captchaToken = null;
  captchaWidgetId = null;

  let captchaContainer = null;
  if (needCaptcha) {
    captchaContainer = document.createElement("div");
    captchaContainer.className = "turnstile-widget";
    installSlot.appendChild(captchaContainer);
  }

  const actions = document.createElement("div");
  actions.className = "downloads";

  const buildBtn = document.createElement("button");
  buildBtn.className = "btn btn-primary";
  buildBtn.type = "button";
  buildBtn.textContent = "🛠 Firmware bauen";
  buildBtn.disabled = needCaptcha;
  if (needCaptcha) buildBtn.title = "Bitte zuerst die Bot-Pruefung abschliessen.";
  buildBtn.addEventListener("click", () => startBuild(project));
  actions.appendChild(buildBtn);

  const dlBtn = document.createElement("button");
  dlBtn.className = "btn";
  dlBtn.type = "button";
  dlBtn.textContent = `⬇ ${project.source.headerName ?? "config_private.h"}`;
  dlBtn.addEventListener("click", () => downloadConfigHeader(project, state.config));
  actions.appendChild(dlBtn);

  installSlot.appendChild(actions);

  if (needCaptcha) {
    mountTurnstile(captchaContainer, siteKey, buildBtn);
  }
}

async function mountTurnstile(container, sitekey, buildBtn) {
  // Script ist async geladen, kann beim ersten Render fehlen.
  let waited = 0;
  while (!window.turnstile && waited < 5000) {
    await new Promise((r) => setTimeout(r, 80));
    waited += 80;
  }
  if (!window.turnstile) {
    container.textContent = "Bot-Pruefung konnte nicht geladen werden.";
    container.classList.add("error");
    buildBtn.disabled = false; // Lass den Klick trotzdem zu — Worker pruefen.
    return;
  }
  captchaWidgetId = window.turnstile.render(container, {
    sitekey,
    theme: "dark",
    callback: (token) => {
      captchaToken = token;
      buildBtn.disabled = false;
      buildBtn.title = "";
    },
    "error-callback": () => {
      captchaToken = null;
      buildBtn.disabled = true;
      buildBtn.title = "Bot-Pruefung fehlgeschlagen, bitte erneut versuchen.";
    },
    "expired-callback": () => {
      captchaToken = null;
      buildBtn.disabled = true;
      buildBtn.title = "Bot-Pruefung abgelaufen, bitte erneut.";
    },
  });
}

function renderBuildProgress(project) {
  installSlotHeading("Firmware wird gebaut …");
  const build = state.build;

  const elapsed = Math.floor((Date.now() - build.startedAt) / 1000);
  const mm = Math.floor(elapsed / 60);
  const ss = String(elapsed % 60).padStart(2, "0");

  const phaseText =
    build.phase === "dispatching"
      ? "Build wird gestartet …"
      : build.lastStatus === "queued"
        ? "In Warteschlange …"
        : build.lastStatus === "in_progress"
          ? "Compile läuft …"
          : "Warte auf GitHub Actions …";

  const status = document.createElement("div");
  status.className = "build-progress";
  status.innerHTML = `
    <div class="build-spinner"></div>
    <div class="build-text">
      <div class="build-phase"></div>
      <div class="build-elapsed muted"></div>
    </div>
  `;
  status.querySelector(".build-phase").textContent = phaseText;
  status.querySelector(".build-elapsed").textContent = `${mm}:${ss} vergangen`;
  installSlot.appendChild(status);

  if (build.correlationId) {
    const hint = document.createElement("p");
    hint.className = "muted small";
    hint.textContent = `Build-ID: ${build.correlationId}`;
    installSlot.appendChild(hint);
  }

  const actions = document.createElement("div");
  actions.className = "downloads";
  const cancelBtn = document.createElement("button");
  cancelBtn.className = "btn";
  cancelBtn.type = "button";
  cancelBtn.textContent = "Abbrechen";
  cancelBtn.title = "Bricht nur die Anzeige ab — Build laeuft auf GitHub bis er fertig ist.";
  cancelBtn.addEventListener("click", () => {
    pollAbort = {};
    state.build = null;
    render();
  });
  actions.appendChild(cancelBtn);
  installSlot.appendChild(actions);
}

function renderBuildSuccess(project) {
  const build = state.build;
  installSlotHeading("Web Flash (Browser → USB)");

  // Manifest dynamisch erzeugen, asset_url ab Offset 0.
  const manifest = {
    name: project.name,
    version: "on-demand",
    new_install_prompt_erase: true,
    builds: [
      { chipFamily: project.chipFamily ?? "ESP32-S3", parts: [{ path: proxifyAssetUrl(build.assetUrl), offset: 0 }] },
    ],
  };
  const blob = new Blob([JSON.stringify(manifest)], { type: "application/json" });
  activeManifestUrl = URL.createObjectURL(blob);

  const btn = document.createElement("esp-web-install-button");
  btn.manifest = activeManifestUrl;
  installSlot.appendChild(btn);

  installSlotHeading("Direkt-Download");
  const dl = document.createElement("div");
  dl.className = "downloads";
  const a = document.createElement("a");
  a.href = build.assetUrl;
  a.className = "download-btn";
  a.download = "firmware.bin";
  a.textContent = `⬇ firmware.bin`;
  dl.appendChild(a);

  const cfgBtn = document.createElement("button");
  cfgBtn.className = "btn";
  cfgBtn.type = "button";
  cfgBtn.textContent = `⬇ ${project.source.headerName ?? "config_private.h"}`;
  cfgBtn.addEventListener("click", () => downloadConfigHeader(project, state.config));
  dl.appendChild(cfgBtn);

  installSlot.appendChild(dl);

  const meta = document.createElement("p");
  meta.className = "muted small";
  meta.innerHTML = `Build erfolgreich. Diese Firmware wird in ~24h automatisch von GitHub gelöscht.`;
  installSlot.appendChild(meta);

  const redoActions = document.createElement("div");
  redoActions.className = "downloads";
  const newBtn = document.createElement("button");
  newBtn.className = "btn";
  newBtn.type = "button";
  newBtn.textContent = "Neuen Build starten";
  newBtn.addEventListener("click", () => {
    state.build = null;
    render();
  });
  redoActions.appendChild(newBtn);
  installSlot.appendChild(redoActions);
}

function renderBuildError(project) {
  installSlotHeading("Build fehlgeschlagen");
  const p = document.createElement("p");
  p.className = "muted";
  p.textContent = state.build?.message ?? "Unbekannter Fehler.";
  installSlot.appendChild(p);

  if (state.build?.runUrl) {
    const link = document.createElement("p");
    link.className = "small";
    link.innerHTML = `Details: <a href="${state.build.runUrl}" target="_blank" rel="noopener">GitHub Actions Run</a>`;
    installSlot.appendChild(link);
  }

  const actions = document.createElement("div");
  actions.className = "downloads";
  const retryBtn = document.createElement("button");
  retryBtn.className = "btn btn-primary";
  retryBtn.type = "button";
  retryBtn.textContent = "Erneut versuchen";
  retryBtn.addEventListener("click", () => startBuild(project));
  actions.appendChild(retryBtn);
  installSlot.appendChild(actions);
}

// ---------- Build flow ----------

function encodeBase64Utf8(s) {
  // Browser btoa kann nur Latin-1. Fuer UTF-8 (Umlaute in SSID o.ae.) erst
  // nach UTF-8 enkodieren, dann base64-encodieren.
  const bytes = new TextEncoder().encode(s);
  let binary = "";
  for (const b of bytes) binary += String.fromCharCode(b);
  return btoa(binary);
}

async function startBuild(project) {
  if (!project.source.workerUrl) {
    state.build = { phase: "error", message: "Worker-URL fehlt in projects.json." };
    render();
    return;
  }

  state.build = { phase: "dispatching", startedAt: Date.now() };
  render();

  try {
    const headerText = generateConfigHeader(project, state.config);
    const configB64 = encodeBase64Utf8(headerText);
    const payload = { config_h_b64: configB64 };
    if (captchaToken) payload.turnstile_token = captchaToken;
    const res = await fetch(`${project.source.workerUrl}/build`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });
    if (!res.ok) {
      const errText = await res.text();
      throw new Error(`POST /build: ${res.status} ${errText.slice(0, 200)}`);
    }
    const data = await res.json();
    if (!data.correlation_id) throw new Error("Worker antwortete ohne correlation_id");
    state.build = {
      phase: "polling",
      correlationId: data.correlation_id,
      startedAt: Date.now(),
      lastStatus: "pending",
    };
    render();
    const token = (pollAbort = {});
    pollBuild(project, token);
  } catch (err) {
    console.error(err);
    state.build = { phase: "error", message: err.message };
    render();
  }
}

async function pollBuild(project, token) {
  const id = state.build?.correlationId;
  if (!id) return;
  // Erstes Re-Render kurz nach Start, damit der Timer-String aktualisiert wird.
  let lastRenderAt = Date.now();

  while (pollAbort === token && state.build?.correlationId === id) {
    try {
      const res = await fetch(`${project.source.workerUrl}/status?id=${encodeURIComponent(id)}`);
      const data = await res.json();
      if (pollAbort !== token) return;

      if (data.status === "completed") {
        if (data.conclusion === "success" && data.asset_url) {
          state.build = {
            phase: "success",
            correlationId: id,
            assetUrl: data.asset_url,
            runUrl: data.run_url ?? null,
            releaseUrl: data.release_url ?? null,
          };
        } else {
          state.build = {
            phase: "error",
            message: `Build endete mit '${data.conclusion ?? "unbekannt"}'.`,
            runUrl: data.run_url ?? null,
          };
        }
        render();
        return;
      }

      // Noch nicht fertig — Status aktualisieren.
      state.build = { ...state.build, lastStatus: data.status };
      if (Date.now() - lastRenderAt > 1500) {
        render();
        lastRenderAt = Date.now();
      }
    } catch (err) {
      console.warn("status poll error", err);
      // Bei transienten Fehlern weitermachen.
    }

    // Polling-Intervall: anfangs eng, dann groesser, GitHub-API ist 5000/h.
    const elapsedSec = (Date.now() - state.build.startedAt) / 1000;
    const delay = elapsedSec < 60 ? 5000 : 10000;
    await new Promise((r) => setTimeout(r, delay));
  }
}

// ---------- Render ----------

function render() {
  writeUrlState();
  wizardEl.innerHTML = "";
  setStatus("");
  clearInstallSlot();

  let stepNum = 0;

  // 1: Board
  stepNum++;
  wizardEl.appendChild(renderOptionsStep({
    stepNum,
    stepId: "board",
    label: "Board",
    valueLabel: getBoard()?.name,
    optionsBuilder: () =>
      config.boards.map((b) =>
        makeOption(
          { value: b.id, title: b.name, meta: `${b.projects.length} Projekt(e)` },
          (val) => { clearFrom("board"); state.board = val; render(); },
        ),
      ),
  }));

  // 2: Projekt
  stepNum++;
  wizardEl.appendChild(renderOptionsStep({
    stepNum,
    stepId: "project",
    label: "Projekt",
    valueLabel: getProject()?.name,
    optionsBuilder: () => {
      const board = getBoard();
      if (!board) return [];
      return board.projects.map((p) =>
        makeOption(
          { value: p.id, title: p.name, meta: p.description ?? "" },
          (val) => {
            clearFrom("project");
            state.project = val;
            render();
            const project = getProject();
            if (project?.source.type === "github-release") {
              kickoffVersionLoad(project);
            }
          },
        ),
      );
    },
  }));

  const project = getProject();
  if (!project) return;

  if (project.source.type === "github-release") {
    // 3: Version
    stepNum++;
    const versions = project._versions ?? [];
    const cur = getCurrentVersion(project);
    wizardEl.appendChild(renderOptionsStep({
      stepNum,
      stepId: "version",
      label: "Version",
      valueLabel: cur?.version,
      emptyMsg: "Versionen werden geladen …",
      optionsBuilder: () =>
        versions.map((v, idx) =>
          makeOption(
            {
              value: v.tag,
              title: v.version,
              meta: v.publishedAt ? new Date(v.publishedAt).toLocaleDateString("de-DE") : "",
              badge: idx === 0 ? "Latest" : v.prerelease ? "Pre" : "",
            },
            (val) => {
              clearFrom("version");
              state.version = val;
              const ver = getCurrentVersion(project);
              if (ver && ver.langs.size === 1) state.lang = [...ver.langs.keys()][0];
              render();
            },
          ),
        ),
    }));

    // 4: Sprache (nur wenn mehrere)
    if (state.version) {
      const ver = getCurrentVersion(project);
      if (ver && ver.langs.size > 1) {
        stepNum++;
        wizardEl.appendChild(renderOptionsStep({
          stepNum,
          stepId: "lang",
          label: "Sprache",
          valueLabel: state.lang,
          optionsBuilder: () =>
            [...ver.langs.keys()].sort().map((lang) =>
              makeOption(
                { value: lang, title: lang, meta: ver.langs.get(lang).name },
                (val) => { state.lang = val; render(); },
              ),
            ),
        }));
      }
    }
  } else if (project.source.type === "build-on-demand") {
    // 3: Konfiguration
    stepNum++;
    wizardEl.appendChild(renderConfigStep({ stepNum, project }));
  }

  // Install/Action slot wenn alle Schritte fertig.
  if (activeStepId() === null) {
    if (project.source.type === "github-release") {
      const ver = getCurrentVersion(project);
      const lang = state.lang ?? [...ver.langs.keys()][0];
      renderInstallRelease(project, ver, lang);
    } else if (project.source.type === "manifest") {
      renderInstallManifest(project);
    } else if (project.source.type === "build-on-demand") {
      renderInstallBuildOnDemand(project);
    }
  }
}

// ---------- Activation helpers ----------

async function kickoffVersionLoad(project) {
  try {
    setStatus("Lade Releases …");
    await loadVersions(project);
    setStatus("");
  } catch (err) {
    console.error(err);
    setStatus(`Releases konnten nicht geladen werden: ${err.message}`, "error");
  } finally {
    render();
  }
}

// ---------- Bootstrap ----------

async function applyUrlState() {
  const url = readUrlState();
  if (!url.board) return;
  if (!getBoard(url.board)) return;
  state.board = url.board;

  if (!url.project) return;
  if (!getProject(url.board, url.project)) return;
  state.project = url.project;

  const project = getProject();
  if (project.source.type !== "github-release") return;

  try {
    setStatus("Lade Releases …");
    await loadVersions(project);
    setStatus("");
  } catch (err) {
    console.error(err);
    setStatus(`Releases konnten nicht geladen werden: ${err.message}`, "error");
    return;
  }

  if (!url.version) return;
  const ver = project._versions?.find((v) => v.tag === url.version);
  if (!ver) return;
  state.version = url.version;

  if (url.lang && ver.langs.has(url.lang.toUpperCase())) {
    state.lang = url.lang.toUpperCase();
  } else if (ver.langs.size === 1) {
    state.lang = [...ver.langs.keys()][0];
  }
}

(async () => {
  try {
    const res = await fetch("projects.json", { cache: "no-cache" });
    if (!res.ok) throw new Error(`projects.json: ${res.status}`);
    config = await res.json();
    await applyUrlState();
    render();
  } catch (err) {
    console.error(err);
    setStatus(`Konfiguration konnte nicht geladen werden: ${err.message}`, "error");
  }
})();
