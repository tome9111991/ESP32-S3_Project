const boardSelect = document.getElementById("board");
const projectSelect = document.getElementById("project");
const installSlot = document.getElementById("install-slot");
const installer = document.getElementById("installer");
const manifestPath = document.getElementById("manifest-path");
const projectInfo = document.getElementById("project-info");
const projectTitle = document.getElementById("project-title");
const projectDescription = document.getElementById("project-description");

let config;

async function loadConfig() {
  const res = await fetch("projects.json", { cache: "no-cache" });
  if (!res.ok) throw new Error(`projects.json: ${res.status}`);
  return res.json();
}

function populateBoards() {
  for (const board of config.boards) {
    const opt = document.createElement("option");
    opt.value = board.id;
    opt.textContent = board.name;
    boardSelect.appendChild(opt);
  }
}

function populateProjects(boardId) {
  projectSelect.innerHTML = "";
  const placeholder = document.createElement("option");
  placeholder.value = "";
  placeholder.disabled = true;
  placeholder.selected = true;
  placeholder.textContent = "– Projekt wählen –";
  projectSelect.appendChild(placeholder);

  const board = config.boards.find((b) => b.id === boardId);
  if (!board) return;

  for (const project of board.projects) {
    const opt = document.createElement("option");
    opt.value = project.id;
    opt.textContent = project.name;
    projectSelect.appendChild(opt);
  }
  projectSelect.disabled = false;
}

function selectProject(boardId, projectId) {
  const board = config.boards.find((b) => b.id === boardId);
  const project = board?.projects.find((p) => p.id === projectId);
  if (!project) {
    installSlot.hidden = true;
    projectInfo.hidden = true;
    return;
  }

  installer.manifest = project.manifest;
  manifestPath.textContent = project.manifest;
  installSlot.hidden = false;

  projectTitle.textContent = project.name;
  projectDescription.textContent = project.description ?? "";
  projectInfo.hidden = !project.description;
}

boardSelect.addEventListener("change", () => {
  populateProjects(boardSelect.value);
  installSlot.hidden = true;
  projectInfo.hidden = true;
});

projectSelect.addEventListener("change", () => {
  selectProject(boardSelect.value, projectSelect.value);
});

(async () => {
  try {
    config = await loadConfig();
    populateBoards();
  } catch (err) {
    console.error(err);
    document.querySelector("main").insertAdjacentHTML(
      "afterbegin",
      `<p style="color:#f87171">Konfiguration konnte nicht geladen werden: ${err.message}</p>`,
    );
  }
})();
