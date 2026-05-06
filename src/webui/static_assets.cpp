#include "webui/static_assets.hpp"

namespace kiseki::webui {

std::string_view index_html() {
    return R"HTML(<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Kiseki Input Configuration</title>
    <link rel="stylesheet" href="/styles.css">
  </head>
  <body>
    <main class="shell">
      <header class="topbar">
        <div>
          <h1>Kiseki Input</h1>
          <p>Configuration</p>
        </div>
        <button id="save" type="button" disabled>Save</button>
      </header>

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
              <option>png</option>
            </select>
          </label>
        </section>
      </form>

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

body {
  margin: 0;
}

.shell {
  max-width: 1120px;
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

p {
  color: #647287;
}

.grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
  gap: 16px;
}

section {
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
button {
  font: inherit;
}

input,
select {
  min-height: 36px;
  border: 1px solid #b8c2d0;
  border-radius: 6px;
  padding: 6px 8px;
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

#status {
  min-height: 20px;
  margin-top: 16px;
  white-space: pre-wrap;
})CSS";
}

std::string_view app_js() {
    return R"JS(const form = document.querySelector("#config-form");
const save = document.querySelector("#save");
const status = document.querySelector("#status");

let config = null;

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

save.addEventListener("click", () => {
  saveConfig().catch((error) => {
    status.textContent = error.message;
  });
});

loadConfig().catch((error) => {
  status.textContent = error.message;
});)JS";
}

}
