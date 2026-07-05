#include "webui/static_assets.hpp"

namespace kiseki::webui {

std::string_view index_html() {
    return R"HTML(<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Kiseki Input</title>
    <link rel="stylesheet" href="/styles.css">
  </head>
  <body>
    <main class="shell">
      <header class="topbar">
        <div>
          <h1>Kiseki Input</h1>
          <p id="subtitle">Configuration</p>
        </div>
        <div class="top-actions">
          <nav class="tabs" aria-label="Main view">
            <button class="tab active" data-view="config-view" type="button">Config</button>
            <button class="tab" data-view="teach-view" type="button">Teaching</button>
          </nav>
          <button id="save" type="button" disabled>Save</button>
        </div>
      </header>

      <section id="config-view" class="view active">
        <form id="config-form" class="grid">
          <section>
            <h2>WebUI</h2>
            <label>Host <input name="webui.host" autocomplete="off"></label>
            <label>Port <input name="webui.port" type="number" min="1" max="65535"></label>
          </section>

          <section>
            <h2>Heartbeat</h2>
            <label><input name="heartbeat.enabled" type="checkbox"> Enabled</label>
            <label>Interval seconds <input name="heartbeat.intervalSeconds" type="number" min="1"></label>
            <label><input name="heartbeat.notificationEnabled" type="checkbox"> Notifications</label>
            <label>Message <input name="heartbeat.message" autocomplete="off"></label>
          </section>

          <section>
            <h2>Input Defaults</h2>
            <label>Default backend
              <select name="input.defaultBackend">
                <option value="background-window">background-window</option>
                <option value="driver">driver</option>
              </select>
            </label>
            <label>Windows driver
              <select name="input.windowsDriver">
                <option>AnyDriver</option>
                <option>SendInput</option>
                <option>Logitech</option>
                <option>LogitechGHubNew</option>
                <option>Razer</option>
                <option>DD</option>
                <option>MouClassInputInjection</option>
              </select>
            </label>
            <label>Linux driver
              <select name="input.linuxDriver">
                <option>uinput</option>
              </select>
            </label>
            <label><input name="input.backgroundInputEnabled" type="checkbox"> Background input enabled</label>
          </section>

          <section>
            <h2>Screenshot Defaults</h2>
            <label>Output directory <input name="screenshot.defaultOutputDirectory" autocomplete="off"></label>
            <label>Burst FPS <input name="screenshot.burstFps" type="number" min="1" max="240"></label>
            <label>Burst frames <input name="screenshot.burstFrames" type="number" min="1" max="240"></label>
            <label>Format
              <select name="screenshot.format">
                <option>bmp</option>
              </select>
            </label>
          </section>
        </form>
      </section>

      <section id="teach-view" class="view">
        <div class="teach-layout">
          <section class="teach-sidebar">
            <h2>Teaching Bundle</h2>
            <label>Bundle files
              <input id="teach-files" type="file" multiple webkitdirectory>
            </label>
            <label>Video
              <input id="teach-video" type="file" accept="video/*">
            </label>
            <label>Audio
              <input id="teach-audio" type="file" accept="audio/*">
            </label>
            <label>Transcript
              <input id="teach-transcript" type="file" accept=".json,.txt,application/json,text/plain">
            </label>

            <div class="summary">
              <strong id="teach-title">No bundle loaded</strong>
              <span id="teach-counts">0 keyframes, 0 actions, 0 events</span>
            </div>

            <h2>Keyframes</h2>
            <div id="frame-list" class="listbox"></div>

            <h2>Actions</h2>
            <div id="event-list" class="listbox event-list"></div>
          </section>

          <section class="teach-stage">
            <div class="stage-toolbar">
              <button id="play-frames" type="button" disabled>Play</button>
              <button id="pause-frames" type="button" disabled>Pause</button>
              <span id="selection-label">No selection</span>
            </div>
            <div class="frame-canvas">
              <img id="frame-image" alt="">
              <video id="video-player" controls></video>
            </div>

            <div class="media-grid">
              <section>
                <h2>Instruction</h2>
                <pre id="instruction-text"></pre>
              </section>
              <section>
                <h2>Transcript</h2>
                <audio id="audio-player" controls></audio>
                <pre id="transcript-text"></pre>
              </section>
            </div>

            <section class="annotation-panel">
              <h2>Guidance</h2>
              <textarea id="annotation-text" rows="4"></textarea>
              <div class="annotation-actions">
                <button id="add-annotation" type="button" disabled>Add Note</button>
                <button id="download-annotations" type="button" disabled>Export</button>
              </div>
              <div id="annotation-list" class="annotation-list"></div>
            </section>
          </section>
        </div>
      </section>

      <pre id="status" role="status"></pre>
    </main>
    <script src="/app.js"></script>
  </body>
</html>)HTML";
}

std::string_view styles_css() {
    return R"CSS(:root {
  color-scheme: light dark;
  font-family: "Segoe UI", system-ui, sans-serif;
  background: #f4f6f8;
  color: #1c2530;
}

* {
  box-sizing: border-box;
}

body {
  margin: 0;
}

.shell {
  max-width: 1280px;
  margin: 0 auto;
  padding: 24px;
}

.topbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  margin-bottom: 20px;
}

.top-actions {
  display: flex;
  align-items: center;
  gap: 10px;
}

.tabs {
  display: inline-flex;
  border: 1px solid #b8c2d0;
  border-radius: 8px;
  overflow: hidden;
}

h1,
h2,
p {
  margin: 0;
}

h1 {
  font-size: 28px;
  font-weight: 650;
}

h2 {
  font-size: 16px;
  margin-bottom: 14px;
}

p,
#subtitle,
.summary span,
#selection-label {
  color: #647287;
}

.view {
  display: none;
}

.view.active {
  display: block;
}

.grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
  gap: 16px;
}

#config-view section,
.teach-sidebar,
.media-grid section,
.annotation-panel {
  background: #ffffff;
  border: 1px solid #d8dee8;
  border-radius: 8px;
  padding: 16px;
}

label {
  display: grid;
  gap: 6px;
  margin-bottom: 12px;
  font-size: 14px;
}

input,
select,
button,
textarea {
  font: inherit;
}

input,
select,
textarea {
  min-height: 36px;
  border: 1px solid #b8c2d0;
  border-radius: 6px;
  padding: 6px 8px;
  background: #ffffff;
  color: #1c2530;
}

textarea {
  width: 100%;
  resize: vertical;
}

button {
  min-height: 38px;
  border: 0;
  border-radius: 6px;
  padding: 0 16px;
  background: #245fba;
  color: #ffffff;
}

button:disabled {
  background: #9aa7b8;
}

.tab {
  min-height: 36px;
  border-radius: 0;
  background: #ffffff;
  color: #1c2530;
}

.tab.active {
  background: #245fba;
  color: #ffffff;
}

.teach-layout {
  display: grid;
  grid-template-columns: minmax(280px, 340px) minmax(0, 1fr);
  gap: 18px;
  align-items: start;
}

.teach-sidebar {
  position: sticky;
  top: 16px;
  max-height: calc(100vh - 48px);
  overflow: auto;
}

.summary {
  display: grid;
  gap: 4px;
  margin: 12px 0 18px;
}

.listbox {
  display: grid;
  gap: 6px;
  max-height: 220px;
  overflow: auto;
  margin-bottom: 18px;
}

.event-list {
  max-height: 320px;
}

.list-item {
  min-height: 34px;
  width: 100%;
  text-align: left;
  background: #eef2f7;
  color: #1c2530;
}

.list-item.active {
  background: #245fba;
  color: #ffffff;
}

.teach-stage {
  min-width: 0;
}

.stage-toolbar {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 10px;
}

.frame-canvas {
  display: grid;
  gap: 12px;
  min-height: 360px;
  border: 1px solid #d8dee8;
  border-radius: 8px;
  background: #0f1720;
  padding: 12px;
}

#frame-image,
#video-player {
  display: none;
  max-width: 100%;
  max-height: 620px;
  margin: 0 auto;
  object-fit: contain;
}

#video-player,
#audio-player {
  width: 100%;
}

.media-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 16px;
  margin-top: 16px;
}

pre {
  margin: 0;
  white-space: pre-wrap;
  overflow: auto;
}

#instruction-text,
#transcript-text {
  min-height: 120px;
  max-height: 260px;
}

#audio-player {
  display: none;
  margin-bottom: 10px;
}

.annotation-panel {
  margin-top: 16px;
}

.annotation-actions {
  display: flex;
  gap: 10px;
  margin-top: 10px;
}

.annotation-list {
  display: grid;
  gap: 8px;
  margin-top: 12px;
}

.annotation {
  border: 1px solid #d8dee8;
  border-radius: 6px;
  padding: 10px;
  background: #f8fafc;
}

#status {
  min-height: 20px;
  margin-top: 16px;
  white-space: pre-wrap;
}

@media (max-width: 820px) {
  .topbar,
  .top-actions,
  .stage-toolbar {
    align-items: stretch;
    flex-direction: column;
  }

  .teach-layout,
  .media-grid {
    grid-template-columns: 1fr;
  }

  .teach-sidebar {
    position: static;
    max-height: none;
  }
})CSS";
}

std::string_view app_js() {
    return R"JS(const form = document.querySelector("#config-form");
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
});)JS";
}

}
