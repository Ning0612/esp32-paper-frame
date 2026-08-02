(() => {
  "use strict";

  const $ = (selector) => document.querySelector(selector);
  const $$ = (selector) => Array.from(document.querySelectorAll(selector));

  const scanButton = $("#scan-button");
  const scanStatus = $("#scan-status");
  const networkList = $("#network-list");
  const manualToggle = $("#manual-toggle");
  const manualField = $("#manual-field");
  const manualSsid = $("#manual-ssid");
  const selectedSsid = $("#selected-ssid");
  const password = $("#password");
  const passwordToggle = $("#password-toggle");
  const form = $("#wifi-form");
  const saveButton = $("#save-button");
  const saveStatus = $("#save-status");
  const authGate = $("#auth-gate");
  const authForm = $("#auth-form");
  const authTitle = $("#auth-title");
  const authCopy = $("#auth-copy");
  const authPasswordLabel = $("#auth-password-label");
  const authPassword = $("#auth-password");
  const authPasswordToggle = $("#auth-password-toggle");
  const authButton = $("#auth-button");
  const authStatus = $("#auth-status");
  const authenticatedActions = $("#authenticated-actions");
  const logoutButton = $("#logout-button");
  const appShell = $("#app-shell");
  const topNavigation = $("#top-navigation");
  const dashboardView = $("#dashboard-view");
  const wifiView = $("#wifi-view");
  const weatherView = $("#weather-view");
  const imageView = $("#image-view");
  const weatherForm = $("#weather-form");
  const weatherApiKey = $("#weather-api-key");
  const weatherLatitude = $("#weather-latitude");
  const weatherLongitude = $("#weather-longitude");
  const weatherUnits = $("#weather-units");
  const weatherNtpServer = $("#weather-ntp-server");
  const weatherSave = $("#weather-save");
  const weatherStatus = $("#weather-status");
  const environmentView = $("#environment-view");
  const systemView = $("#system-view");
  const systemRefresh = $("#system-refresh");
  const systemReboot = $("#system-reboot");
  const systemRebootStatus = $("#system-reboot-status");
  const systemOtaCheck = $("#system-ota-check");
  const systemOtaUpdate = $("#system-ota-update");
  const systemOtaStatus = $("#system-ota-status");
  const systemEventsList = $("#system-events-list");
  const environmentForm = $("#environment-form");
  const environmentEnabled = $("#environment-enabled");
  const lightEnabled = $("#light-enabled");
  const lightThreshold = $("#light-threshold");
  const awayDuration = $("#away-duration");
  const returnDuration = $("#return-duration");
  const environmentSave = $("#environment-save");
  const environmentStatus = $("#environment-status");
  const imageSourceInput = $("#image-source");
  const imageSourceInfo = $("#image-source-info");
  const imageOrientation = $("#image-orientation");
  const imageFit = $("#image-fit");
  const imageDither = $("#image-dither");
  const imageFilename = $("#image-filename");
  const imageMirrorX = $("#image-mirror-x");
  const imageMirrorY = $("#image-mirror-y");
  const imageRotate = $("#image-rotate");
  const imageTransformButtons = [imageMirrorX, imageMirrorY, imageRotate];
  const imageProcessButton = $("#image-process");
  const imageStatus = $("#image-status");
  const imageOutputDimensions = $("#image-output-dimensions");
  const imageOutputPayload = $("#image-output-payload");
  const imageOutputSize = $("#image-output-size");
  const imageOutputOrientation = $("#image-output-orientation");
  const downloadPfr1 = $("#download-pfr1");
  const uploadPfr1 = $("#upload-pfr1");
  const previewOriginal = $("#preview-original");
  const previewProcessed = $("#preview-processed");
  const previewSixColor = $("#preview-sixcolor");
  const previewFrame = $("#preview-frame");
  const dashboardStatus = $("#dashboard-status");
  const refreshDashboard = $("#refresh-dashboard");
  const themeToggle = $("#theme-toggle");
  const imageLibraryRefresh = $("#image-library-refresh");
  const imageLibraryStatus = $("#image-library-status");
  const imageLibraryList = $("#image-library-list");

  let selected = "";
  let polling = 0;
  let csrfToken = "";
  let currentView = "dashboard";
  let lastRuntime = null;
  let imageSourceRaster = null;
  let imageBaseRaster = null;
  let imageWorkingRaster = null;
  let imageExifOrientation = 1;
  let imageFileName = "paperframe.pfr1";
  let imagePfr1 = null;
  let imageTransformFlags = 0;
  let imageRevision = 0;
  let imageSelectionRevision = 0;
  let activeQuantizeWorker = null;
  let imageLibraryRevision = 0;
  let systemOtaPollTimer = null;
  let systemOtaPollAttemptsRemaining = 0;
  let imageLibraryImages = [];
  const maxSourceBytes = 32 * 1024 * 1024;
  const maxSourcePixels = 16 * 1024 * 1024;
  const scriptReloadPromises = new Map();

  function loadScriptOnce(path) {
    if (scriptReloadPromises.has(path)) {
      return scriptReloadPromises.get(path);
    }
    const promise = new Promise((resolve, reject) => {
      const script = document.createElement("script");
      script.src = path;
      script.async = true;
      script.onload = resolve;
      script.onerror = () => {
        scriptReloadPromises.delete(path);
        reject(new Error(`script_load_failed:${path}`));
      };
      document.head.append(script);
    });
    scriptReloadPromises.set(path, promise);
    return promise;
  }

  async function ensureImageDependency(globalName, scriptPath) {
    if (window[globalName]) return;
    await loadScriptOnce(scriptPath);
    if (!window[globalName]) {
      throw new Error(`${globalName}_unavailable`);
    }
  }

  function setTheme(theme) {
    const dark = theme === "dark";
    document.documentElement.dataset.theme = dark ? "dark" : "light";
    themeToggle.textContent = dark ? "LIGHT" : "DARK";
    themeToggle.setAttribute("aria-pressed", dark ? "true" : "false");
  }

  function loadTheme() {
    let stored = "light";
    try { stored = window.localStorage.getItem("iot-ui-theme") || "light"; } catch {}
    setTheme(stored === "dark" ? "dark" : "light");
  }

  themeToggle.addEventListener("click", () => {
    const next = document.documentElement.dataset.theme === "dark" ? "light" : "dark";
    setTheme(next);
    try { window.localStorage.setItem("iot-ui-theme", next); } catch {}
  });

  function chooseNetwork(ssid) {
    selected = ssid;
    selectedSsid.textContent = ssid || "尚未選擇";
    $$(".network-option").forEach((option) => {
      option.setAttribute("aria-checked", option.dataset.ssid === selected ? "true" : "false");
    });
  }

  function signalLabel(rssi) {
    if (rssi >= -50) return "強";
    if (rssi >= -68) return "中";
    return "弱";
  }

  function renderNetworks(networks) {
    networkList.replaceChildren();
    networks.forEach((network) => {
      const option = document.createElement("button");
      option.type = "button";
      option.className = "network-option";
      option.dataset.ssid = network.ssid;
      option.setAttribute("role", "radio");
      option.setAttribute("aria-checked", network.ssid === selected ? "true" : "false");
      const name = document.createElement("strong");
      name.textContent = network.ssid;
      const detail = document.createElement("small");
      detail.textContent = `${String(network.security || "unknown").toUpperCase()} · ${network.rssi} dBm`;
      const signal = document.createElement("span");
      signal.className = "signal";
      signal.textContent = signalLabel(Number(network.rssi));
      option.append(name, detail, signal);
      option.addEventListener("click", () => {
        manualToggle.checked = false;
        manualField.hidden = true;
        chooseNetwork(network.ssid);
      });
      networkList.append(option);
    });
    if (networks.length === 0) {
      const empty = document.createElement("p");
      empty.className = "status-line";
      empty.textContent = "沒有找到可辨識的網路，可改用手動輸入。";
      networkList.append(empty);
    }
  }

  async function scan(refresh) {
    window.clearTimeout(polling);
    scanButton.disabled = true;
    scanStatus.textContent = "正在掃描附近網路…";
    try {
      const response = await fetch(refresh ? "/api/v1/wifi/scan?refresh=1" : "/api/v1/wifi/scan", { cache: "no-store" });
      const payload = await response.json();
      if (!response.ok && response.status !== 202) throw new Error(payload.error || "scan_failed");
      if (response.status === 202 || payload.data.state === "scanning") {
        polling = window.setTimeout(() => scan(false), 900);
        return;
      }
      renderNetworks(payload.data.networks || []);
      scanStatus.textContent = `找到 ${payload.data.networks.length} 個網路`;
      scanButton.disabled = false;
    } catch {
      scanStatus.textContent = "掃描失敗，請確認仍連著 PaperFrame AP 後重試。";
      scanButton.disabled = false;
    }
  }

  function labelState(value) {
    const labels = {
      ready: "正常", connected: "已連線", provisioning: "配網 AP",
      starting_ap: "啟動 AP", connecting: "連線中", reachable: "可連線",
      unreachable: "無法連線", deep_sleep: "休眠", refreshing: "刷新中",
      queued: "等待刷新", failed: "失敗", unknown: "未知",
    };
    return labels[value] || value || "未知";
  }

  function formatBytes(value) {
    if (value === null || value === undefined || Number(value) === 0) return "未知";
    const bytes = Number(value);
    if (!Number.isFinite(bytes)) return "未知";
    if (bytes >= 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
    return `${Math.round(bytes / 1024)} KB`;
  }

  function formatUptime(value) {
    if (value === null || value === undefined) return "未知";
    const seconds = Math.max(0, Math.floor(Number(value) / 1000));
    if (!Number.isFinite(seconds)) return "未知";
    const days = Math.floor(seconds / 86400);
    const hours = Math.floor((seconds % 86400) / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    return days > 0 ? `${days}d ${hours}h` : `${hours}h ${minutes}m`;
  }

  function renderDevice(data) {
    $("#device-model").textContent = data.model || "未知型號";
    $("#device-firmware").textContent = `韌體 ${data.firmware || "未知"}`;
    $("#device-api").textContent = `API ${data.api_version || "—"}`;
  }

  function renderRuntime(data) {
    lastRuntime = data;
    const network = data.network || {};
    const storage = data.storage || {};
    const display = data.display || {};
    const services = data.services || {};
    const carousel = data.carousel || {};
    $("#dashboard-wifi").textContent = labelState(network.wifi);
    $("#dashboard-internet").textContent = `Internet：${labelState(network.internet)}`;
    $("#dashboard-sntp").textContent = `SNTP：${labelState(network.sntp)}`;
    $("#dashboard-uptime").textContent = formatUptime(data.uptime_ms);
    $("#dashboard-sequence").textContent = `snapshot ${data.sequence ?? "—"}`;
    $("#dashboard-carousel").textContent = carousel.refresh_minutes == null ? "輪播：尚未提供" : `輪播：每 ${carousel.refresh_minutes} 分鐘`;
    $("#display-state").textContent = labelState(display.state);
    $("#display-queue").textContent = display.queued_count == null ? "未知" : `${display.queued_count} 件`;
    $("#display-last").textContent = display.last_outcome ? labelState(display.last_outcome) : "尚未刷新";
    $("#flash-capacity").textContent = formatBytes(storage.flash_bytes);
    $("#psram-capacity").textContent = formatBytes(storage.psram_bytes);
    $("#webfs-capacity").textContent = storage.webfs_total_bytes == null ? "未知" : `${formatBytes(storage.webfs_used_bytes)} / ${formatBytes(storage.webfs_total_bytes)}`;
    $("#imagefs-capacity").textContent = storage.imagefs_total_bytes == null ? "未知" : `${formatBytes(storage.imagefs_used_bytes)} / ${formatBytes(storage.imagefs_total_bytes)}`;
    const serviceValues = Object.values(services).map(labelState);
    $("#service-state").textContent = serviceValues.length ? serviceValues.join(" · ") : "未知";
    const weatherLabels = { available: "可用", stale: "過期快取", unavailable: "尚未提供" };
    $("#weather-state").textContent = weatherLabels[(data.weather || {}).state] || "未知";
    $("#sensor-state").textContent = data.sensors && data.sensors.temperature_c != null
      ? `${data.sensors.temperature_c} °C`
      : labelSensorStatus((data.sensors || {}).environment_status);
    $("#dashboard-current-image").textContent = carousel.current_image == null
      ? "尚未輪播"
      : `圖片 #${carousel.current_image}`;
    $("#dashboard-next-refresh").textContent = carousel.next_refresh_ms == null
      ? "未知"
      : `${formatUptime(carousel.next_refresh_ms)} 後`;
  }

  async function loadDashboard() {
    dashboardStatus.textContent = "正在讀取 runtime snapshot…";
    try {
      const [deviceResponse, statusResponse] = await Promise.all([
        fetch("/api/v1/device", { cache: "no-store" }),
        fetch("/api/v1/status", { cache: "no-store" }),
      ]);
      const devicePayload = await deviceResponse.json();
      const statusPayload = await statusResponse.json();
      if (deviceResponse.ok && devicePayload.data) renderDevice(devicePayload.data);
      if (statusResponse.status === 401) {
        showAuthForm(true);
        return null;
      }
      if (statusResponse.ok && statusPayload.data) {
        renderRuntime(statusPayload.data);
        dashboardStatus.textContent = `已更新 snapshot ${statusPayload.data.sequence ?? "—"}`;
        return statusPayload.data;
      }
      dashboardStatus.textContent = "runtime snapshot 尚未提供，未知欄位保持空值。";
      return null;
    } catch {
      dashboardStatus.textContent = "無法讀取裝置狀態；請確認仍連著 PaperFrame。";
      return null;
    }
  }

  // Coordinate picker for the weather form's latitude/longitude fields.
  // Online: real OpenStreetMap XYZ raster tiles with a fixed center pin
  // (drag the map to pan, +/- to zoom). Offline (tile probe fails, or
  // navigator.onLine is false): falls back to a canvas-drawn equirectangular
  // graticule with a free-form draggable pin, so the picker keeps working
  // without any external asset or CDN dependency (CLAUDE.md offline
  // requirement) -- see ADR-0014.
  const weatherMap = (() => {
    const TILE_SIZE = 256;
    const MIN_ZOOM = 0;
    const MAX_ZOOM = 12;
    // Web Mercator's usable latitude range; used everywhere centerLat is
    // set or clamped (online tiles, offline canvas, and setCoordinates())
    // so the number inputs, the fixed center pin, and the offline marker
    // never disagree about what a stored latitude actually represents.
    const MAX_LAT = 85.0511;
    const mapEl = $("#weather-map");
    const tilesLayer = $("#weather-map-tiles");
    const canvas = $("#weather-map-canvas");
    const pin = $("#weather-map-pin");
    const zoomInButton = $("#weather-map-zoom-in");
    const zoomOutButton = $("#weather-map-zoom-out");
    const modeLabel = $("#weather-map-mode");
    const attribution = $("#weather-map-attribution");

    const state = {
      online: null,
      zoom: 4,
      centerLat: 25.033,
      centerLon: 121.565,
      dragging: false,
      dragStartX: 0,
      dragStartY: 0,
      dragStartLat: 0,
      dragStartLon: 0,
    };

    const clamp = (value, min, max) => Math.min(max, Math.max(min, value));
    // Wrap into [-180, 180) so panning across the antimeridian can't leave
    // centerLon (and therefore the saved longitude) out of range; renderTiles()
    // already wraps tile X independently, but the stored/synced value needs
    // its own wrap.
    const normalizeLon = (lon) => ((((lon + 180) % 360) + 360) % 360) - 180;

    function lonToWorldX(lon, zoom) {
      return ((lon + 180) / 360) * TILE_SIZE * 2 ** zoom;
    }
    function latToWorldY(lat, zoom) {
      const rad = (clamp(lat, -MAX_LAT, MAX_LAT) * Math.PI) / 180;
      return (
        ((1 - Math.log(Math.tan(rad) + 1 / Math.cos(rad)) / Math.PI) / 2) *
        TILE_SIZE *
        2 ** zoom
      );
    }
    function worldXToLon(x, zoom) {
      return (x / (TILE_SIZE * 2 ** zoom)) * 360 - 180;
    }
    function worldYToLat(y, zoom) {
      const n = Math.PI - (2 * Math.PI * y) / (TILE_SIZE * 2 ** zoom);
      return (180 / Math.PI) * Math.atan(0.5 * (Math.exp(n) - Math.exp(-n)));
    }

    function syncInputsFromCenter() {
      weatherLatitude.value = Math.round(state.centerLat * 1e6);
      weatherLongitude.value = Math.round(state.centerLon * 1e6);
    }

    function renderTiles() {
      const width = mapEl.clientWidth;
      const height = mapEl.clientHeight;
      if (width === 0 || height === 0) return;
      const centerX = lonToWorldX(state.centerLon, state.zoom);
      const centerY = latToWorldY(state.centerLat, state.zoom);
      const originX = centerX - width / 2;
      const originY = centerY - height / 2;
      const tileCount = 2 ** state.zoom;
      const firstTileX = Math.floor(originX / TILE_SIZE);
      const firstTileY = Math.floor(originY / TILE_SIZE);
      const tilesX = Math.ceil(width / TILE_SIZE) + 2;
      const tilesY = Math.ceil(height / TILE_SIZE) + 2;
      tilesLayer.textContent = "";
      for (let ty = 0; ty < tilesY; ty += 1) {
        const gy = firstTileY + ty;
        if (gy < 0 || gy >= tileCount) continue;
        for (let tx = 0; tx < tilesX; tx += 1) {
          const gx = firstTileX + tx;
          const wrappedX = ((gx % tileCount) + tileCount) % tileCount;
          const img = document.createElement("img");
          img.src = `https://tile.openstreetmap.org/${state.zoom}/${wrappedX}/${gy}.png`;
          img.alt = "";
          img.draggable = false;
          img.style.left = `${gx * TILE_SIZE - originX}px`;
          img.style.top = `${gy * TILE_SIZE - originY}px`;
          tilesLayer.appendChild(img);
        }
      }
    }

    function drawGraticule() {
      const rect = mapEl.getBoundingClientRect();
      const dpr = window.devicePixelRatio || 1;
      canvas.width = rect.width * dpr;
      canvas.height = rect.height * dpr;
      canvas.style.width = `${rect.width}px`;
      canvas.style.height = `${rect.height}px`;
      const ctx = canvas.getContext("2d");
      const styles = getComputedStyle(document.documentElement);
      const surfaceColor = styles.getPropertyValue("--surface-strong").trim() || "#fffdf5";
      const mutedColor = styles.getPropertyValue("--muted").trim() || "#686355";
      const lineColor = styles.getPropertyValue("--line").trim() || "#25241f";
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.fillStyle = surfaceColor;
      ctx.fillRect(0, 0, rect.width, rect.height);
      ctx.strokeStyle = `${mutedColor}66`;
      ctx.lineWidth = 1;
      ctx.font = "10px Consolas, monospace";
      ctx.fillStyle = mutedColor;
      for (let lon = -180; lon <= 180; lon += 30) {
        const x = ((lon + 180) / 360) * rect.width;
        ctx.beginPath();
        ctx.moveTo(x, 0);
        ctx.lineTo(x, rect.height);
        ctx.stroke();
        ctx.fillText(`${lon}°`, Math.min(x + 3, rect.width - 26), 11);
      }
      for (let lat = -90; lat <= 90; lat += 30) {
        const y = ((90 - lat) / 180) * rect.height;
        ctx.beginPath();
        ctx.moveTo(0, y);
        ctx.lineTo(rect.width, y);
        ctx.stroke();
        ctx.fillText(`${lat}°`, 2, Math.max(y - 3, 11));
      }
      ctx.strokeStyle = "#d8483d";
      ctx.lineWidth = 2;
      const zeroX = (180 / 360) * rect.width;
      ctx.beginPath();
      ctx.moveTo(zeroX, 0);
      ctx.lineTo(zeroX, rect.height);
      ctx.stroke();
      const zeroY = (90 / 180) * rect.height;
      ctx.beginPath();
      ctx.moveTo(0, zeroY);
      ctx.lineTo(rect.width, zeroY);
      ctx.stroke();

      const markerX = ((state.centerLon + 180) / 360) * rect.width;
      const markerY = ((90 - state.centerLat) / 180) * rect.height;
      ctx.fillStyle = "#d8483d";
      ctx.strokeStyle = lineColor;
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.arc(markerX, markerY, 7, 0, Math.PI * 2);
      ctx.fill();
      ctx.stroke();
    }

    function render() {
      if (state.online) renderTiles();
      else if (state.online === false) drawGraticule();
    }

    function updateFromCanvasEvent(event) {
      const rect = canvas.getBoundingClientRect();
      // A zero-size rect (layout not settled yet, e.g. right as the view
      // is unhidden) would divide by zero below; bail out rather than
      // writing NaN into state and the number inputs.
      if (rect.width <= 0 || rect.height <= 0) return;
      const x = event.clientX - rect.left;
      const y = event.clientY - rect.top;
      const lon = clamp((x / rect.width) * 360 - 180, -180, 180);
      const lat = clamp(90 - (y / rect.height) * 180, -MAX_LAT, MAX_LAT);
      if (!Number.isFinite(lon) || !Number.isFinite(lat)) return;
      state.centerLon = lon;
      state.centerLat = lat;
      drawGraticule();
      syncInputsFromCenter();
    }

    function enterOnlineMode() {
      if (state.online) return;
      state.online = true;
      modeLabel.textContent = "線上地圖";
      canvas.hidden = true;
      tilesLayer.hidden = false;
      pin.hidden = false;
      zoomInButton.hidden = false;
      zoomOutButton.hidden = false;
      attribution.hidden = false;
      renderTiles();
    }

    function enterOfflineMode() {
      if (state.online === false) return;
      state.online = false;
      modeLabel.textContent = "離線模式（經緯度格線）";
      tilesLayer.hidden = true;
      pin.hidden = true;
      zoomInButton.hidden = true;
      zoomOutButton.hidden = true;
      attribution.hidden = true;
      canvas.hidden = false;
      drawGraticule();
    }

    function probeConnectivity() {
      if (!navigator.onLine) {
        enterOfflineMode();
        return;
      }
      const probe = new Image();
      probe.addEventListener("load", enterOnlineMode, { once: true });
      probe.addEventListener("error", enterOfflineMode, { once: true });
      probe.src = "https://tile.openstreetmap.org/0/0/0.png";
    }

    mapEl.addEventListener("pointerdown", (event) => {
      // The zoom buttons and the attribution link are DOM descendants of
      // mapEl (positioned as overlays), so their pointerdown bubbles up
      // here too. Without this guard, starting a click on them also
      // starts a map drag and steals the pointer via setPointerCapture
      // below, so the button/link never gets a working click.
      if (!state.online || event.target.closest("button, a")) return;
      state.dragging = true;
      state.dragStartX = event.clientX;
      state.dragStartY = event.clientY;
      state.dragStartLat = state.centerLat;
      state.dragStartLon = state.centerLon;
      mapEl.setPointerCapture(event.pointerId);
    });
    mapEl.addEventListener("pointermove", (event) => {
      // state.dragging is shared with the offline canvas's own drag
      // handling below; without this online check, a pointermove that
      // bubbles up from a canvas drag (offline mode) would still run
      // this branch using stale/zeroed dragStart values and call
      // renderTiles() -- i.e. issue real OpenStreetMap tile requests
      // while the picker is supposed to be fully offline.
      if (!state.online || !state.dragging) return;
      const dx = event.clientX - state.dragStartX;
      const dy = event.clientY - state.dragStartY;
      const originX = lonToWorldX(state.dragStartLon, state.zoom) - dx;
      const originY = latToWorldY(state.dragStartLat, state.zoom) - dy;
      state.centerLon = normalizeLon(worldXToLon(originX, state.zoom));
      state.centerLat = clamp(worldYToLat(originY, state.zoom), -MAX_LAT, MAX_LAT);
      renderTiles();
    });
    const endDrag = () => {
      if (!state.dragging) return;
      state.dragging = false;
      syncInputsFromCenter();
    };
    mapEl.addEventListener("pointerup", endDrag);
    mapEl.addEventListener("pointercancel", endDrag);

    canvas.addEventListener("pointerdown", (event) => {
      if (state.online) return;
      event.stopPropagation();
      state.dragging = true;
      canvas.setPointerCapture(event.pointerId);
      updateFromCanvasEvent(event);
    });
    canvas.addEventListener("pointermove", (event) => {
      if (state.online || !state.dragging) return;
      event.stopPropagation();
      updateFromCanvasEvent(event);
    });
    const endCanvasDrag = (event) => {
      state.dragging = false;
      event.stopPropagation();
    };
    canvas.addEventListener("pointerup", endCanvasDrag);
    canvas.addEventListener("pointercancel", endCanvasDrag);

    zoomInButton.addEventListener("click", () => {
      state.zoom = clamp(state.zoom + 1, MIN_ZOOM, MAX_ZOOM);
      renderTiles();
    });
    zoomOutButton.addEventListener("click", () => {
      state.zoom = clamp(state.zoom - 1, MIN_ZOOM, MAX_ZOOM);
      renderTiles();
    });
    window.addEventListener("resize", () => {
      if (!weatherView.hidden) render();
    });

    return {
      // syncBack controls whether the (possibly clamped/normalized) value
      // gets written back into the number inputs. loadWeatherConfig()
      // passes false: positioning the map to show what the backend has
      // saved should never itself alter what would be submitted on the
      // next unrelated save (e.g. just changing the NTP server). The
      // manual lat/lng "change" handler passes true, since there the user
      // just edited that exact field and immediate clamp feedback is
      // expected. Drag/click on the map itself always calls
      // syncInputsFromCenter() directly (not through here), since that is
      // inherently a user-driven coordinate edit.
      setCoordinates(latE6, lonE6, { syncBack = false } = {}) {
        const lat = Number(latE6);
        const lon = Number(lonE6);
        if (!Number.isFinite(lat) || !Number.isFinite(lon)) return;
        // Clamp to the same +/-85.0511 Web Mercator limit renderTiles()
        // uses, not +/-90 (no real weather location is this far
        // poleward anyway).
        state.centerLat = clamp(lat / 1e6, -MAX_LAT, MAX_LAT);
        state.centerLon = normalizeLon(clamp(lon / 1e6, -180, 180));
        if (syncBack) syncInputsFromCenter();
        if (state.online === null) probeConnectivity();
        else render();
      },
    };
  })();

  const syncMapFromInputs = () => {
    weatherMap.setCoordinates(weatherLatitude.value, weatherLongitude.value, {
      syncBack: true,
    });
  };
  weatherLatitude.addEventListener("change", syncMapFromInputs);
  weatherLongitude.addEventListener("change", syncMapFromInputs);

  async function loadWeatherConfig() {
    weatherStatus.textContent = "正在讀取天氣設定…";
    try {
      const response = await fetch("/api/v1/weather/config", { cache: "no-store" });
      const payload = await response.json();
      if (response.status === 401) {
        showAuthForm(true);
        return;
      }
      if (!response.ok || !payload.data || !payload.data.weather) {
        throw new Error(payload.error || "weather_config_failed");
      }
      const weather = payload.data.weather;
      weatherLatitude.value = weather.latitude_e6 ?? "";
      weatherLongitude.value = weather.longitude_e6 ?? "";
      weatherUnits.value = weather.units || "metric";
      weatherNtpServer.value = weather.ntp_server || "pool.ntp.org";
      weatherApiKey.value = "";
      $("#weather-config-state").textContent = weather.configured ? "已保存" : "使用預設值";
      $("#weather-api-key-state").textContent = weather.api_key_set ? "已設定（遮罩）" : "尚未設定";
      weatherStatus.textContent = "設定已載入；留白 API key 會保留原值。";
      weatherMap.setCoordinates(weatherLatitude.value, weatherLongitude.value);
    } catch (error) {
      weatherStatus.className = "save-status error";
      weatherStatus.textContent = `天氣設定讀取失敗：${error.message || "請稍後重試"}`;
    }
  }

  weatherForm.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!csrfToken) return;
    const values = {
      latitude_e6: weatherLatitude.value.trim(),
      longitude_e6: weatherLongitude.value.trim(),
      units: weatherUnits.value,
      ntp_server: weatherNtpServer.value.trim(),
    };
    if (!values.latitude_e6 || !values.longitude_e6 || !values.ntp_server) {
      weatherStatus.className = "save-status error";
      weatherStatus.textContent = "請完整填寫經緯度與 NTP server。";
      return;
    }
    weatherSave.disabled = true;
    weatherStatus.className = "save-status";
    weatherStatus.textContent = "正在保存天氣設定…";
    try {
      if (weatherApiKey.value.trim()) values.api_key = weatherApiKey.value.trim();
      const response = await fetch("/api/v1/weather/config", {
        method: "POST",
        headers: {
          "Content-Type": "application/x-www-form-urlencoded",
          "X-CSRF-Token": csrfToken,
        },
        body: new URLSearchParams(values).toString(),
      });
      const payload = await response.json();
      if (response.status === 401) {
        showAuthForm(true);
        return;
      }
      if (!response.ok || !payload.ok) throw new Error(payload.error || "weather_save_failed");
      weatherStatus.className = "save-status success";
      weatherStatus.textContent = "天氣設定已保存；HTTPS worker 接入後會依此設定更新。";
      await loadWeatherConfig();
    } catch (error) {
      weatherStatus.className = "save-status error";
      weatherStatus.textContent = `保存失敗：${error.message || "請稍後重試"}`;
    } finally {
      weatherSave.disabled = false;
    }
  });

  function labelSensorStatus(status) {
    const labels = {
      disabled: "未啟用", probing: "偵測中", online: "正常",
      stale: "資料過舊", not_detected: "未偵測到", error: "錯誤",
      saturated: "訊號飽和", unknown: "未知", present: "在場", away: "離席",
    };
    return labels[status] || status || "未知";
  }

  async function loadEnvironmentConfig() {
    environmentStatus.textContent = "正在讀取感測器設定…";
    try {
      const response = await fetch("/api/v1/sensors/config", { cache: "no-store" });
      const payload = await response.json();
      if (response.status === 401) {
        showAuthForm(true);
        return;
      }
      if (!response.ok || !payload.data || !payload.data.sensors) {
        throw new Error(payload.error || "sensor_config_failed");
      }
      const sensors = payload.data.sensors;
      environmentEnabled.checked = !!sensors.environment_enabled;
      lightEnabled.checked = !!sensors.light_enabled;
      lightThreshold.value = sensors.light_threshold ?? 2000;
      awayDuration.value = sensors.away_duration_s ?? 180;
      returnDuration.value = sensors.return_duration_s ?? 30;
      environmentStatus.textContent = "設定已載入。";
    } catch (error) {
      environmentStatus.className = "save-status error";
      environmentStatus.textContent = `感測器設定讀取失敗：${error.message || "請稍後重試"}`;
    }
    await loadSensorReadings();
  }

  async function loadSensorReadings() {
    try {
      const response = await fetch("/api/v1/sensors", { cache: "no-store" });
      const payload = await response.json();
      if (!response.ok || !payload.data) return;
      const environment = payload.data.environment || {};
      const light = payload.data.light || {};
      $("#environment-reading-status").textContent = labelSensorStatus(environment.status);
      $("#environment-temperature").textContent = environment.temperature_c == null ? "—" : `${environment.temperature_c} °C`;
      $("#environment-humidity").textContent = environment.humidity_percent == null ? "—" : `${environment.humidity_percent} %`;
      const today = environment.today || {};
      $("#environment-today-temperature").textContent = today.temperature_min_c == null
        ? "—"
        : `${today.temperature_min_c} / ${today.temperature_avg_c} / ${today.temperature_max_c} °C`;
      $("#light-reading-status").textContent = labelSensorStatus(light.status);
      $("#light-raw").textContent = light.raw == null ? "—" : `${light.raw}`;
      $("#presence-state").textContent = labelSensorStatus(payload.data.presence);
    } catch (error) {
      // Live readings are a best-effort overlay; config load already
      // reported an error if the device is unreachable.
    }
  }

  environmentForm.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!csrfToken) return;
    const values = {
      light_threshold: lightThreshold.value.trim(),
      away_duration_s: awayDuration.value.trim(),
      return_duration_s: returnDuration.value.trim(),
    };
    if (environmentEnabled.checked) values.environment_enabled = "on";
    if (lightEnabled.checked) values.light_enabled = "on";
    environmentSave.disabled = true;
    environmentStatus.className = "save-status";
    environmentStatus.textContent = "正在保存感測器設定…";
    try {
      const response = await fetch("/api/v1/sensors/config", {
        method: "POST",
        headers: {
          "Content-Type": "application/x-www-form-urlencoded",
          "X-CSRF-Token": csrfToken,
        },
        body: new URLSearchParams(values).toString(),
      });
      const payload = await response.json();
      if (response.status === 401) {
        showAuthForm(true);
        return;
      }
      if (!response.ok || !payload.ok) throw new Error(payload.error || "sensor_save_failed");
      environmentStatus.className = "save-status success";
      environmentStatus.textContent = "感測器設定已保存。";
      await loadEnvironmentConfig();
    } catch (error) {
      environmentStatus.className = "save-status error";
      environmentStatus.textContent = `保存失敗：${error.message || "請稍後重試"}`;
    } finally {
      environmentSave.disabled = false;
    }
  });

  function formatImageSize(bytes) {
    if (!Number.isFinite(bytes)) return "未知";
    if (bytes >= 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
    return `${Math.max(1, Math.ceil(bytes / 1024))} KB`;
  }

  function renderImageLibrary(images) {
    imageLibraryImages = images.slice();
    imageLibraryList.replaceChildren();
    if (images.length === 0) {
      const empty = document.createElement("p");
      empty.className = "image-library-empty";
      empty.textContent = "裝置目前沒有可用的 PFR1 圖片。";
      imageLibraryList.append(empty);
      return;
    }
    images.forEach((image, index) => {
      const row = document.createElement("article");
      row.className = "image-library-row";

      const copy = document.createElement("div");
      copy.className = "image-library-copy";
      const name = document.createElement("strong");
      name.className = "image-library-name";
      name.textContent = image.name || "未命名圖片";
      const meta = document.createElement("small");
      meta.className = "image-library-meta";
      const dimensions = Number.isFinite(Number(image.width)) && Number.isFinite(Number(image.height))
        ? `${image.width} × ${image.height}`
        : "尺寸未知";
      const orientation = image.orientation === "portrait" ? "直向" : "橫向";
      meta.textContent = `#${index + 1} · ${dimensions} · ${orientation} · ${formatImageSize(Number(image.file_bytes))}`;
      copy.append(name, meta);

      const states = document.createElement("div");
      states.className = "image-library-states";
      if (image.current) {
        const current = document.createElement("span");
        current.className = "image-library-state current";
        current.textContent = "目前";
        states.append(current);
      }
      if (!image.enabled) {
        const disabled = document.createElement("span");
        disabled.className = "image-library-state disabled";
        disabled.textContent = "停用";
        states.append(disabled);
      }
      if (image.corrupt) {
        const corrupt = document.createElement("span");
        corrupt.className = "image-library-state corrupt";
        corrupt.textContent = "損壞";
        states.append(corrupt);
      }
      if (states.childElementCount > 0) copy.append(states);

      const actions = document.createElement("div");
      actions.className = "image-library-actions";
      if (image.corrupt || !image.name) {
        const unavailable = document.createElement("span");
        unavailable.className = "field-hint";
        unavailable.textContent = image.corrupt ? "檔案無法下載" : "缺少檔名";
        actions.append(unavailable);
      } else {
        const download = document.createElement("a");
        download.className = "plain-button";
        download.href = `/api/v1/images/${encodeURIComponent(image.name)}/download`;
        download.download = image.name;
        download.textContent = "下載 PFR1";
        actions.append(download);
      }
      if (image.name && image.enabled && !image.corrupt && !image.current) {
        const activate = document.createElement("button");
        activate.className = "plain-button";
        activate.type = "button";
        activate.dataset.imageAction = "activate";
        activate.dataset.imageName = image.name;
        activate.textContent = "設為目前";
        actions.append(activate);
      }
      if (image.name) {
        const remove = document.createElement("button");
        remove.className = "plain-button danger-button";
        remove.type = "button";
        remove.dataset.imageAction = "remove";
        remove.dataset.imageName = image.name;
        remove.textContent = "刪除";
        actions.append(remove);
      }
      if (image.name) {
        const moveUp = document.createElement("button");
        moveUp.className = "plain-button order-button";
        moveUp.type = "button";
        moveUp.dataset.imageAction = "move-up";
        moveUp.dataset.imageName = image.name;
        moveUp.disabled = index === 0;
        moveUp.textContent = "↑";
        actions.append(moveUp);
        const moveDown = document.createElement("button");
        moveDown.className = "plain-button order-button";
        moveDown.type = "button";
        moveDown.dataset.imageAction = "move-down";
        moveDown.dataset.imageName = image.name;
        moveDown.disabled = index === images.length - 1;
        moveDown.textContent = "↓";
        actions.append(moveDown);
      }
      row.append(copy, actions);
      imageLibraryList.append(row);
    });
  }

  async function mutateImageLibrary(path, method) {
    if (!csrfToken) return;
    imageLibraryStatus.textContent = "正在更新裝置圖片庫…";
    try {
      const response = await fetch(path, {
        method,
        headers: { "X-CSRF-Token": csrfToken },
      });
      const payload = await response.json();
      if (response.status === 401) {
        showAuthForm(true);
        return;
      }
      if (!response.ok || !payload.ok) {
        if (payload.catalog_committed) await loadImageLibrary();
        throw new Error(payload.error || "image_mutation_failed");
      }
      await loadImageLibrary();
    } catch (error) {
      imageLibraryStatus.textContent = `圖片庫更新失敗：${error.message || "請稍後重試"}`;
    }
  }

  async function reorderImage(name, delta) {
    const index = imageLibraryImages.findIndex((image) => image.name === name);
    const target = index + delta;
    if (index < 0 || target < 0 || target >= imageLibraryImages.length || !csrfToken) return;
    const next = imageLibraryImages.slice();
    [next[index], next[target]] = [next[target], next[index]];
    imageLibraryStatus.textContent = "正在儲存輪播順序…";
    try {
      const response = await fetch("/api/v1/images/order", {
        method: "PUT",
        headers: {
          "Content-Type": "application/json",
          "X-CSRF-Token": csrfToken,
        },
        body: JSON.stringify({ ids: next.map((image) => Number(image.id)) }),
      });
      const payload = await response.json();
      if (response.status === 401) {
        showAuthForm(true);
        return;
      }
      if (!response.ok || !payload.ok) throw new Error(payload.error || "image_order_failed");
      await loadImageLibrary();
    } catch (error) {
      imageLibraryStatus.textContent = `輪播順序更新失敗：${error.message || "請稍後重試"}`;
    }
  }

  async function loadImageLibrary() {
    const revision = ++imageLibraryRevision;
    imageLibraryRefresh.disabled = true;
    imageLibraryStatus.textContent = "正在讀取裝置 catalog…";
    try {
      const response = await fetch("/api/v1/images", { cache: "no-store" });
      const payload = await response.json();
      if (revision !== imageLibraryRevision) return;
      if (response.status === 401) {
        showAuthForm(true);
        return;
      }
      if (!response.ok || !payload.data || !Array.isArray(payload.data.images)) {
        throw new Error(payload.error || "image_library_failed");
      }
      const images = payload.data.images;
      renderImageLibrary(images);
      imageLibraryStatus.textContent = `已載入 ${images.length} 張裝置圖片`;
    } catch {
      if (revision !== imageLibraryRevision) return;
      imageLibraryList.replaceChildren();
      imageLibraryStatus.textContent = "圖片庫讀取失敗，請確認仍連著 PaperFrame 後重試。";
    } finally {
      if (revision === imageLibraryRevision) imageLibraryRefresh.disabled = false;
    }
  }

  function drawRaster(canvas, raster) {
    canvas.width = raster.width;
    canvas.height = raster.height;
    const context = canvas.getContext("2d", { alpha: false });
    const imageData = context.createImageData(raster.width, raster.height);
    imageData.data.set(raster.data);
    context.putImageData(imageData, 0, 0);
  }

  function drawFramePreview(canvas, raster) {
    const statusHeight = 40;
    canvas.width = raster.width;
    canvas.height = raster.height + statusHeight;
    const context = canvas.getContext("2d", { alpha: false });
    context.fillStyle = "#ffffff";
    context.fillRect(0, 0, canvas.width, canvas.height);
    const imageData = context.createImageData(raster.width, raster.height);
    imageData.data.set(raster.data);
    context.putImageData(imageData, 0, statusHeight);
    const colors = (window.PaperFrameQuantizer && window.PaperFrameQuantizer.PALETTE) || [];
    const segment = Math.max(1, Math.floor(canvas.width / Math.max(1, colors.length)));
    colors.forEach((color, index) => {
      context.fillStyle = `rgb(${color.rgb[0]}, ${color.rgb[1]}, ${color.rgb[2]})`;
      context.fillRect(index * segment, 0, index === colors.length - 1 ? canvas.width - (index * segment) : segment, statusHeight);
    });
    context.fillStyle = "#171712";
    context.font = "800 14px Consolas, monospace";
    context.textBaseline = "middle";
    context.fillText("PF  /  PAPERFRAME", 10, statusHeight / 2);
  }

  function defaultPfr1Name(fileName) {
    const stem = String(fileName || "paperframe")
      .replace(/\.[^/.]+$/, "")
      .replace(/[\\/\u0000-\u001F\u007F]/g, "")
      .trim();
    const safeStem = stem && !stem.startsWith(".") ? stem : "paperframe";
    return `${safeStem.slice(0, 80)}.pfr1`;
  }

  async function decodeImageFile(file) {
    await ensureImageDependency("PaperFrameImage", "/image_pipeline.js");
    if (file.size > maxSourceBytes) {
      throw new RangeError("source_file_too_large");
    }
    const bytes = await file.arrayBuffer();
    const exif = window.PaperFrameImage.readExifOrientation(bytes);
    let bitmap;
    let decoderAppliedOrientation = false;
    const decodeWithImageElement = () => new Promise((resolve, reject) => {
      const image = new Image();
      const url = URL.createObjectURL(file);
      image.onload = () => { URL.revokeObjectURL(url); resolve(image); };
      image.onerror = () => { URL.revokeObjectURL(url); reject(new Error("image_decode_failed")); };
      image.src = url;
    });
    if (window.createImageBitmap) {
      try {
        bitmap = await window.createImageBitmap(file, { imageOrientation: "none" });
      } catch {
        try {
          bitmap = await window.createImageBitmap(file);
          decoderAppliedOrientation = true;
        } catch {
          bitmap = await decodeWithImageElement();
          decoderAppliedOrientation = true;
        }
      }
    } else {
      bitmap = await decodeWithImageElement();
      decoderAppliedOrientation = true;
    }
    const canvas = document.createElement("canvas");
    if ((bitmap.width * bitmap.height) > maxSourcePixels) {
      if (typeof bitmap.close === "function") bitmap.close();
      throw new RangeError("source_image_too_large");
    }
    canvas.width = bitmap.width;
    canvas.height = bitmap.height;
    const context = canvas.getContext("2d", { willReadFrequently: true });
    context.drawImage(bitmap, 0, 0);
    if (typeof bitmap.close === "function") bitmap.close();
    return {
      raster: window.PaperFrameImage.makeRaster(
        canvas.width,
        canvas.height,
        context.getImageData(0, 0, canvas.width, canvas.height).data,
      ),
      exifOrientation: decoderAppliedOrientation ? 1 : exif,
    };
  }

  function cancelQuantizeWorker() {
    const active = activeQuantizeWorker;
    if (active) {
      activeQuantizeWorker = null;
      active.cancel();
    }
  }

  function quantizeWithWorker(raster, mode, requestId) {
    if (!window.Worker) {
      return Promise.resolve(window.PaperFrameQuantizer.quantize(raster, mode));
    }
    return new Promise((resolve, reject) => {
      const worker = new Worker("/image_quantize_worker.js");
      const state = { worker, cancel: null };
      activeQuantizeWorker = state;
      let settled = false;
      const finish = (callback) => {
        if (settled) return;
        settled = true;
        if (activeQuantizeWorker === state) activeQuantizeWorker = null;
        worker.terminate();
        callback();
      };
      state.cancel = () => finish(() => reject(new Error("quantize_cancelled")));
      const source = new Uint8ClampedArray(raster.data);
      worker.onmessage = (event) => {
        const result = event.data || {};
        finish(() => {
          if (!result.ok || result.id !== requestId) {
            reject(new Error(result.error || "quantize_failed"));
            return;
          }
          resolve({
            width: result.width,
            height: result.height,
            data: new Uint8ClampedArray(result.data),
            codes: new Uint8Array(result.codes),
          });
        });
      };
      worker.onerror = () => {
        finish(() => reject(new Error("quantize_worker_failed")));
      };
      try {
        worker.postMessage({
          id: requestId,
          width: raster.width,
          height: raster.height,
          data: source.buffer,
          mode,
        }, [source.buffer]);
      } catch (error) {
        finish(() => reject(error));
      }
    });
  }

  function applyImageTransform(transform) {
    if (!imageWorkingRaster) return;
    if (transform === "mirror-x") {
      imageWorkingRaster = window.PaperFrameImage.mirror(imageWorkingRaster, true, false);
      imageTransformFlags |= 0x0001;
    } else if (transform === "mirror-y") {
      imageWorkingRaster = window.PaperFrameImage.mirror(imageWorkingRaster, false, true);
      imageTransformFlags |= 0x0002;
    } else if (transform === "rotate-90-cw") {
      imageWorkingRaster = window.PaperFrameImage.rotate90Cw(imageWorkingRaster);
      imageTransformFlags |= 0x0004;
    } else {
      return;
    }
    void processImage();
  }

  async function processImage() {
    if (!imageWorkingRaster || !imageBaseRaster) return;
    await ensureImageDependency("PaperFrameImage", "/image_pipeline.js");
    await ensureImageDependency("PaperFrameQuantizer", "/image_quantizer.js");
    await ensureImageDependency("PaperFramePfr1", "/image_pfr1.js");
    const requestId = ++imageRevision;
    const workingRaster = imageWorkingRaster;
    const transformFlags = imageTransformFlags;
    cancelQuantizeWorker();
    imageProcessButton.disabled = true;
    downloadPfr1.disabled = true;
    uploadPfr1.disabled = true;
    imagePfr1 = null;
    imageStatus.className = "save-status";
    imageStatus.textContent = "正在套用已按下的變換與 fit…";
    try {
      const profile = window.PaperFrameImage.ORIENTATION_PROFILES[imageOrientation.value];
      const processed = window.PaperFrameImage.processRaster(workingRaster, {
        exifOrientation: 1,
        mirrorX: false,
        mirrorY: false,
        rotate90Cw: false,
        fit: imageFit.value,
        targetWidth: profile.width,
        targetHeight: profile.height,
      });
      if (requestId !== imageRevision) return;
      drawRaster(
        previewOriginal,
        imageBaseRaster,
      );
      drawRaster(previewProcessed, processed);
      imageStatus.textContent = "正在由離線 worker 做六色量化…";
      const quantized = await quantizeWithWorker(processed, imageDither.value, requestId);
      if (requestId !== imageRevision) return;
      drawRaster(previewSixColor, quantized);
      drawFramePreview(previewFrame, quantized);
      const packed = await window.PaperFramePfr1.packPfr1(quantized, {
        filename: imageFilename.value.trim() || imageFileName,
        orientation: imageOrientation.value,
        flags: transformFlags,
        dithering: imageDither.value,
      });
      imagePfr1 = packed;
      imageOutputDimensions.textContent = `${profile.width} × ${profile.height}`;
      imageOutputPayload.textContent = formatImageSize((profile.width * profile.height) / 2);
      imageOutputSize.textContent = formatImageSize(packed.length);
      imageOutputOrientation.textContent = imageOrientation.value === "portrait" ? "直向" : "橫向";
      imageStatus.className = "save-status success";
      imageStatus.textContent = "已完成本機處理；可下載或上傳 PFR1。";
      downloadPfr1.disabled = false;
      uploadPfr1.disabled = false;
    } catch (error) {
      if (requestId !== imageRevision) return;
      imageStatus.className = "save-status error";
      imageStatus.textContent = `圖片處理失敗：${error.message || "unknown"}`;
      imageOutputDimensions.textContent = "未知";
      imageOutputPayload.textContent = "未知";
      imageOutputSize.textContent = "未知";
      imageOutputOrientation.textContent = "未知";
    } finally {
      if (requestId === imageRevision) imageProcessButton.disabled = !imageWorkingRaster;
    }
  }

  async function selectImageFile(file) {
    const selectionRevision = ++imageSelectionRevision;
    imageRevision += 1;
    cancelQuantizeWorker();
    imageBaseRaster = null;
    imageWorkingRaster = null;
    imageTransformFlags = 0;
    imageTransformButtons.forEach((button) => { button.disabled = true; });
    imageProcessButton.disabled = true;
    downloadPfr1.disabled = true;
    uploadPfr1.disabled = true;
    imageStatus.className = "save-status";
    imageStatus.textContent = "正在讀取本機圖片…";
    if (!file) {
      imageSourceRaster = null;
      imageStatus.textContent = "請先選擇圖片。";
      return;
    }
    try {
      const decoded = await decodeImageFile(file);
      if (selectionRevision !== imageSelectionRevision) return;
      imageSourceRaster = decoded.raster;
      imageExifOrientation = decoded.exifOrientation;
      imageBaseRaster = window.PaperFrameImage.flattenOnWhite(
        window.PaperFrameImage.orientExif(imageSourceRaster, imageExifOrientation),
      );
      imageWorkingRaster = imageBaseRaster;
      imageFileName = defaultPfr1Name(file.name);
      imageFilename.value = imageFileName;
      imageSourceInfo.textContent = `${file.name} · ${decoded.raster.width} × ${decoded.raster.height} · EXIF ${imageExifOrientation}`;
      imageProcessButton.disabled = false;
      imageTransformButtons.forEach((button) => { button.disabled = false; });
      await processImage();
    } catch (error) {
      if (selectionRevision !== imageSelectionRevision) return;
      imageSourceRaster = null;
      imageBaseRaster = null;
      imageWorkingRaster = null;
      imageTransformFlags = 0;
      imageTransformButtons.forEach((button) => { button.disabled = true; });
      imageProcessButton.disabled = true;
      imageStatus.className = "save-status error";
      const reason = error && typeof error.message === "string" ? error.message : "";
      if (reason === "source_file_too_large") {
        imageStatus.textContent = "圖片檔案過大（上限 32 MB）。";
      } else if (reason === "source_image_too_large") {
        imageStatus.textContent = "圖片像素過大（上限 1600 萬像素）。";
      } else if (reason === "image_decode_failed") {
        imageStatus.textContent = "瀏覽器無法解碼這張圖片；請改用 PNG/JPEG/WebP。";
      } else if (reason.startsWith("script_load_failed:")) {
        imageStatus.textContent = "圖片處理模組下載失敗；請重新整理頁面後再試。";
      } else if (reason === "PaperFrameImage_unavailable") {
        imageStatus.textContent = "圖片處理模組未載入完成；請稍候再試。";
      } else {
        imageStatus.textContent = `無法讀取圖片：${reason || "unknown_error"}`;
      }
    }
  }

  function downloadImagePfr1() {
    if (!imagePfr1) return;
    const fileName = imageFilename.value.trim() || imageFileName;
    const blob = new Blob([imagePfr1], { type: "application/vnd.paperframe.pfr1" });
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = fileName;
    link.click();
    window.setTimeout(() => URL.revokeObjectURL(url), 1000);
  }

  async function uploadImagePfr1() {
    if (!imagePfr1 || !csrfToken) return;
    uploadPfr1.disabled = true;
    downloadPfr1.disabled = true;
    imageStatus.className = "save-status";
    imageStatus.textContent = "正在上傳處理後的 PFR1…";
    try {
      const response = await fetch("/api/v1/images", {
        method: "POST",
        headers: {
          "Content-Type": "application/vnd.paperframe.pfr1",
          "X-CSRF-Token": csrfToken,
        },
        body: imagePfr1,
      });
      const payload = await response.json();
      if (response.status === 401) {
        showAuthForm(true);
        return;
      }
      if (!response.ok || !payload.data) {
        throw new Error(payload.error || "upload_failed");
      }
      imageStatus.className = "save-status success";
      imageStatus.textContent = `已上傳到裝置圖片庫（ID ${payload.data.id}）。`;
      await loadImageLibrary();
    } catch (error) {
      imageStatus.className = "save-status error";
      imageStatus.textContent = `上傳失敗：${error.message || "請稍後重試"}`;
    } finally {
      if (imagePfr1) {
        downloadPfr1.disabled = false;
        uploadPfr1.disabled = false;
      }
    }
  }

  function renderSystemEvents(events) {
    systemEventsList.replaceChildren();
    if (!events || events.length === 0) {
      const empty = document.createElement("p");
      empty.className = "image-library-empty";
      empty.textContent = "尚無診斷事件。";
      systemEventsList.append(empty);
      return;
    }
    events.slice().reverse().forEach((event) => {
      const row = document.createElement("article");
      row.className = "image-library-row";
      const copy = document.createElement("div");
      copy.className = "image-library-copy";
      const name = document.createElement("strong");
      name.className = "image-library-name";
      name.textContent = event.message || "事件";
      const meta = document.createElement("small");
      meta.className = "image-library-meta";
      meta.textContent = `#${event.sequence_id} · ${event.category} / ${event.severity} · ${formatUptime(event.uptime_ms)}`;
      copy.append(name, meta);
      row.append(copy);
      systemEventsList.append(row);
    });
  }

  async function loadSystemStatus() {
    systemOtaStatus.textContent = "";
    try {
      const [deviceResponse, statusResponse, otaResponse, eventsResponse] = await Promise.all([
        fetch("/api/v1/device", { cache: "no-store" }),
        fetch("/api/v1/status", { cache: "no-store" }),
        fetch("/api/v1/system/ota/status", { cache: "no-store" }),
        fetch("/api/v1/events", { cache: "no-store" }),
      ]);
      if (statusResponse.status === 401) {
        showAuthForm(true);
        return;
      }

      const devicePayload = await deviceResponse.json();
      if (deviceResponse.ok && devicePayload.data) {
        $("#system-firmware-version").textContent = devicePayload.data.firmware || "未知";
      }

      const statusPayload = await statusResponse.json();
      if (statusResponse.ok && statusPayload.data) {
        const data = statusPayload.data;
        const display = data.display || {};
        const network = data.network || {};
        const diagnostics = data.diagnostics || {};
        const storage = data.storage || {};
        $("#system-display-state").textContent = labelState(display.state);
        $("#system-display-outcome").textContent = display.last_outcome ? labelState(display.last_outcome) : "尚未刷新";
        $("#system-reboot-reason").textContent = diagnostics.reboot_reason || "未知";
        $("#system-wifi-state").textContent = labelState(network.wifi);
        $("#system-internet-state").textContent = labelState(network.internet);
        $("#system-sntp-state").textContent = labelState(network.sntp);
        $("#system-uptime").textContent = formatUptime(data.uptime_ms);
        $("#system-flash-capacity").textContent = formatBytes(storage.flash_bytes);
        $("#system-psram-capacity").textContent = formatBytes(storage.psram_bytes);
        $("#system-webfs-capacity").textContent = storage.webfs_total_bytes == null
          ? "未知" : `${formatBytes(storage.webfs_used_bytes)} / ${formatBytes(storage.webfs_total_bytes)}`;
        $("#system-imagefs-capacity").textContent = storage.imagefs_total_bytes == null
          ? "未知" : `${formatBytes(storage.imagefs_used_bytes)} / ${formatBytes(storage.imagefs_total_bytes)}`;
      }

      const otaPayload = await otaResponse.json();
      if (otaResponse.ok && otaPayload.data) {
        const ota = otaPayload.data;
        const checkLabels = { unknown: "未知", checking: "檢查中", up_to_date: "已是最新", update_available: "有新版本", check_failed: "檢查失敗" };
        const updateLabels = { idle: "閒置", downloading: "下載中", writing: "寫入中", ready_pending_reboot: "已完成，待重啟", failed: "失敗" };
        $("#system-ota-check-state").textContent = checkLabels[ota.check_state] || "未知";
        $("#system-ota-latest-version").textContent = ota.latest_version || "未知";
        $("#system-ota-update-state").textContent = updateLabels[ota.update_state] || "未知";
        $("#system-ota-progress").textContent = (ota.update_state === "downloading" || ota.update_state === "writing")
          ? `${ota.progress_percent}%` : "—";
        // ready_pending_reboot + non-empty last_error means "succeeded but
        // automatic reboot didn't fire, manual reboot needed" -- NOT a
        // failure; only "failed" state is an actual OTA failure.
        $("#system-ota-error").textContent = ota.last_error || "—";
        systemOtaUpdate.disabled = ota.check_state !== "update_available";
        // Stop polling once the update reaches a terminal state: idle
        // means nothing was ever started, ready_pending_reboot means the
        // device is about to disconnect on its own, failed is a real
        // stop. Only downloading/writing keep polling alive.
        if (ota.update_state !== "downloading" && ota.update_state !== "writing") {
          stopSystemOtaPoll();
        }
      }

      const eventsPayload = await eventsResponse.json();
      if (eventsResponse.ok && eventsPayload.data) {
        renderSystemEvents(eventsPayload.data.events);
      }
    } catch (error) {
      systemOtaStatus.className = "save-status error";
      systemOtaStatus.textContent = "無法讀取系統狀態；請確認仍連著 PaperFrame。";
    }
  }

  function stopSystemOtaPoll() {
    if (systemOtaPollTimer) {
      clearInterval(systemOtaPollTimer);
      systemOtaPollTimer = null;
    }
  }

  function startSystemOtaPoll() {
    stopSystemOtaPoll();
    // Bounded: an OTA download/write is expected to finish well within a
    // few minutes (see OtaWorker::kUpdateOverallDeadlineMs); this stops
    // polling on its own after ~10 minutes even if something wedges,
    // rather than polling forever in a background tab.
    systemOtaPollAttemptsRemaining = 200;
    systemOtaPollTimer = setInterval(() => {
      if (systemOtaPollAttemptsRemaining-- <= 0 || currentView !== "system") {
        stopSystemOtaPoll();
        return;
      }
      loadSystemStatus();
    }, 3000);
  }

  systemRefresh.addEventListener("click", () => loadSystemStatus());

  systemReboot.addEventListener("click", async () => {
    if (!csrfToken) return;
    if (!window.confirm("確定要重新啟動裝置？連線會暫時中斷。")) return;
    systemReboot.disabled = true;
    systemRebootStatus.className = "save-status";
    systemRebootStatus.textContent = "正在重新啟動…";
    try {
      const response = await fetch("/api/v1/system/reboot", {
        method: "POST",
        headers: { "X-CSRF-Token": csrfToken },
      });
      const payload = await response.json();
      if (response.status === 401) {
        showAuthForm(true);
        return;
      }
      if (!response.ok || !payload.ok) throw new Error(payload.error || "reboot_failed");
      systemRebootStatus.className = "save-status success";
      systemRebootStatus.textContent = "已送出重新啟動要求，裝置即將斷線。";
    } catch (error) {
      systemRebootStatus.className = "save-status error";
      systemRebootStatus.textContent = `重新啟動失敗：${error.message || "請稍後重試"}`;
      systemReboot.disabled = false;
    }
  });

  systemOtaCheck.addEventListener("click", async () => {
    if (!csrfToken) return;
    systemOtaCheck.disabled = true;
    systemOtaStatus.className = "save-status";
    systemOtaStatus.textContent = "正在檢查更新…";
    try {
      const response = await fetch("/api/v1/system/ota/check", {
        method: "POST",
        headers: { "X-CSRF-Token": csrfToken },
      });
      const payload = await response.json();
      if (response.status === 401) {
        showAuthForm(true);
        return;
      }
      if (response.status === 409) {
        systemOtaStatus.textContent = "已有檢查或更新在進行中，請稍候。";
        return;
      }
      if (!response.ok || !payload.ok) throw new Error(payload.error || "ota_check_failed");
      systemOtaStatus.className = "save-status success";
      systemOtaStatus.textContent = "已送出檢查要求，稍後重新整理查看結果。";
      setTimeout(loadSystemStatus, 3000);
    } catch (error) {
      systemOtaStatus.className = "save-status error";
      systemOtaStatus.textContent = `檢查失敗：${error.message || "請稍後重試"}`;
    } finally {
      systemOtaCheck.disabled = false;
    }
  });

  systemOtaUpdate.addEventListener("click", async () => {
    if (!csrfToken) return;
    if (!window.confirm("確定要立即更新韌體？更新完成後裝置會自動重新啟動，過程中請勿斷電。")) return;
    systemOtaUpdate.disabled = true;
    systemOtaStatus.className = "save-status";
    systemOtaStatus.textContent = "正在下載並寫入新韌體…";
    try {
      const response = await fetch("/api/v1/system/ota/update", {
        method: "POST",
        headers: { "X-CSRF-Token": csrfToken },
      });
      const payload = await response.json();
      if (response.status === 401) {
        showAuthForm(true);
        return;
      }
      if (response.status === 409) {
        systemOtaStatus.textContent = "已有檢查或更新在進行中，請稍候。";
        systemOtaUpdate.disabled = false;
        return;
      }
      if (!response.ok || !payload.ok) throw new Error(payload.error || "ota_update_failed");
      systemOtaStatus.className = "save-status success";
      systemOtaStatus.textContent = "更新已開始，完成後裝置會自動重新啟動；請勿中斷電源。";
      startSystemOtaPoll();
    } catch (error) {
      systemOtaStatus.className = "save-status error";
      systemOtaStatus.textContent = `更新失敗：${error.message || "請稍後重試"}`;
      systemOtaUpdate.disabled = false;
    }
  });

  function showView(view, refresh = true) {
    currentView = view;
    dashboardView.hidden = view !== "dashboard";
    wifiView.hidden = view !== "wifi";
    weatherView.hidden = view !== "weather";
    imageView.hidden = view !== "image";
    environmentView.hidden = view !== "environment";
    systemView.hidden = view !== "system";
    $$(".nav-link[data-view]").forEach((link) => {
      const active = link.dataset.view === view;
      link.classList.toggle("active", active);
      if (active) link.setAttribute("aria-current", "page");
      else link.removeAttribute("aria-current");
    });
    if (refresh && view === "dashboard") loadDashboard();
    if (refresh && view === "wifi") scan(true);
    if (refresh && view === "weather") loadWeatherConfig();
    if (refresh && view === "image") loadImageLibrary();
    if (refresh && view === "environment") loadEnvironmentConfig();
    if (refresh && view === "system") loadSystemStatus();
  }

  $$(".nav-link[data-view]").forEach((link) => link.addEventListener("click", () => showView(link.dataset.view)));
  refreshDashboard.addEventListener("click", () => loadDashboard());
  imageSourceInput.addEventListener("change", () => selectImageFile(imageSourceInput.files[0]));
  [imageOrientation, imageFit, imageDither].forEach((control) => {
    control.addEventListener("change", () => { if (imageSourceRaster) processImage(); });
  });
  imageFilename.addEventListener("change", () => { if (imageSourceRaster) processImage(); });
  imageProcessButton.addEventListener("click", () => processImage());
  imageMirrorX.addEventListener("click", () => applyImageTransform("mirror-x"));
  imageMirrorY.addEventListener("click", () => applyImageTransform("mirror-y"));
  imageRotate.addEventListener("click", () => applyImageTransform("rotate-90-cw"));
  downloadPfr1.addEventListener("click", downloadImagePfr1);
  uploadPfr1.addEventListener("click", uploadImagePfr1);
  imageLibraryRefresh.addEventListener("click", () => loadImageLibrary());
  imageLibraryList.addEventListener("click", (event) => {
    const button = event.target.closest("button[data-image-action]");
    if (!button) return;
    const name = button.dataset.imageName;
    const action = button.dataset.imageAction;
    if (action === "activate") {
      mutateImageLibrary(`/api/v1/images/${encodeURIComponent(name)}/activate`, "POST");
    } else if (action === "remove") {
      if (window.confirm(`確定刪除「${name}」？`)) {
        mutateImageLibrary(`/api/v1/images/${encodeURIComponent(name)}`, "DELETE");
      }
    } else if (action === "move-up") {
      reorderImage(name, -1);
    } else if (action === "move-down") {
      reorderImage(name, 1);
    }
  });

  function showAuthenticated(token) {
    csrfToken = token || "";
    authPassword.value = "";
    authGate.hidden = true;
    appShell.hidden = false;
    topNavigation.hidden = false;
    authenticatedActions.hidden = false;
    loadDashboard().then((runtime) => {
      const wifiState = runtime && runtime.network ? runtime.network.wifi : null;
      const initialView =
        wifiState === "provisioning" || wifiState === "starting_ap"
          ? "wifi"
          : "dashboard";
      showView(initialView, initialView === "wifi");
    });
  }

  function showAuthForm(passwordConfigured) {
    imageLibraryRevision += 1;
    imageLibraryImages = [];
    imageLibraryList.replaceChildren();
    imageLibraryStatus.textContent = "請重新登入後查看裝置圖片庫。";
    authGate.hidden = false;
    appShell.hidden = true;
    topNavigation.hidden = true;
    authenticatedActions.hidden = true;
    uploadPfr1.disabled = true;
    authTitle.textContent = passwordConfigured ? "管理員登入" : "建立管理密碼";
    authPasswordLabel.textContent = passwordConfigured ? "管理密碼" : "新管理密碼";
    authCopy.textContent = passwordConfigured
      ? "請先登入，才能存取 Dashboard、Wi‑Fi 與裝置管理功能。"
      : "首次設定必須先建立管理密碼，接著才會開啟管理介面。";
    authButton.textContent = passwordConfigured ? "登入" : "建立密碼並繼續";
    authPassword.autocomplete = passwordConfigured ? "current-password" : "new-password";
    authPassword.focus();
  }

  async function loadAuthStatus() {
    try {
      const response = await fetch("/api/v1/auth/status", { cache: "no-store" });
      const payload = await response.json();
      if (!response.ok || !payload.data) throw new Error("auth_status");
      if (payload.data.authenticated) showAuthenticated(payload.data.csrf_token);
      else showAuthForm(payload.data.password_configured);
    } catch {
      authCopy.textContent = "無法讀取管理員狀態。請確認仍連著 PaperFrame 後重新整理。";
      authForm.hidden = true;
    }
  }

  authForm.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (authPassword.value.length < 8) {
      authStatus.className = "save-status error";
      authStatus.textContent = "管理密碼至少需要 8 個字元。";
      return;
    }
    authButton.disabled = true;
    authStatus.className = "save-status";
    authStatus.textContent = "正在驗證…";
    try {
      const response = await fetch("/api/v1/auth/login", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: new URLSearchParams({ username: "admin", password: authPassword.value }).toString(),
      });
      const payload = await response.json();
      if (!response.ok || !payload.data) {
        if (response.status === 409) throw new Error("busy");
        if (response.status === 401) throw new Error("invalid_credentials");
        if (response.status === 503) throw new Error("device_busy");
        throw new Error(payload.error || "login_failed");
      }
      showAuthenticated(payload.data.csrf_token);
    } catch (error) {
      authStatus.className = "save-status error";
      if (error && error.message === "busy") {
        authStatus.textContent = "已有另一個登入嘗試進行中，請稍候再試。";
      } else if (error && error.message === "invalid_credentials") {
        authStatus.textContent = "密碼錯誤，請重新輸入。";
      } else if (error && error.message === "device_busy") {
        authStatus.textContent = "裝置忙碌中（可能正在刷新面板），請稍後再試。";
      } else {
        authStatus.textContent = "登入失敗，請確認密碼後再試一次。";
      }
    } finally {
      authButton.disabled = false;
    }
  });

  logoutButton.addEventListener("click", async () => {
    try {
      await fetch("/api/v1/auth/logout", { method: "POST", headers: { "X-CSRF-Token": csrfToken } });
    } finally {
      csrfToken = "";
      window.location.reload();
    }
  });

  passwordToggle.addEventListener("click", () => {
    const reveal = password.type === "password";
    password.type = reveal ? "text" : "password";
    passwordToggle.textContent = reveal ? "隱藏" : "顯示";
    passwordToggle.setAttribute("aria-pressed", reveal ? "true" : "false");
  });
  authPasswordToggle.addEventListener("click", () => {
    const reveal = authPassword.type === "password";
    authPassword.type = reveal ? "text" : "password";
    authPasswordToggle.textContent = reveal ? "隱藏" : "顯示";
    authPasswordToggle.setAttribute("aria-pressed", reveal ? "true" : "false");
  });
  scanButton.addEventListener("click", () => scan(true));
  manualToggle.addEventListener("change", () => {
    manualField.hidden = !manualToggle.checked;
    if (manualToggle.checked) { chooseNetwork(manualSsid.value.trim()); manualSsid.focus(); }
  });
  manualSsid.addEventListener("input", () => { if (manualToggle.checked) chooseNetwork(manualSsid.value.trim()); });

  async function waitForCredentialCommit(requestId) {
    const deadline = Date.now() + 25000;
    while (Date.now() < deadline) {
      const response = await fetch(`/api/v1/wifi/config/status?request_id=${requestId}`, { cache: "no-store" });
      const payload = await response.json();
      if (!response.ok && response.status !== 202) throw new Error(payload.error || "save_failed");
      if (payload.data.state === "committed" || payload.data.state === "reboot_pending") return;
      await new Promise((resolve) => window.setTimeout(resolve, 300));
    }
    throw new Error("save_timeout");
  }

  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    const ssid = manualToggle.checked ? manualSsid.value.trim() : selected;
    if (!ssid) { saveStatus.className = "save-status error"; saveStatus.textContent = "請先選擇或輸入 Wi‑Fi 名稱。"; return; }
    if (password.value.length > 0 && password.value.length < 8) { saveStatus.className = "save-status error"; saveStatus.textContent = "加密網路的密碼至少需要 8 個字元。"; return; }
    saveButton.disabled = true;
    saveStatus.className = "save-status";
    saveStatus.textContent = "正在安全保存設定…";
    try {
      const response = await fetch("/api/v1/wifi/config", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded", "X-CSRF-Token": csrfToken },
        body: new URLSearchParams({ ssid, password: password.value }).toString(),
      });
      const payload = await response.json();
      if (!response.ok || !payload.data || !payload.data.request_id) throw new Error(payload.error || "save_failed");
      saveStatus.textContent = "設定已接收，正在確認寫入完成…";
      await waitForCredentialCommit(payload.data.request_id);
      saveStatus.className = "save-status success";
      saveStatus.textContent = "已保存。PaperFrame 即將重新啟動，請稍候再連回家中網路。";
    } catch {
      saveButton.disabled = false;
      saveStatus.className = "save-status error";
      saveStatus.textContent = "保存失敗，請保持 AP 連線後再試一次。";
    }
  });

  loadTheme();
  loadAuthStatus();
})();
