// Liest GitHub-Releases fuer ein Projekt mit source.type = "github-release"
// und mappt die Assets gegen assetTemplate (mit {lang}-Platzhalter) auf
// versionierte Eintraege mit Sprach-Map.

const releaseCache = new Map();
const versionsCache = new Map();

function projectKey(boardId, projectId) {
  return `${boardId}/${projectId}`;
}

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

export async function loadVersions(boardId, project) {
  const key = projectKey(boardId, project.id);
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
