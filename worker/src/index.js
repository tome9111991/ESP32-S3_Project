// ESP32 Web Flasher Worker
//
// Endpoints:
//   POST /build   { config_h_b64 }  -> 202 { correlation_id }
//   GET  /status?id=<corr_id>       -> 200 { status, conclusion?, asset_url? }
//
// Env (set via wrangler):
//   GH_TOKEN          Secret. Fine-Grained PAT with actions:write + contents:read
//                     auf dem Ziel-Repo.
//   GH_OWNER          z.B. tome9111991
//   GH_REPO           z.B. ESP32-S3_Project
//   WORKFLOW_FILE     Dateiname, z.B. build-guition-klippercrypto.yml
//   ALLOWED_ORIGIN    Erlaubter Origin (z.B. https://tome9111991.github.io)
//                     oder "*" fuer dev/lokal.
//   TURNSTILE_SECRET  Optional. Cloudflare-Turnstile-Secret. Wenn gesetzt,
//                     verlangt /build einen gueltigen turnstile_token im body.

const RELEASE_TAG_PREFIX = "build-";
const FIRMWARE_ASSET_NAME = "firmware.bin";

// Soft-Limit: GitHub Actions inputs sind insgesamt auf ~65 KB begrenzt.
// 60 KB base64 == ~45 KB Klartext, mehr als genug fuer config_private.h.
const MAX_CONFIG_B64_LEN = 60_000;

export default {
  async fetch(req, env, ctx) {
    const url = new URL(req.url);

    if (req.method === "OPTIONS") {
      return preflight(env, req);
    }

    try {
      if (url.pathname === "/build" && req.method === "POST") {
        return withCors(await handleBuild(req, env), env, req);
      }
      if (url.pathname === "/status" && req.method === "GET") {
        return withCors(await handleStatus(url, env), env, req);
      }
      if (url.pathname === "/" || url.pathname === "/health") {
        return withCors(jsonResp({ ok: true }), env, req);
      }
      return withCors(errorResp(404, "not found"), env, req);
    } catch (err) {
      console.error(err);
      return withCors(errorResp(500, err?.message ?? "internal error"), env, req);
    }
  },
};

// ---------- Handlers ----------

async function handleBuild(req, env) {
  let body;
  try {
    body = await req.json();
  } catch {
    return errorResp(400, "body must be JSON");
  }

  const configB64 = body?.config_h_b64;
  if (typeof configB64 !== "string" || configB64.length === 0) {
    return errorResp(400, "config_h_b64 required");
  }
  if (configB64.length > MAX_CONFIG_B64_LEN) {
    return errorResp(413, "config too large");
  }
  if (!/^[A-Za-z0-9+/=\s]+$/.test(configB64)) {
    return errorResp(400, "config_h_b64 must be base64");
  }

  // Turnstile-Verify (nur wenn ein Secret konfiguriert ist, sonst skippen
  // damit lokales wrangler-dev ohne Captcha laeuft).
  if (env.TURNSTILE_SECRET) {
    const token = body?.turnstile_token;
    if (typeof token !== "string" || token.length === 0) {
      return errorResp(403, "turnstile_token required");
    }
    const ip = req.headers.get("CF-Connecting-IP") ?? "";
    const verifyRes = await fetch(
      "https://challenges.cloudflare.com/turnstile/v0/siteverify",
      {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: new URLSearchParams({
          secret: env.TURNSTILE_SECRET,
          response: token,
          remoteip: ip,
        }),
      },
    );
    let verify;
    try {
      verify = await verifyRes.json();
    } catch {
      return errorResp(502, "turnstile verify malformed");
    }
    if (!verify.success) {
      return errorResp(403, `turnstile: ${(verify["error-codes"] ?? []).join(",") || "failed"}`);
    }
  }

  const correlationId = crypto.randomUUID();
  const dispatchRes = await fetch(
    `https://api.github.com/repos/${env.GH_OWNER}/${env.GH_REPO}/actions/workflows/${env.WORKFLOW_FILE}/dispatches`,
    {
      method: "POST",
      headers: ghHeaders(env),
      body: JSON.stringify({
        ref: "main",
        inputs: {
          config_h_b64: configB64.replace(/\s+/g, ""),
          correlation_id: correlationId,
        },
      }),
    },
  );

  if (!dispatchRes.ok) {
    const errText = await dispatchRes.text();
    return errorResp(502, `github dispatch ${dispatchRes.status}: ${truncate(errText, 200)}`);
  }
  return jsonResp({ correlation_id: correlationId }, 202);
}

async function handleStatus(url, env) {
  const id = url.searchParams.get("id");
  // crypto.randomUUID liefert 36 Zeichen (8-4-4-4-12). Akzeptiere konservativ
  // hex+dashes, 32-36 lang, um auch alternative Korrelations-IDs zuzulassen.
  if (!id || !/^[a-f0-9-]{32,36}$/i.test(id)) {
    return errorResp(400, "invalid id");
  }

  const tag = `${RELEASE_TAG_PREFIX}${id}`;

  // 1) Release-Existenz pruefen (Success-Indikator). Spart eine Run-Listung
  //    sobald der Build durch ist.
  const relRes = await fetch(
    `https://api.github.com/repos/${env.GH_OWNER}/${env.GH_REPO}/releases/tags/${tag}`,
    { headers: ghHeaders(env) },
  );
  if (relRes.ok) {
    const rel = await relRes.json();
    const asset = rel.assets?.find((a) => a.name === FIRMWARE_ASSET_NAME);
    return jsonResp({
      status: "completed",
      conclusion: "success",
      asset_url: asset?.browser_download_url ?? null,
      release_url: rel.html_url ?? null,
    });
  }

  // 2) Sonst: nach dem Run mit display_title == build-<id> suchen.
  const runsRes = await fetch(
    `https://api.github.com/repos/${env.GH_OWNER}/${env.GH_REPO}/actions/workflows/${env.WORKFLOW_FILE}/runs?event=workflow_dispatch&per_page=30`,
    { headers: ghHeaders(env) },
  );
  if (!runsRes.ok) {
    return errorResp(502, `github runs ${runsRes.status}`);
  }
  const runs = await runsRes.json();
  const expectedName = `${RELEASE_TAG_PREFIX}${id}`;
  const run = (runs.workflow_runs ?? []).find(
    (r) => r.display_title === expectedName || r.name === expectedName,
  );
  if (!run) {
    return jsonResp({ status: "pending" });
  }
  return jsonResp({
    status: run.status, // queued | in_progress | completed
    conclusion: run.conclusion, // null | success | failure | cancelled | ...
    run_url: run.html_url,
  });
}

// ---------- Helpers ----------

function ghHeaders(env) {
  return {
    Authorization: `Bearer ${env.GH_TOKEN}`,
    Accept: "application/vnd.github+json",
    "User-Agent": "kcwp-flasher-worker",
    "Content-Type": "application/json",
    "X-GitHub-Api-Version": "2022-11-28",
  };
}

function jsonResp(obj, status = 200) {
  return new Response(JSON.stringify(obj), {
    status,
    headers: { "Content-Type": "application/json" },
  });
}

function errorResp(status, message) {
  return jsonResp({ error: message }, status);
}

function truncate(s, n) {
  return s.length > n ? s.slice(0, n) + "…" : s;
}

function corsHeaders(env, req) {
  const origin = req.headers.get("Origin") ?? "";
  const allowed = env.ALLOWED_ORIGIN ?? "*";
  const allowList = allowed.split(",").map((s) => s.trim()).filter(Boolean);
  const allow =
    allowed === "*"
      ? "*"
      : allowList.includes(origin)
        ? origin
        : allowList[0] ?? "null";
  return {
    "Access-Control-Allow-Origin": allow,
    "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
    "Access-Control-Allow-Headers": "Content-Type",
    "Access-Control-Max-Age": "86400",
    Vary: "Origin",
  };
}

function preflight(env, req) {
  return new Response(null, { status: 204, headers: corsHeaders(env, req) });
}

function withCors(resp, env, req) {
  const h = new Headers(resp.headers);
  for (const [k, v] of Object.entries(corsHeaders(env, req))) h.set(k, v);
  return new Response(resp.body, { status: resp.status, headers: h });
}
