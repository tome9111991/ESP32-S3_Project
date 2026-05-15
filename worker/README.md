# kcwp-flasher worker

Cloudflare Worker, der `workflow_dispatch`-Builds fuer den Guition-Build
anstoesst und den Build-Status an die GitHub-Pages-Seite zurueckliefert.

## Endpoints

| Methode | Pfad      | Body / Query             | Antwort |
|---------|-----------|--------------------------|---------|
| POST    | `/build`  | `{ "config_h_b64": "..." }` | `202 { correlation_id }` |
| GET     | `/status` | `?id=<correlation_id>`   | `200 { status, conclusion?, asset_url?, run_url?, release_url? }` |
| GET     | `/health` | -                        | `200 { ok: true }` |

`status` ist einer von:
- `pending` (Run noch nicht in der GitHub-API sichtbar)
- `queued` / `in_progress` (Run laeuft)
- `completed` (mit `conclusion: success` -> `asset_url` ist gesetzt)

## Setup einmalig

Auf einem Rechner mit Node.js ≥ 18:

```bash
cd worker
npm install
npx wrangler login                  # Browser-OAuth zu Cloudflare
```

GitHub PAT anlegen
([Fine-Grained Personal Access Token](https://github.com/settings/personal-access-tokens/new)):

- **Repository access**: nur dieses eine Repo
- **Permissions**:
  - `Actions`: **Read and write**
  - `Contents`: **Read** (für `GET /releases/...`)
  - `Metadata`: **Read** (default, nicht abwaehlbar)
- Ablauf max. 90 Tage (CF erlaubt keine geheime Rotation in Free, also notieren)

Token an den Worker geben:

```bash
npx wrangler secret put GH_TOKEN
# > Paste your secret here: <ghp_...>
```

## Lokal testen

```bash
npx wrangler dev
# Worker hoert auf http://localhost:8787
```

Smoke-Test:

```bash
curl http://localhost:8787/health
# {"ok":true}

curl -X POST http://localhost:8787/build \
  -H "Content-Type: application/json" \
  -d "{\"config_h_b64\":\"$(base64 -w0 ../Guition_JC4827W543/KlipperCryptoWeatherPanel/config_private.example.h)\"}"
# 202 {"correlation_id":"..."}

# Status pollen (am Anfang pending, dann queued/in_progress, am Ende completed):
curl "http://localhost:8787/status?id=<correlation_id>"
```

## Deployen

```bash
npx wrangler deploy
# -> Worker URL kommt in der Ausgabe, z.B.
#    https://kcwp-flasher.<account-subdomain>.workers.dev
```

Anschliessend die `worker.workers.dev`-URL in `docs/projects.json` unter
`source.workerUrl` eintragen.

## Security-Hinweise

- `GH_TOKEN` darf NIE in `wrangler.toml` landen — nur als Secret.
- `ALLOWED_ORIGIN` einschraenken sobald nicht mehr lokal getestet wird.
- Optional: im Cloudflare-Dashboard unter *Security → WAF → Rate Limiting*
  eine Regel anlegen: `POST /build` -> max 10/Minute pro IP. Free-Tier hat
  10 kostenlose Rate-Limit-Regeln.
- `GH_TOKEN` mit minimalem Scope (Repo-spezifisch, nur die zwei Permissions
  oben). Sollte er leaken, kann der Angreifer maximal CI-Minuten verbrennen
  und Releases im Repo manipulieren.

## Rotation Token

```bash
npx wrangler secret put GH_TOKEN   # ueberschreibt den alten Secret
```

Alten PAT auf GitHub revoken nicht vergessen.
