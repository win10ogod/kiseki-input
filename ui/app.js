const form = document.querySelector("#config-form");
const save = document.querySelector("#save");
const status = document.querySelector("#status");
const subtitle = document.querySelector("#subtitle");

const teachFiles = document.querySelector("#teach-files");
const teachVideo = document.querySelector("#teach-video");
const teachAudio = document.querySelector("#teach-audio");
const teachTranscript = document.querySelector("#teach-transcript");
const teachTitle = document.querySelector("#teach-title");
const teachCounts = document.querySelector("#teach-counts");
const frameList = document.querySelector("#frame-list");
const eventList = document.querySelector("#event-list");
const frameImage = document.querySelector("#frame-image");
const videoPlayer = document.querySelector("#video-player");
const audioPlayer = document.querySelector("#audio-player");
const transcriptText = document.querySelector("#transcript-text");
const instructionText = document.querySelector("#instruction-text");
const selectionLabel = document.querySelector("#selection-label");
const playFrames = document.querySelector("#play-frames");
const pauseFrames = document.querySelector("#pause-frames");
const annotationText = document.querySelector("#annotation-text");
const addAnnotation = document.querySelector("#add-annotation");
const downloadAnnotations = document.querySelector("#download-annotations");
const annotationList = document.querySelector("#annotation-list");

let config = null;
let bundle = {
  files: new Map(),
  manifest: null,
  timeline: null,
  events: [],
  actions: [],
  videoKeyframes: null,
  annotations: [],
  frameUrls: new Map(),
  selectedFrame: null,
  selectedEvent: null,
  playTimer: null
};

function field(name) {
  return form.elements[name];
}

function setField(name, value) {
  const control = field(name);
  if (control.type === "checkbox") {
    control.checked = Boolean(value);
  } else {
    control.value = value;
  }
}

function readField(name) {
  const control = field(name);
  if (control.type === "checkbox") return control.checked;
  if (control.type === "number") return Number(control.value);
  return control.value;
}

function populate(nextConfig) {
  config = nextConfig;
  setField("webui.host", config.webui.host);
  setField("webui.port", config.webui.port);
  setField("heartbeat.enabled", config.heartbeat.enabled);
  setField("heartbeat.intervalSeconds", config.heartbeat.intervalSeconds);
  setField("heartbeat.notificationEnabled", config.heartbeat.notificationEnabled);
  setField("heartbeat.message", config.heartbeat.message);
  setField("input.defaultBackend", config.input.defaultBackend);
  setField("input.windowsDriver", config.input.windowsDriver);
  setField("input.linuxDriver", config.input.linuxDriver);
  setField("input.backgroundInputEnabled", config.input.backgroundInputEnabled);
  setField("screenshot.defaultOutputDirectory", config.screenshot.defaultOutputDirectory);
  setField("screenshot.burstFps", config.screenshot.burstFps);
  setField("screenshot.burstFrames", config.screenshot.burstFrames);
  setField("screenshot.format", config.screenshot.format);
  save.disabled = false;
}

function collect() {
  return {
    ...config,
    webui: {
      host: readField("webui.host"),
      port: readField("webui.port")
    },
    heartbeat: {
      enabled: readField("heartbeat.enabled"),
      intervalSeconds: readField("heartbeat.intervalSeconds"),
      notificationEnabled: readField("heartbeat.notificationEnabled"),
      message: readField("heartbeat.message")
    },
    input: {
      defaultBackend: readField("input.defaultBackend"),
      windowsDriver: readField("input.windowsDriver"),
      linuxDriver: readField("input.linuxDriver"),
      backgroundInputEnabled: readField("input.backgroundInputEnabled")
    },
    screenshot: {
      defaultOutputDirectory: readField("screenshot.defaultOutputDirectory"),
      burstFps: readField("screenshot.burstFps"),
      burstFrames: readField("screenshot.burstFrames"),
      format: readField("screenshot.format")
    }
  };
}

async function loadConfig() {
  const response = await fetch("/api/config");
  const body = await response.json();
  if (!response.ok) throw new Error(body.error || "Failed to load configuration");
  populate(body);
  status.textContent = "";
}

async function saveConfig() {
  const response = await fetch("/api/config", {
    method: "PUT",
    headers: {"Content-Type": "application/json"},
    body: JSON.stringify(collect())
  });
  const body = await response.json();
  if (!response.ok) throw new Error(body.error || "Failed to save configuration");
  populate(body);
  status.textContent = "Saved";
}

function setView(id) {
  document.querySelectorAll(".tab").forEach((button) => {
    button.classList.toggle("active", button.dataset.view === id);
  });
  document.querySelectorAll(".view").forEach((view) => {
    view.classList.toggle("active", view.id === id);
  });
  subtitle.textContent = id === "teach-view" ? "Teaching" : "Configuration";
  save.style.display = id === "teach-view" ? "none" : "";
}

function normalizePath(path) {
  return path.replaceAll("\\", "/").replace(/^\/+/, "");
}

function addFileToMap(file) {
  const full = normalizePath(file.webkitRelativePath || file.name);
  bundle.files.set(full, file);
  bundle.files.set(file.name, file);
  const parts = full.split("/");
  if (parts.length > 1) {
    bundle.files.set(parts.slice(1).join("/"), file);
  }
}

function findFile(path) {
  if (!path) return null;
  const wanted = normalizePath(path);
  if (bundle.files.has(wanted)) return bundle.files.get(wanted);
  for (const [name, file] of bundle.files) {
    if (name.endsWith(`/${wanted}`)) return file;
  }
  return null;
}

async function readTextFile(file) {
  if (!file) return "";
  return await file.text();
}

async function readJsonFile(file) {
  const text = await readTextFile(file);
  return JSON.parse(text);
}

function parseEvents(text) {
  return text
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter(Boolean)
    .map((line) => JSON.parse(line));
}

function eventLabel(event) {
  if (!event) return "";
  const actionPrefix = event.actionIndex !== undefined ? `#${event.actionIndex} event ${event.index}` : `#${event.index}`;
  if (event.type === "mouse_move") return `${actionPrefix} ${event.timestampMs}ms move ${event.x},${event.y}`;
  if (event.type === "mouse_button") return `${actionPrefix} ${event.timestampMs}ms ${event.button} ${event.state}`;
  if (event.type === "key") return `${actionPrefix} ${event.timestampMs}ms key ${event.key || event.keyCode} ${event.state}`;
  return `${actionPrefix} ${event.timestampMs || 0}ms ${event.type}`;
}

function frameLabel(frame) {
  return `#${frame.index} ${frame.timestampMs}ms`;
}

function setSelectionLabel() {
  const parts = [];
  if (bundle.selectedFrame !== null) parts.push(`frame ${bundle.selectedFrame}`);
  if (bundle.selectedEvent !== null) parts.push(`event ${bundle.selectedEvent}`);
  selectionLabel.textContent = parts.length ? parts.join(" / ") : "No selection";
  addAnnotation.disabled = parts.length === 0;
}

function renderFrames() {
  frameList.innerHTML = "";
  const frames = bundle.manifest?.keyframes || [];
  for (const frame of frames) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "list-item";
    button.textContent = frameLabel(frame);
    button.dataset.index = String(frame.index);
    button.addEventListener("click", () => selectFrame(frame.index));
    frameList.appendChild(button);
  }
  playFrames.disabled = frames.length === 0;
  pauseFrames.disabled = frames.length === 0;
}

function renderEvents() {
  eventList.innerHTML = "";
  const actions = bundle.actions.length ? bundle.actions : bundle.events;
  for (const event of actions) {
    const index = event.index !== undefined ? event.index : event.actionIndex;
    const button = document.createElement("button");
    button.type = "button";
    button.className = "list-item";
    button.textContent = eventLabel(event);
    button.dataset.index = String(index);
    button.addEventListener("click", () => selectEvent(index));
    eventList.appendChild(button);
  }
}

function markActive(container, index) {
  container.querySelectorAll(".list-item").forEach((item) => {
    item.classList.toggle("active", item.dataset.index === String(index));
  });
}

function selectFrame(index) {
  const frame = (bundle.manifest?.keyframes || []).find((item) => item.index === index);
  if (!frame) return;
  const file = findFile(frame.path);
  if (file) {
    if (!bundle.frameUrls.has(index)) {
      bundle.frameUrls.set(index, URL.createObjectURL(file));
    }
    frameImage.src = bundle.frameUrls.get(index);
    frameImage.alt = frameLabel(frame);
    frameImage.style.display = "block";
  }
  bundle.selectedFrame = index;
  markActive(frameList, index);
  setSelectionLabel();
  renderAnnotations();
}

function selectEvent(index) {
  bundle.selectedEvent = index;
  markActive(eventList, index);
  setSelectionLabel();
  renderAnnotations();
}

function frameDelay(index) {
  const frames = bundle.manifest?.keyframes || [];
  const current = frames[index];
  const next = frames[index + 1];
  if (!current || !next) return 500;
  return Math.max(80, next.timestampMs - current.timestampMs);
}

function playFrameSequence(index = 0) {
  const frames = bundle.manifest?.keyframes || [];
  if (frames.length === 0) return;
  const frame = frames[index % frames.length];
  selectFrame(frame.index);
  bundle.playTimer = window.setTimeout(() => playFrameSequence((index + 1) % frames.length), frameDelay(index));
}

function pauseFrameSequence() {
  if (bundle.playTimer !== null) {
    window.clearTimeout(bundle.playTimer);
    bundle.playTimer = null;
  }
}

function renderInstruction(text) {
  instructionText.textContent = text || "";
}

function transcriptFromJson(json) {
  if (typeof json.text === "string" && json.text.trim()) return json.text;
  if (Array.isArray(json.segments)) {
    return json.segments.map((segment) => segment.text || "").filter(Boolean).join("\n");
  }
  return JSON.stringify(json, null, 2);
}

async function renderTranscript(file) {
  if (!file) {
    transcriptText.textContent = "";
    return;
  }
  const text = await readTextFile(file);
  try {
    transcriptText.textContent = transcriptFromJson(JSON.parse(text));
  } catch {
    transcriptText.textContent = text;
  }
}

function setVideo(file) {
  if (!file) {
    videoPlayer.removeAttribute("src");
    videoPlayer.style.display = "none";
    return;
  }
  videoPlayer.src = URL.createObjectURL(file);
  videoPlayer.style.display = "block";
}

function setAudio(file) {
  if (!file) {
    audioPlayer.removeAttribute("src");
    audioPlayer.style.display = "none";
    return;
  }
  audioPlayer.src = URL.createObjectURL(file);
  audioPlayer.style.display = "block";
}

function renderAnnotations() {
  annotationList.innerHTML = "";
  for (const annotation of bundle.annotations) {
    const item = document.createElement("div");
    item.className = "annotation";
    const target = [];
    if (annotation.frameIndex !== undefined) target.push(`frame ${annotation.frameIndex}`);
    if (annotation.eventIndex !== undefined) target.push(`event ${annotation.eventIndex}`);
    item.innerHTML = `<strong>${target.join(" / ")}</strong><p></p>`;
    item.querySelector("p").textContent = annotation.text;
    annotationList.appendChild(item);
  }
  downloadAnnotations.disabled = bundle.annotations.length === 0;
}

function addCurrentAnnotation() {
  const text = annotationText.value.trim();
  if (!text) return;
  const annotation = {
    id: `annotation-${bundle.annotations.length + 1}`,
    createdAtUtc: new Date().toISOString(),
    text
  };
  if (bundle.selectedFrame !== null) annotation.frameIndex = bundle.selectedFrame;
  if (bundle.selectedEvent !== null) annotation.eventIndex = bundle.selectedEvent;
  bundle.annotations.push(annotation);
  annotationText.value = "";
  renderAnnotations();
}

function downloadCurrentAnnotations() {
  const payload = {
    schemaVersion: 1,
    annotations: bundle.annotations
  };
  const blob = new Blob([`${JSON.stringify(payload, null, 2)}\n`], {type: "application/json"});
  const link = document.createElement("a");
  link.href = URL.createObjectURL(blob);
  link.download = "annotations.json";
  link.click();
  URL.revokeObjectURL(link.href);
}

async function loadTeachingBundle(files) {
  pauseFrameSequence();
  for (const url of bundle.frameUrls.values()) URL.revokeObjectURL(url);
  bundle = {
    files: new Map(),
    manifest: null,
    timeline: null,
    events: [],
    actions: [],
    videoKeyframes: null,
    annotations: [],
    frameUrls: new Map(),
    selectedFrame: null,
    selectedEvent: null,
    playTimer: null
  };
  Array.from(files).forEach(addFileToMap);

  const manifestFile = findFile("manifest.json");
  if (!manifestFile) throw new Error("manifest.json not found");
  bundle.manifest = await readJsonFile(manifestFile);

  const timelineFile = findFile(bundle.manifest.timelineFile || "timeline.json");
  if (timelineFile) bundle.timeline = await readJsonFile(timelineFile);

  const eventsFile = findFile(bundle.manifest.eventsFile || "events.jsonl");
  if (eventsFile) bundle.events = parseEvents(await readTextFile(eventsFile));

  const actionsFile = findFile(bundle.manifest.actionsFile || "actions.json");
  if (actionsFile) {
    const actions = await readJsonFile(actionsFile);
    bundle.actions = Array.isArray(actions.actions) ? actions.actions : [];
  }

  const annotationsFile = findFile(bundle.manifest.annotationsFile || "annotations.json");
  if (annotationsFile) {
    const annotations = await readJsonFile(annotationsFile);
    bundle.annotations = Array.isArray(annotations.annotations) ? annotations.annotations : [];
  }

  const instructionFile = findFile(bundle.manifest.instructionFile || "instruction.txt");
  renderInstruction(await readTextFile(instructionFile));

  setVideo(findFile(bundle.manifest.media?.video));
  setAudio(findFile(bundle.manifest.media?.audio));
  await renderTranscript(findFile(bundle.manifest.media?.transcript));

  const videoKeyframesFile = findFile(bundle.manifest.media?.videoKeyframes);
  if (videoKeyframesFile) {
    bundle.videoKeyframes = await readJsonFile(videoKeyframesFile);
  }

  teachTitle.textContent = bundle.manifest.title || "Recorded teaching";
  const videoFrameCount = Array.isArray(bundle.videoKeyframes?.frames) ? bundle.videoKeyframes.frames.length : 0;
  const videoFrameText = videoFrameCount ? `, ${videoFrameCount} video frames` : "";
  teachCounts.textContent = `${bundle.manifest.keyframes?.length || 0} keyframes, ${bundle.actions.length || bundle.events.length} actions, ${bundle.events.length} events${videoFrameText}`;
  renderFrames();
  renderEvents();
  renderAnnotations();
  if ((bundle.manifest.keyframes || []).length > 0) {
    selectFrame(bundle.manifest.keyframes[0].index);
  } else {
    setSelectionLabel();
  }
  status.textContent = "";
}

document.querySelectorAll(".tab").forEach((button) => {
  button.addEventListener("click", () => setView(button.dataset.view));
});

save.addEventListener("click", () => {
  saveConfig().catch((error) => {
    status.textContent = error.message;
  });
});

teachFiles.addEventListener("change", () => {
  loadTeachingBundle(teachFiles.files).catch((error) => {
    status.textContent = error.message;
  });
});

teachVideo.addEventListener("change", () => {
  setVideo(teachVideo.files[0] || null);
});

teachAudio.addEventListener("change", () => {
  setAudio(teachAudio.files[0] || null);
});

teachTranscript.addEventListener("change", () => {
  renderTranscript(teachTranscript.files[0] || null).catch((error) => {
    status.textContent = error.message;
  });
});

playFrames.addEventListener("click", () => {
  pauseFrameSequence();
  playFrameSequence(0);
});

pauseFrames.addEventListener("click", pauseFrameSequence);
addAnnotation.addEventListener("click", addCurrentAnnotation);
downloadAnnotations.addEventListener("click", downloadCurrentAnnotations);

loadConfig().catch((error) => {
  status.textContent = error.message;
});
