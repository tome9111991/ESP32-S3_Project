// Generiert den C-Header aus einem Projekt-Schema + Werte-Objekt.
// Die Gruppen-Labels werden bewusst auf Deutsch (bzw. dem ersten Eintrag des
// Labels) ausgegeben, damit der generierte Code unabhaengig von der UI-Sprache
// reproduzierbar bleibt.

function escapeCString(s) {
  return String(s).replace(/\\/g, "\\\\").replace(/"/g, '\\"');
}

function formatFloat(v) {
  const num = Number(v);
  if (!Number.isFinite(num)) return "0.0f";
  // Sechs Nachkommastellen + f-Suffix, kompatibel zur Beispiel-Datei.
  return num.toFixed(6) + "f";
}

export function generateConfigHeader(project, values) {
  const lines = [];
  lines.push((project.source.preamble ?? "#pragma once\n").trimEnd());
  lines.push("");
  for (const group of project.source.groups ?? []) {
    const groupLabel = typeof group.label === "string"
      ? group.label
      : (group.label?.de ?? group.label?.en ?? "");
    lines.push(`// ${groupLabel}`);
    for (const f of group.fields) {
      const raw = values[f.key];
      let val;
      if (f.type === "float") {
        val = formatFloat(raw);
      } else if (f.type === "boolean") {
        const tVal = f.trueValue ?? 1;
        const fVal = f.falseValue ?? 0;
        val = String(raw === true ? tVal : fVal);
      } else if (f.rawValue === true) {
        // Fuer Compile-Time-Makros wie UI_LANGUAGE_DE keine Quotes schreiben.
        val = String(raw ?? "");
      } else {
        val = `"${escapeCString(raw ?? "")}"`;
      }
      lines.push(`#define ${f.key} ${val}`);
    }
    lines.push("");
  }
  return lines.join("\n");
}

export function downloadConfigHeader(project, values) {
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

// Browser btoa kann nur Latin-1. Fuer UTF-8 (Umlaute in SSID o.ae.) erst
// nach UTF-8 enkodieren, dann base64-encodieren.
export function encodeBase64Utf8(s) {
  const bytes = new TextEncoder().encode(s);
  let binary = "";
  for (const b of bytes) binary += String.fromCharCode(b);
  return btoa(binary);
}
