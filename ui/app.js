const form = document.querySelector("#config-form");
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
});
