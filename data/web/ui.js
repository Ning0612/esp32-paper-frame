(() => {
  "use strict";

  const $ = (selector) => document.querySelector(selector);
  const $$ = (selector) => Array.from(document.querySelectorAll(selector));
  const t = (key, vars) => window.PaperFrameI18n.t(key, vars);

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
  const timezoneForm = $("#timezone-form");
  const deviceTimezone = $("#device-timezone");
  const timezoneSave = $("#timezone-save");
  const timezoneStatus = $("#timezone-status");
  const environmentView = $("#environment-view");
  const systemView = $("#system-view");
  const systemRefresh = $("#system-refresh");
  const systemReboot = $("#system-reboot");
  const systemRebootStatus = $("#system-reboot-status");
  const systemPasswordResetForm = $("#system-password-reset-form");
  const systemNewPassword = $("#system-new-password");
  const systemConfirmPassword = $("#system-confirm-password");
  const systemPasswordReset = $("#system-password-reset");
  const systemPasswordResetStatus = $("#system-password-reset-status");
  const systemOtaCheck = $("#system-ota-check");
  const systemOtaUpdate = $("#system-ota-update");
  const systemOtaStatus = $("#system-ota-status");
  const systemEventsList = $("#system-events-list");
  const environmentForm = $("#environment-form");
  const environmentEnabled = $("#environment-enabled");
  const light1Enabled = $("#light1-enabled");
  const light1Threshold = $("#light1-threshold");
  const light2Enabled = $("#light2-enabled");
  const light2Threshold = $("#light2-threshold");
  const awayDuration = $("#away-duration");
  const returnDuration = $("#return-duration");
  const environmentSave = $("#environment-save");
  const environmentStatus = $("#environment-status");
  const imageSourceInput = $("#image-source");
  const imageSourceInfo = $("#image-source-info");
  const imageSourceDropzone = $("#image-source-dropzone");
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
  const imageProcessedCard = $("#image-processed-card");
  const imageCropHint = $("#image-crop-hint");
  const imageCropPositionLabel = $("#image-crop-position");
  const imageCropControls = $("#image-crop-controls");
  const imageCropZoomInput = $("#image-crop-zoom");
  const imageCropZoomValue = $("#image-crop-zoom-value");
  const previewFrame = $("#preview-frame");
  const dashboardStatus = $("#dashboard-status");
  const refreshDashboard = $("#refresh-dashboard");
  const themeToggle = $("#theme-toggle");
  const langToggle = $("#lang-toggle");
  const imageLibraryRefresh = $("#image-library-refresh");
  const imageLibraryStatus = $("#image-library-status");
  const imageLibraryList = $("#image-library-list");
  const imageCarouselForm = $("#image-carousel-form");
  const imageCarouselRandom = $("#image-carousel-random");
  const imageCarouselRefreshMinutes = $("#image-carousel-refresh-minutes");
  const imageCarouselSave = $("#image-carousel-save");
  const imageCarouselStatus = $("#image-carousel-status");

  let selected = "";
  let polling = 0;
  let csrfToken = "";
  let currentView = "dashboard";
  let lastRuntime = null;
  let imageBaseRaster = null;
  let imageWorkingRaster = null;
  let imageCropPosition = { x: 0.5, y: 0.5 };
  let imageCropZoom = 1;
  let cropDragState = null;
  let imageExifOrientation = 1;
  let imageFileName = "paperframe.pfr1";
  let imagePfr1 = null;
  let imageTransformFlags = 0;
  let imageRevision = 0;
  let imageSelectionRevision = 0;
  let activeQuantizeWorker = null;
  let imageLibraryRevision = 0;
  let environmentPollTimer = null;
  let systemOtaPollTimer = null;
  let systemOtaPollAttemptsRemaining = 0;
  let systemOtaStatusRequestId = 0;
  let imageLibraryImages = [];
  const maxSourceBytes = 32 * 1024 * 1024;
  const maxSourcePixels = 64 * 1024 * 1024;
  const maxWorkingPixels = 4 * 1024 * 1024;
  const minCarouselRefreshMinutes = 10;
  const defaultCarouselRefreshMinutes = 30;
  const maxCarouselRefreshMinutes = 24 * 60;
  const scriptReloadPromises = new Map();
  imageCarouselRefreshMinutes.value = String(defaultCarouselRefreshMinutes);

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

  langToggle.addEventListener("click", () => {
    const next = document.documentElement.dataset.lang === "en" ? "zh-Hant" : "en";
    window.PaperFrameI18n.setLang(next);
    try { window.localStorage.setItem(window.PaperFrameI18n.STORAGE_KEY, next); } catch {}
    window.location.reload();
  });

  function chooseNetwork(ssid) {
    selected = ssid;
    selectedSsid.textContent = ssid || t("wifi.not_selected");
    $$(".network-option").forEach((option) => {
      option.setAttribute("aria-checked", option.dataset.ssid === selected ? "true" : "false");
    });
  }

  function signalLabel(rssi) {
    if (rssi >= -50) return t("enum.signal.strong");
    if (rssi >= -68) return t("enum.signal.medium");
    return t("enum.signal.weak");
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
      empty.textContent = t("wifi.scan.empty");
      networkList.append(empty);
    }
  }

  async function scan(refresh) {
    window.clearTimeout(polling);
    scanButton.disabled = true;
    scanStatus.textContent = t("wifi.scan.scanning");
    try {
      const response = await fetch(refresh ? "/api/v1/wifi/scan?refresh=1" : "/api/v1/wifi/scan", { cache: "no-store" });
      const payload = await response.json();
      if (!response.ok && response.status !== 202) throw new Error(payload.error || "scan_failed");
      if (response.status === 202 || payload.data.state === "scanning") {
        polling = window.setTimeout(() => scan(false), 900);
        return;
      }
      renderNetworks(payload.data.networks || []);
      scanStatus.textContent = t("wifi.scan.found", { count: payload.data.networks.length });
      scanButton.disabled = false;
    } catch {
      scanStatus.textContent = t("wifi.scan.failed");
      scanButton.disabled = false;
    }
  }

  function labelState(value) {
    const labels = {
      ready: t("enum.state.ready"), connected: t("enum.state.connected"), provisioning: t("enum.state.provisioning"),
      starting_ap: t("enum.state.starting_ap"), connecting: t("enum.state.connecting"), reachable: t("enum.state.reachable"),
      unreachable: t("enum.state.unreachable"), deep_sleep: t("enum.state.deep_sleep"), refreshing: t("enum.state.refreshing"),
      queued: t("enum.state.queued"), failed: t("enum.state.failed"), unknown: t("common.unknown"),
    };
    return labels[value] || value || t("common.unknown");
  }

  function formatBytes(value) {
    if (value === null || value === undefined || Number(value) === 0) return t("common.unknown");
    const bytes = Number(value);
    if (!Number.isFinite(bytes)) return t("common.unknown");
    if (bytes >= 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
    return `${Math.round(bytes / 1024)} KB`;
  }

  function formatUptime(value) {
    if (value === null || value === undefined) return t("common.unknown");
    const seconds = Math.max(0, Math.floor(Number(value) / 1000));
    if (!Number.isFinite(seconds)) return t("common.unknown");
    const days = Math.floor(seconds / 86400);
    const hours = Math.floor((seconds % 86400) / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    return days > 0 ? `${days}d ${hours}h` : `${hours}h ${minutes}m`;
  }

  function renderDevice(data) {
    $("#device-model").textContent = data.model || t("dashboard.device.model_unknown");
    $("#device-firmware").textContent = t("dashboard.device.firmware", { version: data.firmware || t("common.unknown") });
    $("#device-api").textContent = t("dashboard.device.api", { version: data.api_version || "—" });
  }

  function renderRuntime(data) {
    lastRuntime = data;
    const network = data.network || {};
    const storage = data.storage || {};
    const display = data.display || {};
    const services = data.services || {};
    const carousel = data.carousel || {};
    $("#dashboard-wifi").textContent = labelState(network.wifi);
    $("#dashboard-internet").textContent = t("dashboard.internet_label", { state: labelState(network.internet) });
    $("#dashboard-sntp").textContent = t("dashboard.sntp_label", { state: labelState(network.sntp) });
    $("#dashboard-uptime").textContent = formatUptime(data.uptime_ms);
    $("#dashboard-sequence").textContent = `snapshot ${data.sequence ?? "—"}`;
    $("#dashboard-carousel").textContent = carousel.refresh_minutes == null
      ? t("dashboard.carousel_not_available")
      : t("dashboard.carousel_interval", { minutes: carousel.refresh_minutes });
    $("#display-state").textContent = labelState(display.state);
    $("#display-queue").textContent = display.queued_count == null ? t("common.unknown") : t("dashboard.queue_count", { count: display.queued_count });
    $("#display-last").textContent = display.last_outcome ? labelState(display.last_outcome) : t("dashboard.display_not_refreshed");
    $("#flash-capacity").textContent = formatBytes(storage.flash_bytes);
    $("#psram-capacity").textContent = formatBytes(storage.psram_bytes);
    $("#imagefs-capacity").textContent = storage.imagefs_total_bytes == null ? t("common.unknown") : `${formatBytes(storage.imagefs_used_bytes)} / ${formatBytes(storage.imagefs_total_bytes)}`;
    const serviceValues = Object.values(services).map(labelState);
    $("#service-state").textContent = serviceValues.length ? serviceValues.join(" · ") : t("common.unknown");
    const weatherLabels = {
      available: t("enum.weather.available"),
      stale: t("enum.weather.stale"),
      unavailable: t("dashboard.weather_not_available"),
    };
    $("#weather-state").textContent = weatherLabels[(data.weather || {}).state] || t("common.unknown");
    $("#sensor-state").textContent = data.sensors && data.sensors.temperature_c != null
      ? `${data.sensors.temperature_c} °C`
      : labelSensorStatus((data.sensors || {}).environment_status);
    $("#light-sensor-state").textContent = labelSensorStatus((data.sensors || {}).light_status);
    $("#dashboard-current-image").textContent = carousel.current_image == null
      ? t("dashboard.no_carousel_yet")
      : t("dashboard.image_number", { number: carousel.current_image });
    $("#dashboard-next-refresh").textContent = carousel.next_refresh_ms == null
      ? t("common.unknown")
      : t("dashboard.carousel_next_in", { duration: formatUptime(carousel.next_refresh_ms) });
  }

  async function loadDashboard() {
    dashboardStatus.textContent = t("dashboard.status.loading");
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
        dashboardStatus.textContent = t("dashboard.status.updated", { sequence: statusPayload.data.sequence ?? "—" });
        return statusPayload.data;
      }
      dashboardStatus.textContent = t("dashboard.status.unavailable");
      return null;
    } catch {
      dashboardStatus.textContent = t("dashboard.status.load_failed");
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
    const MAX_ZOOM = 16;
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
      // Rounded to 6 decimal places -- the same resolution as the
      // latitude_e6/longitude_e6 wire format (1 part in 1e6 degree) -- so
      // the round-trip through the number inputs never drifts.
      weatherLatitude.value = state.centerLat.toFixed(6);
      weatherLongitude.value = state.centerLon.toFixed(6);
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
      modeLabel.textContent = t("weather.map_mode.online");
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
      modeLabel.textContent = t("weather.map_mode.offline_grid");
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
      setCoordinates(latDegrees, lonDegrees, { syncBack = false } = {}) {
        const lat = Number(latDegrees);
        const lon = Number(lonDegrees);
        if (!Number.isFinite(lat) || !Number.isFinite(lon)) return;
        // Clamp to the same +/-85.0511 Web Mercator limit renderTiles()
        // uses, not +/-90 (no real weather location is this far
        // poleward anyway).
        state.centerLat = clamp(lat, -MAX_LAT, MAX_LAT);
        state.centerLon = normalizeLon(clamp(lon, -180, 180));
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
    weatherStatus.textContent = t("weather.status.loading");
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
      weatherLatitude.value =
        weather.latitude_e6 != null ? (weather.latitude_e6 / 1e6).toFixed(6) : "";
      weatherLongitude.value =
        weather.longitude_e6 != null ? (weather.longitude_e6 / 1e6).toFixed(6) : "";
      weatherUnits.value = weather.units || "metric";
      weatherNtpServer.value = weather.ntp_server || "pool.ntp.org";
      weatherApiKey.value = "";
      $("#weather-config-state").textContent = weather.configured ? t("weather.config_state.saved") : t("weather.config_state.default");
      $("#weather-api-key-state").textContent = weather.api_key_set ? t("weather.api_key_state.set") : t("weather.api_key_state.unset");
      weatherStatus.textContent = t("weather.status.loaded");
      weatherMap.setCoordinates(weatherLatitude.value, weatherLongitude.value);
    } catch (error) {
      weatherStatus.className = "save-status error";
      weatherStatus.textContent = t("weather.status.load_failed", { reason: error.message || t("common.retry_later") });
    }
  }

  weatherForm.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!csrfToken) return;
    const latitudeDegrees = weatherLatitude.value.trim();
    const longitudeDegrees = weatherLongitude.value.trim();
    if (!latitudeDegrees || !longitudeDegrees || !weatherNtpServer.value.trim()) {
      weatherStatus.className = "save-status error";
      weatherStatus.textContent = t("weather.error.missing_coords");
      return;
    }
    const values = {
      // Wire format stays latitude_e6/longitude_e6 (whole degrees * 1e6);
      // only the input field itself shows plain decimal degrees.
      latitude_e6: String(Math.round(Number(latitudeDegrees) * 1e6)),
      longitude_e6: String(Math.round(Number(longitudeDegrees) * 1e6)),
      units: weatherUnits.value,
      ntp_server: weatherNtpServer.value.trim(),
    };
    weatherSave.disabled = true;
    weatherStatus.className = "save-status";
    weatherStatus.textContent = t("weather.status.saving");
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
      weatherStatus.textContent = t("weather.status.saved");
      await loadWeatherConfig();
    } catch (error) {
      weatherStatus.className = "save-status error";
      weatherStatus.textContent = t("common.save_failed", { reason: error.message || t("common.retry_later") });
    } finally {
      weatherSave.disabled = false;
    }
  });

  // /api/v1/config's `random` and `refresh_minutes` fields are optional on
  // POST (omitting one preserves the carousel's current value server-side --
  // same convention as the carousel form on the image-library tab), so this
  // form only ever sends `timezone` and never touches carousel state.
  async function loadDeviceTimezone() {
    timezoneStatus.textContent = t("weather.timezone.status.loading");
    try {
      const response = await fetch("/api/v1/config", { cache: "no-store" });
      const payload = await response.json();
      if (response.status === 401) {
        showAuthForm(true);
        return;
      }
      const time = payload.data && payload.data.time;
      if (!response.ok || !time || typeof time.timezone !== "string") {
        throw new Error(payload.error || "timezone_config_failed");
      }
      deviceTimezone.value = time.timezone;
      timezoneStatus.textContent = t("common.settings_loaded");
      timezoneSave.disabled = false;
    } catch (error) {
      timezoneStatus.className = "save-status error";
      timezoneStatus.textContent = t("weather.timezone.status.load_failed", { reason: error.message || t("common.retry_later") });
    }
  }

  timezoneForm.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!csrfToken) return;
    const timezone = deviceTimezone.value.trim();
    if (!timezoneForm.checkValidity() || !timezone) {
      timezoneForm.reportValidity();
      return;
    }
    timezoneSave.disabled = true;
    timezoneStatus.className = "save-status";
    timezoneStatus.textContent = t("weather.timezone.status.saving");
    try {
      const response = await fetch("/api/v1/config", {
        method: "POST",
        headers: {
          "Content-Type": "application/x-www-form-urlencoded",
          "X-CSRF-Token": csrfToken,
        },
        body: new URLSearchParams({ timezone }).toString(),
      });
      const payload = await response.json();
      if (response.status === 401) {
        showAuthForm(true);
        return;
      }
      if (response.status === 409 && payload.error === "config_read_only") {
        throw new Error(t("common.config_read_only"));
      }
      if (!response.ok || !payload.ok) throw new Error(payload.error || "timezone_save_failed");
      timezoneStatus.className = "save-status success";
      timezoneStatus.textContent = t("weather.timezone.status.saved");
      await loadDeviceTimezone();
    } catch (error) {
      timezoneStatus.className = "save-status error";
      timezoneStatus.textContent = t("common.save_failed", { reason: error.message || t("common.retry_later") });
    } finally {
      timezoneSave.disabled = false;
    }
  });

  function labelSensorStatus(status) {
    const labels = {
      disabled: t("enum.sensor.disabled"), probing: t("enum.sensor.probing"), online: t("enum.sensor.online"),
      stale: t("enum.sensor.stale"), not_detected: t("enum.sensor.not_detected"), error: t("enum.sensor.error"),
      low_clipped: t("enum.sensor.low_clipped"), high_clipped: t("enum.sensor.high_clipped"),
      unknown: t("common.unknown"), present: t("enum.sensor.present"), away: t("enum.sensor.away"),
    };
    return labels[status] || status || t("common.unknown");
  }

  async function loadEnvironmentConfig() {
    environmentStatus.textContent = t("environment.status.loading");
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
      light1Enabled.checked = !!sensors.light1_enabled;
      light1Threshold.value = sensors.light1_threshold ?? 2000;
      light2Enabled.checked = !!sensors.light2_enabled;
      light2Threshold.value = sensors.light2_threshold ?? 2000;
      awayDuration.value = sensors.away_duration_s ?? 180;
      returnDuration.value = sensors.return_duration_s ?? 30;
      environmentStatus.textContent = t("common.settings_loaded");
    } catch (error) {
      environmentStatus.className = "save-status error";
      environmentStatus.textContent = t("environment.status.load_failed", { reason: error.message || t("common.retry_later") });
    }
    await loadSensorReadings();
    // Only the readings are polled, never the config: re-fetching the config
    // would overwrite the threshold/duration inputs and silently discard
    // whatever the user was in the middle of typing.
    startEnvironmentPoll();
  }

  // The device samples both light channels every 2 s and reports them
  // through an 8-sample moving average, so a step change in light takes
  // ~16 s to settle. A single reading fetched at page-switch time is
  // therefore useless for calibrating a threshold -- you would have to
  // leave and re-enter the page for every lighting condition. 3 s sits just
  // above the device's own sampling period, so each poll can return
  // something new without spinning on unchanged data.
  const environmentPollMs = 3000;

  function stopEnvironmentPoll() {
    if (environmentPollTimer) {
      clearInterval(environmentPollTimer);
      environmentPollTimer = null;
    }
  }

  function startEnvironmentPoll() {
    stopEnvironmentPoll();
    environmentPollTimer = setInterval(() => {
      if (currentView !== "environment") {
        stopEnvironmentPoll();
        return;
      }
      // Nothing to see in a background tab, but keep the timer alive so
      // coming back to the tab resumes without another page switch.
      if (document.hidden) return;
      loadSensorReadings();
    }, environmentPollMs);
  }

  async function loadSensorReadings() {
    try {
      const response = await fetch("/api/v1/sensors", { cache: "no-store" });
      if (response.status === 401) {
        // The session expired underneath the poll; stop rather than
        // retrying every 3 s forever against an endpoint that will keep
        // refusing.
        stopEnvironmentPoll();
        showAuthForm(true);
        return;
      }
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
      // Two photoresistor channels (ADR-0018); the device reports them in a
      // fixed order, but fall back to an empty channel rather than throwing
      // if a payload ever arrives short. Selectors stay literal so
      // test/web/test_dashboard_ui_contract.mjs can still see them.
      const channels = Array.isArray(light.channels) ? light.channels : [];
      const channelOne = channels[0] || {};
      const channelTwo = channels[1] || {};
      const formatChannel = (channel) => {
        const raw = channel.raw == null ? "—" : `${channel.raw}`;
        const threshold = channel.threshold == null ? "—" : `${channel.threshold}`;
        return `${raw} / ${threshold}`;
      };
      // The pin comes from the device rather than the markup: hard-coding it
      // here would silently lie if a channel were ever moved to another GPIO.
      const formatStatus = (channel) => {
        const status = labelSensorStatus(channel.status);
        return channel.gpio == null ? status : t("environment.status_with_gpio", { status, gpio: channel.gpio });
      };
      $("#light1-reading-status").textContent = formatStatus(channelOne);
      $("#light1-raw").textContent = formatChannel(channelOne);
      $("#light2-reading-status").textContent = formatStatus(channelTwo);
      $("#light2-raw").textContent = formatChannel(channelTwo);
      $("#light-deciding-channel").textContent = light.deciding_channel == null
        ? "—"
        : t("environment.light_channel_number", { number: light.deciding_channel });
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
      light1_threshold: light1Threshold.value.trim(),
      light2_threshold: light2Threshold.value.trim(),
      away_duration_s: awayDuration.value.trim(),
      return_duration_s: returnDuration.value.trim(),
    };
    if (environmentEnabled.checked) values.environment_enabled = "on";
    if (light1Enabled.checked) values.light1_enabled = "on";
    if (light2Enabled.checked) values.light2_enabled = "on";
    environmentSave.disabled = true;
    environmentStatus.className = "save-status";
    environmentStatus.textContent = t("environment.status.saving");
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
      environmentStatus.textContent = t("environment.status.saved");
      await loadEnvironmentConfig();
    } catch (error) {
      environmentStatus.className = "save-status error";
      environmentStatus.textContent = t("common.save_failed", { reason: error.message || t("common.retry_later") });
    } finally {
      environmentSave.disabled = false;
    }
  });

  function formatImageSize(bytes) {
    if (!Number.isFinite(bytes)) return t("common.unknown");
    if (bytes >= 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
    return `${Math.max(1, Math.ceil(bytes / 1024))} KB`;
  }

  function renderImageLibrary(images) {
    imageLibraryImages = images.slice();
    imageLibraryList.replaceChildren();
    if (images.length === 0) {
      const empty = document.createElement("p");
      empty.className = "image-library-empty";
      empty.textContent = t("image.library.empty");
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
      name.textContent = image.name || t("image.library.unnamed");
      const meta = document.createElement("small");
      meta.className = "image-library-meta";
      const dimensions = Number.isFinite(Number(image.width)) && Number.isFinite(Number(image.height))
        ? `${image.width} × ${image.height}`
        : t("image.library.dimensions_unknown");
      const orientation = image.orientation === "portrait" ? t("image.orientation.portrait_short") : t("image.orientation.landscape_short");
      meta.textContent = `#${index + 1} · ${dimensions} · ${orientation} · ${formatImageSize(Number(image.file_bytes))}`;
      copy.append(name, meta);

      const states = document.createElement("div");
      states.className = "image-library-states";
      if (image.current) {
        const current = document.createElement("span");
        current.className = "image-library-state current";
        current.textContent = t("image.library.state.current");
        states.append(current);
      }
      if (!image.enabled) {
        const disabled = document.createElement("span");
        disabled.className = "image-library-state disabled";
        disabled.textContent = t("image.library.state.disabled");
        states.append(disabled);
      }
      if (image.corrupt) {
        const corrupt = document.createElement("span");
        corrupt.className = "image-library-state corrupt";
        corrupt.textContent = t("image.library.state.corrupt");
        states.append(corrupt);
      }
      if (states.childElementCount > 0) copy.append(states);

      const actions = document.createElement("div");
      actions.className = "image-library-actions";
      if (image.corrupt || !image.name) {
        const unavailable = document.createElement("span");
        unavailable.className = "field-hint";
        unavailable.textContent = image.corrupt ? t("image.library.download_unavailable") : t("image.library.name_missing");
        actions.append(unavailable);
      } else {
        const download = document.createElement("a");
        download.className = "plain-button";
        download.href = `/api/v1/images/${encodeURIComponent(image.name)}/download`;
        download.download = image.name;
        download.textContent = t("image.download_pfr1");
        actions.append(download);
      }
      if (image.name && image.enabled && !image.corrupt && !image.current) {
        const activate = document.createElement("button");
        activate.className = "plain-button";
        activate.type = "button";
        activate.dataset.imageAction = "activate";
        activate.dataset.imageName = image.name;
        activate.textContent = t("image.library.activate");
        actions.append(activate);
      }
      if (image.name) {
        const remove = document.createElement("button");
        remove.className = "plain-button danger-button";
        remove.type = "button";
        remove.dataset.imageAction = "remove";
        remove.dataset.imageName = image.name;
        remove.textContent = t("image.library.delete");
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
    imageLibraryStatus.textContent = t("image.library.status.updating");
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
      imageLibraryStatus.textContent = t("image.library.status.update_failed", { reason: error.message || t("common.retry_later") });
    }
  }

  async function reorderImage(name, delta) {
    const index = imageLibraryImages.findIndex((image) => image.name === name);
    const target = index + delta;
    if (index < 0 || target < 0 || target >= imageLibraryImages.length || !csrfToken) return;
    const next = imageLibraryImages.slice();
    [next[index], next[target]] = [next[target], next[index]];
    imageLibraryStatus.textContent = t("image.library.status.reordering");
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
      imageLibraryStatus.textContent = t("image.library.status.reorder_failed", { reason: error.message || t("common.retry_later") });
    }
  }

  async function loadImageLibrary() {
    const revision = ++imageLibraryRevision;
    imageLibraryRefresh.disabled = true;
    imageLibraryStatus.textContent = t("image.library.status.loading");
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
      imageLibraryStatus.textContent = t("image.library.status.loaded", { count: images.length });
    } catch {
      if (revision !== imageLibraryRevision) return;
      imageLibraryList.replaceChildren();
      imageLibraryStatus.textContent = t("image.library.status.load_failed");
    } finally {
      if (revision === imageLibraryRevision) imageLibraryRefresh.disabled = false;
    }
  }

  async function loadImageCarouselConfig() {
    imageCarouselStatus.className = "save-status";
    imageCarouselStatus.textContent = t("image.carousel.status.loading");
    try {
      const response = await fetch("/api/v1/config", { cache: "no-store" });
      const payload = await response.json();
      if (response.status === 401) {
        showAuthForm(true);
        return;
      }
      const display = payload.data && payload.data.display;
      const refreshMinutes = display && Number(display.refresh_minutes);
      if (!response.ok || !display || typeof display.random !== "boolean" ||
        !Number.isInteger(refreshMinutes) ||
        refreshMinutes < minCarouselRefreshMinutes ||
        refreshMinutes > maxCarouselRefreshMinutes) {
        throw new Error(payload.error || "carousel_config_failed");
      }
      imageCarouselRandom.checked = display.random;
      imageCarouselRefreshMinutes.value = String(refreshMinutes);
      imageCarouselStatus.textContent = t(
        display.random ? "image.carousel.status.mode_random" : "image.carousel.status.mode_ordered",
        { minutes: refreshMinutes },
      );
    } catch (error) {
      imageCarouselStatus.className = "save-status error";
      imageCarouselStatus.textContent = t("image.carousel.status.load_failed", { reason: error.message || t("common.retry_later") });
    }
  }

  imageCarouselForm.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!csrfToken) return;
    if (!imageCarouselForm.checkValidity()) {
      imageCarouselForm.reportValidity();
      return;
    }
    const refreshMinutes = Number(imageCarouselRefreshMinutes.value);
    if (!Number.isInteger(refreshMinutes) ||
      refreshMinutes < minCarouselRefreshMinutes ||
      refreshMinutes > maxCarouselRefreshMinutes) {
      imageCarouselStatus.className = "save-status error";
      imageCarouselStatus.textContent = t("image.carousel.error.interval_range", { minMinutes: minCarouselRefreshMinutes });
      return;
    }
    imageCarouselSave.disabled = true;
    imageCarouselStatus.className = "save-status";
    imageCarouselStatus.textContent = t("image.carousel.status.saving");
    try {
      const response = await fetch("/api/v1/config", {
        method: "POST",
        headers: {
          "Content-Type": "application/x-www-form-urlencoded",
          "X-CSRF-Token": csrfToken,
        },
        body: new URLSearchParams({
          random: imageCarouselRandom.checked ? "true" : "false",
          refresh_minutes: String(refreshMinutes),
        }).toString(),
      });
      const payload = await response.json();
      if (response.status === 401) {
        showAuthForm(true);
        return;
      }
      if (response.status === 409 && payload.error === "config_read_only") {
        // Firmware older than the stored settings: retrying cannot help.
        throw new Error(t("common.config_read_only"));
      }
      if (!response.ok || !payload.ok) throw new Error(payload.error || "carousel_save_failed");
      imageCarouselStatus.className = "save-status success";
      imageCarouselStatus.textContent = t(
        imageCarouselRandom.checked ? "image.carousel.status.saved_random" : "image.carousel.status.saved_ordered",
        { minutes: refreshMinutes },
      );
      await loadImageCarouselConfig();
    } catch (error) {
      imageCarouselStatus.className = "save-status error";
      imageCarouselStatus.textContent = t("common.save_failed", { reason: error.message || t("common.retry_later") });
    } finally {
      imageCarouselSave.disabled = false;
    }
  });

  function drawRaster(canvas, raster) {
    canvas.width = raster.width;
    canvas.height = raster.height;
    const context = canvas.getContext("2d", { alpha: false });
    const imageData = context.createImageData(raster.width, raster.height);
    imageData.data.set(raster.data);
    context.putImageData(imageData, 0, 0);
  }

  function cropInteractionEnabled() {
    return Boolean(imageWorkingRaster) && imageFit.value === "cover";
  }

  function invalidateImageOutput() {
    imageRevision += 1;
    cancelQuantizeWorker();
    imagePfr1 = null;
    downloadPfr1.disabled = true;
    uploadPfr1.disabled = true;
  }

  function updateCropInteraction() {
    const enabled = cropInteractionEnabled();
    imageProcessedCard.classList.toggle("is-draggable", enabled);
    imageCropHint.hidden = !enabled;
    imageCropControls.hidden = !enabled;
    imageCropZoomInput.disabled = !enabled;
    imageCropZoomInput.value = String(imageCropZoom);
    const zoomPercent = Math.round(imageCropZoom * 100);
    imageCropZoomValue.textContent = `${zoomPercent}%`;
    imageCropZoomInput.setAttribute("aria-valuetext", `${zoomPercent}%`);
    previewProcessed.setAttribute("aria-disabled", enabled ? "false" : "true");
    imageCropPositionLabel.textContent = t("image.crop_position_current", {
      x: imageCropPosition.x.toFixed(2),
      y: imageCropPosition.y.toFixed(2),
      zoom: zoomPercent,
    });
  }

  function cropProfile() {
    return window.PaperFrameImage.ORIENTATION_PROFILES[imageOrientation.value];
  }

  function cropDragGeometry(profile) {
    const geometry = window.PaperFrameImage.cropGeometry(
      imageWorkingRaster,
      profile.width,
      profile.height,
      imageCropZoom,
    );
    return {
      overflowX: geometry.overflowX,
      overflowY: geometry.overflowY,
      viewportWidth: profile.width,
      viewportHeight: profile.height,
    };
  }

  function renderProcessedPreview() {
    if (!imageWorkingRaster) return;
    const profile = cropProfile();
    const processed = window.PaperFrameImage.processRaster(imageWorkingRaster, {
      exifOrientation: 1,
      mirrorX: false,
      mirrorY: false,
      rotate90Cw: false,
      fit: imageFit.value,
      skipFlatten: true,
      cropPosition: imageCropPosition,
      cropZoom: imageCropZoom,
      targetWidth: profile.width,
      targetHeight: profile.height,
    });
    drawRaster(previewProcessed, processed);
  }

  function updateCropPositionFromPointer(event) {
    if (!cropDragState || !imageWorkingRaster || !cropInteractionEnabled()) return;
    if (!cropDragState.invalidated) {
      invalidateImageOutput();
      cropDragState.invalidated = true;
    }
    const profile = cropProfile();
    const geometry = cropDragGeometry(profile);
    const rect = previewProcessed.getBoundingClientRect();
    const displayScaleX = rect.width / geometry.viewportWidth;
    const displayScaleY = rect.height / geometry.viewportHeight;
    const rangeX = geometry.overflowX * displayScaleX;
    const rangeY = geometry.overflowY * displayScaleY;
    const deltaX = event.clientX - cropDragState.startX;
    const deltaY = event.clientY - cropDragState.startY;
    const next = {
      x: rangeX > 0 ? cropDragState.startPosition.x - (deltaX / rangeX) : cropDragState.startPosition.x,
      y: rangeY > 0 ? cropDragState.startPosition.y - (deltaY / rangeY) : cropDragState.startPosition.y,
    };
    imageCropPosition = window.PaperFrameImage.normalizeCropPosition({
      x: Math.min(1, Math.max(0, next.x)),
      y: Math.min(1, Math.max(0, next.y)),
    });
    updateCropInteraction();
    renderProcessedPreview();
  }

  function finishCropDrag() {
    if (!cropDragState) return;
    const shouldProcess = cropDragState.invalidated;
    cropDragState = null;
    if (shouldProcess) void processImage();
  }

  function handleCropZoomInput() {
    imageCropZoom = window.PaperFrameImage.normalizeCropZoom(imageCropZoomInput.value);
    updateCropInteraction();
    if (!imageWorkingRaster) return;
    invalidateImageOutput();
    renderProcessedPreview();
    imageStatus.className = "save-status";
    imageStatus.textContent = t("image.status.crop_zoom_dragging", { percent: Math.round(imageCropZoom * 100) });
  }

  function handleCropZoomChange() {
    if (imageBaseRaster && cropInteractionEnabled()) void processImage();
  }

  function handleCropPointerDown(event) {
    if (!cropInteractionEnabled() || event.button !== 0 || event.isPrimary === false) return;
    event.preventDefault();
    imageStatus.className = "save-status";
    imageStatus.textContent = t("image.status.crop_dragging");
    cropDragState = {
      pointerId: event.pointerId,
      startX: event.clientX,
      startY: event.clientY,
      startPosition: { ...imageCropPosition },
      previousPfr1: imagePfr1,
      invalidated: false,
    };
    previewProcessed.classList.add("is-dragging");
    if (typeof previewProcessed.setPointerCapture === "function") {
      previewProcessed.setPointerCapture(event.pointerId);
    }
  }

  function handleCropPointerMove(event) {
    if (!cropDragState || event.pointerId !== cropDragState.pointerId) return;
    event.preventDefault();
    updateCropPositionFromPointer(event);
  }

  function handleCropPointerEnd(event) {
    if (!cropDragState || event.pointerId !== cropDragState.pointerId) return;
    event.preventDefault();
    previewProcessed.classList.remove("is-dragging");
    finishCropDrag();
  }

  function handleCropPointerCancel(event) {
    if (!cropDragState || event.pointerId !== cropDragState.pointerId) return;
    event.preventDefault();
    imageCropPosition = { ...cropDragState.startPosition };
    const previousPfr1 = cropDragState.previousPfr1;
    previewProcessed.classList.remove("is-dragging");
    cropDragState = null;
    updateCropInteraction();
    renderProcessedPreview();
    if (previousPfr1) {
      imagePfr1 = previousPfr1;
      downloadPfr1.disabled = false;
      uploadPfr1.disabled = false;
      imageStatus.className = "save-status success";
      imageStatus.textContent = t("image.status.crop_cancelled");
    }
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
    const sourceWidth = bitmap.width;
    const sourceHeight = bitmap.height;
    if ((sourceWidth * sourceHeight) > maxSourcePixels) {
      if (typeof bitmap.close === "function") bitmap.close();
      throw new RangeError("source_image_too_large");
    }
    const workingScale = Math.min(
      1,
      Math.sqrt(maxWorkingPixels / (sourceWidth * sourceHeight)),
    );
    const workingWidth = Math.max(1, Math.round(sourceWidth * workingScale));
    const workingHeight = Math.max(1, Math.round(sourceHeight * workingScale));
    const canvas = document.createElement("canvas");
    canvas.width = workingWidth;
    canvas.height = workingHeight;
    const context = canvas.getContext("2d", { willReadFrequently: true });
    if (!context) {
      if (typeof bitmap.close === "function") bitmap.close();
      throw new Error("image_decode_failed");
    }
    try {
      context.drawImage(bitmap, 0, 0, workingWidth, workingHeight);
    } catch {
      if (typeof bitmap.close === "function") bitmap.close();
      throw new Error("image_decode_failed");
    }
    if (typeof bitmap.close === "function") bitmap.close();
    let pixels;
    try {
      pixels = context.getImageData(0, 0, workingWidth, workingHeight).data;
    } catch {
      throw new Error("image_decode_failed");
    }
    return {
      raster: window.PaperFrameImage.makeRaster(
        workingWidth,
        workingHeight,
        pixels,
      ),
      sourceWidth,
      sourceHeight,
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
    imageStatus.textContent = t("image.status.applying_transform");
    try {
      const profile = window.PaperFrameImage.ORIENTATION_PROFILES[imageOrientation.value];
      const processed = window.PaperFrameImage.processRaster(workingRaster, {
        exifOrientation: 1,
        mirrorX: false,
        mirrorY: false,
        rotate90Cw: false,
        fit: imageFit.value,
        skipFlatten: true,
        cropPosition: imageCropPosition,
        cropZoom: imageCropZoom,
        targetWidth: profile.width,
        targetHeight: profile.height,
      });
      if (requestId !== imageRevision) return;
      drawRaster(
        previewOriginal,
        imageBaseRaster,
      );
      drawRaster(previewProcessed, processed);
      updateCropInteraction();
      imageStatus.textContent = t("image.status.quantizing");
      const quantized = await quantizeWithWorker(processed, imageDither.value, requestId);
      if (requestId !== imageRevision) return;
      drawFramePreview(previewFrame, quantized);
      const packed = await window.PaperFramePfr1.packPfr1(quantized, {
        filename: imageFilename.value.trim() || imageFileName,
        orientation: imageOrientation.value,
        flags: transformFlags,
        dithering: imageDither.value,
      });
      if (requestId !== imageRevision) return;
      imagePfr1 = packed;
      imageOutputDimensions.textContent = `${profile.width} × ${profile.height}`;
      imageOutputPayload.textContent = formatImageSize((profile.width * profile.height) / 2);
      imageOutputSize.textContent = formatImageSize(packed.length);
      imageOutputOrientation.textContent = imageOrientation.value === "portrait" ? t("image.orientation.portrait_short") : t("image.orientation.landscape_short");
      imageStatus.className = "save-status success";
      imageStatus.textContent = t("image.status.done");
      downloadPfr1.disabled = false;
      uploadPfr1.disabled = false;
    } catch (error) {
      if (requestId !== imageRevision) return;
      imageStatus.className = "save-status error";
      imageStatus.textContent = t("image.status.process_failed", { reason: error.message || "unknown" });
      imageOutputDimensions.textContent = t("common.unknown");
      imageOutputPayload.textContent = t("common.unknown");
      imageOutputSize.textContent = t("common.unknown");
      imageOutputOrientation.textContent = t("common.unknown");
    } finally {
      if (requestId === imageRevision) imageProcessButton.disabled = !imageWorkingRaster;
    }
  }

  async function selectImageFile(file) {
    const selectionRevision = ++imageSelectionRevision;
    imageRevision += 1;
    cancelQuantizeWorker();
    cropDragState = null;
    imageBaseRaster = null;
    imageWorkingRaster = null;
    imageCropPosition = { x: 0.5, y: 0.5 };
    imageCropZoom = 1;
    imageTransformFlags = 0;
    imageTransformButtons.forEach((button) => { button.disabled = true; });
    imageProcessButton.disabled = true;
    downloadPfr1.disabled = true;
    uploadPfr1.disabled = true;
    imageStatus.className = "save-status";
    imageStatus.textContent = t("image.status.reading_local");
    if (!file) {
      updateCropInteraction();
      imageStatus.textContent = t("image.status.select_first");
      return;
    }
    try {
      const decoded = await decodeImageFile(file);
      if (selectionRevision !== imageSelectionRevision) return;
      imageExifOrientation = decoded.exifOrientation;
      imageBaseRaster = window.PaperFrameImage.flattenOnWhite(
        window.PaperFrameImage.orientExif(decoded.raster, imageExifOrientation),
      );
      imageWorkingRaster = imageBaseRaster;
      imageFileName = defaultPfr1Name(file.name);
      imageFilename.value = imageFileName;
      const workingNote = decoded.sourceWidth !== decoded.raster.width ||
          decoded.sourceHeight !== decoded.raster.height
        ? ` · ${t("image.source_scaled_note", { width: decoded.raster.width, height: decoded.raster.height })}`
        : "";
      imageSourceInfo.textContent = `${file.name} · ${decoded.sourceWidth} × ${decoded.sourceHeight}${workingNote} · EXIF ${imageExifOrientation}`;
      imageProcessButton.disabled = false;
      imageTransformButtons.forEach((button) => { button.disabled = false; });
      await processImage();
    } catch (error) {
      if (selectionRevision !== imageSelectionRevision) return;
      imageBaseRaster = null;
      imageWorkingRaster = null;
      imageCropPosition = { x: 0.5, y: 0.5 };
      updateCropInteraction();
      imageTransformFlags = 0;
      imageTransformButtons.forEach((button) => { button.disabled = true; });
      imageProcessButton.disabled = true;
      imageStatus.className = "save-status error";
      const reason = error && typeof error.message === "string" ? error.message : "";
      if (reason === "source_file_too_large") {
        imageStatus.textContent = t("image.error.file_too_large");
      } else if (reason === "source_image_too_large") {
        imageStatus.textContent = t("image.error.image_too_large");
      } else if (reason === "image_decode_failed") {
        imageStatus.textContent = t("image.error.decode_failed");
      } else if (reason.startsWith("script_load_failed:")) {
        imageStatus.textContent = t("image.error.module_download_failed");
      } else if (reason === "PaperFrameImage_unavailable") {
        imageStatus.textContent = t("image.error.module_not_ready");
      } else {
        imageStatus.textContent = t("image.error.read_failed", { reason: reason || "unknown_error" });
      }
    }
  }

  function dataTransferHasFiles(dataTransfer) {
    return Boolean(dataTransfer && (
      (dataTransfer.files && dataTransfer.files.length > 0) ||
      Array.from(dataTransfer.items || []).some((item) => item.kind === "file")
    ));
  }

  function clearImageSourceDragState() {
    imageSourceDropzone.classList.remove("is-drag-over");
  }

  function handleImageSourceDragOver(event) {
    if (!dataTransferHasFiles(event.dataTransfer)) return;
    event.preventDefault();
    event.dataTransfer.dropEffect = "copy";
    imageSourceDropzone.classList.add("is-drag-over");
  }

  function handleImageSourceDragLeave(event) {
    if (event.relatedTarget && imageSourceDropzone.contains(event.relatedTarget)) return;
    clearImageSourceDragState();
  }

  function handleImageSourceDrop(event) {
    event.preventDefault();
    clearImageSourceDragState();
    const file = event.dataTransfer && event.dataTransfer.files && event.dataTransfer.files[0];
    if (file) void selectImageFile(file);
  }

  function openImageSourcePicker() {
    imageSourceInput.click();
  }

  function handleImageSourceDropzoneKeydown(event) {
    if (event.key !== "Enter" && event.key !== " ") return;
    event.preventDefault();
    openImageSourcePicker();
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
    imageStatus.textContent = t("image.status.uploading");
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
      imageStatus.textContent = t("image.status.uploaded", { id: payload.data.id });
      await loadImageLibrary();
    } catch (error) {
      imageStatus.className = "save-status error";
      imageStatus.textContent = t("image.status.upload_failed", { reason: error.message || t("common.retry_later") });
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
      empty.textContent = t("system.events.empty");
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
      name.textContent = event.message || t("system.events.default_message");
      const meta = document.createElement("small");
      meta.className = "image-library-meta";
      meta.textContent = `#${event.sequence_id} · ${event.category} / ${event.severity} · ${formatUptime(event.uptime_ms)}`;
      copy.append(name, meta);
      row.append(copy);
      systemEventsList.append(row);
    });
  }

  const setOtaProgress = (percent) => {
    const bar = $("#system-ota-progress-bar");
    const known = percent !== null;
    $("#system-ota-progress").textContent = known ? `${percent}%` : "—";
    $("#system-ota-progress-fill").style.width = known ? `${percent}%` : "0%";
    bar.hidden = !known;
    if (known) {
      bar.setAttribute("aria-valuenow", String(percent));
    } else {
      // Absent rather than 0: an unknown value must not read as "no progress".
      bar.removeAttribute("aria-valuenow");
    }
  };

  async function loadSystemStatus() {
    const requestId = ++systemOtaStatusRequestId;
    systemOtaStatus.textContent = "";
    try {
      const [deviceResponse, statusResponse, otaResponse, eventsResponse] = await Promise.all([
        fetch("/api/v1/device", { cache: "no-store" }),
        fetch("/api/v1/status", { cache: "no-store" }),
        fetch("/api/v1/system/ota/status", { cache: "no-store" }),
        fetch("/api/v1/events", { cache: "no-store" }),
      ]);
      if (statusResponse.status === 401) {
        if (requestId === systemOtaStatusRequestId) showAuthForm(true);
        return;
      }

      // Parse every body before the one staleness check below: awaiting
      // json() separately per response (as before) left several more gaps
      // where a newer loadSystemStatus() call, or a check/update POST,
      // could start and finish while this call was still parsing --
      // batching the parses means only a single point-in-time check is
      // needed before any DOM write or polling decision.
      const [devicePayload, statusPayload, otaPayload, eventsPayload] = await Promise.all([
        deviceResponse.json(),
        statusResponse.json(),
        otaResponse.json(),
        eventsResponse.json(),
      ]);

      // A newer loadSystemStatus() call (poll tick, manual refresh, or a
      // fresh check/update click) has started since this one began -- its
      // response is stale and must not overwrite newer state or stop a
      // polling timer started for a more recent operation. Nothing below
      // this point awaits again, so this is the only check needed.
      if (requestId !== systemOtaStatusRequestId) return;

      if (deviceResponse.ok && devicePayload.data) {
        $("#system-firmware-version").textContent = devicePayload.data.firmware || t("common.unknown");
      }

      if (statusResponse.ok && statusPayload.data) {
        const data = statusPayload.data;
        const display = data.display || {};
        const network = data.network || {};
        const diagnostics = data.diagnostics || {};
        const storage = data.storage || {};
        $("#system-display-state").textContent = labelState(display.state);
        $("#system-display-outcome").textContent = display.last_outcome ? labelState(display.last_outcome) : t("dashboard.display_not_refreshed");
        $("#system-reboot-reason").textContent = diagnostics.reboot_reason || t("common.unknown");
        $("#system-wifi-state").textContent = labelState(network.wifi);
        $("#system-internet-state").textContent = labelState(network.internet);
        $("#system-sntp-state").textContent = labelState(network.sntp);
        $("#system-uptime").textContent = formatUptime(data.uptime_ms);
        $("#system-flash-capacity").textContent = formatBytes(storage.flash_bytes);
        $("#system-psram-capacity").textContent = formatBytes(storage.psram_bytes);
        $("#system-imagefs-capacity").textContent = storage.imagefs_total_bytes == null
          ? t("common.unknown") : `${formatBytes(storage.imagefs_used_bytes)} / ${formatBytes(storage.imagefs_total_bytes)}`;
      }

      if (!otaResponse.ok || !otaPayload.data) {
        // Leaving the previous reading on screen would claim an update is
        // still running when all we actually lost is the status request.
        setOtaProgress(null);
      }
      if (otaResponse.ok && otaPayload.data) {
        const ota = otaPayload.data;
        const checkLabels = {
          unknown: t("common.unknown"), checking: t("enum.ota_check.checking"),
          up_to_date: t("enum.ota_check.up_to_date"), update_available: t("enum.ota_check.update_available"),
          check_failed: t("enum.ota_check.check_failed"),
        };
        const updateLabels = {
          idle: t("enum.ota_update.idle"), downloading: t("enum.ota_update.downloading"),
          writing: t("enum.ota_update.writing"), ready_pending_reboot: t("enum.ota_update.ready_pending_reboot"),
          failed: t("enum.ota_update.failed"),
        };
        $("#system-ota-check-state").textContent = checkLabels[ota.check_state] || t("common.unknown");
        $("#system-ota-latest-version").textContent = ota.latest_version || t("common.unknown");
        $("#system-ota-update-state").textContent = updateLabels[ota.update_state] || t("common.unknown");
        const otaInProgress = ota.update_state === "downloading" || ota.update_state === "writing";
        // A missing or malformed percentage is unknown, not zero -- reporting
        // 0% mid-download would be inventing a value, which this project's
        // WebUI rules forbid for absent data.
        const otaPercentRaw = Number(ota.progress_percent);
        setOtaProgress(otaInProgress && Number.isFinite(otaPercentRaw)
          ? Math.max(0, Math.min(100, Math.round(otaPercentRaw)))
          : null);
        // ready_pending_reboot + non-empty last_error means "succeeded but
        // automatic reboot didn't fire, manual reboot needed" -- NOT a
        // failure; only "failed" state is an actual OTA failure.
        $("#system-ota-error").textContent = ota.last_error || "—";
        systemOtaUpdate.disabled = ota.check_state !== "update_available";
        // Keep polling alive while either operation is in flight: "checking"
        // for a version check, downloading/writing for an update. Terminal
        // states (idle, up_to_date, check_failed, ready_pending_reboot,
        // failed) stop it. Also *resume* polling here -- not just start it
        // -- so navigating away from the System page and back while a check
        // or update is still running (which stops the timer, see
        // resumeSystemOtaPollTimer's currentView guard) picks the timer
        // back up instead of leaving the view stuck showing stale state
        // until a manual refresh. This deliberately calls
        // resumeSystemOtaPollTimer(), not startSystemOtaPoll(): resuming
        // must not reset the ~10-minute attempts budget, or a device stuck
        // forever in "checking"/"writing" would let this branch keep
        // re-arming a fresh 200-tick budget indefinitely every time it's
        // observed, defeating the cap entirely.
        const checkInFlight = ota.check_state === "checking";
        const updateInFlight = ota.update_state === "downloading" || ota.update_state === "writing";
        if (checkInFlight || updateInFlight) {
          if (!systemOtaPollTimer) resumeSystemOtaPollTimer();
        } else {
          stopSystemOtaPoll();
        }
      }

      if (eventsResponse.ok && eventsPayload.data) {
        renderSystemEvents(eventsPayload.data.events);
      }
    } catch (error) {
      if (requestId !== systemOtaStatusRequestId) return;
      systemOtaStatus.className = "save-status error";
      systemOtaStatus.textContent = t("system.status.load_failed");
    }
  }

  function stopSystemOtaPoll() {
    if (systemOtaPollTimer) {
      clearInterval(systemOtaPollTimer);
      systemOtaPollTimer = null;
    }
  }

  // Restarts the poll interval WITHOUT resetting the remaining-attempts
  // budget, so re-entering loadSystemStatus while an operation is still in
  // flight (e.g. returning to the System page after the timer stopped due
  // to currentView !== "system") resumes polling rather than leaving the
  // view stuck, but can never extend the ~10-minute overall cap that
  // startSystemOtaPoll() below establishes for a *new* operation -- once
  // the budget reaches zero it stays stopped for good, no matter how many
  // times this is called, until a genuinely new check/update request goes
  // through startSystemOtaPoll() again.
  function resumeSystemOtaPollTimer() {
    stopSystemOtaPoll();
    systemOtaPollTimer = setInterval(() => {
      if (systemOtaPollAttemptsRemaining-- <= 0 || currentView !== "system") {
        stopSystemOtaPoll();
        return;
      }
      loadSystemStatus();
    }, 3000);
  }

  function startSystemOtaPoll() {
    // A brand new check/update request supersedes any GET already in
    // flight from an earlier poll tick or manual refresh -- bump the
    // generation so that response, whenever it lands, is recognized as
    // stale and can never stop (or otherwise act on behalf of) the polling
    // timer being started here for the new request.
    ++systemOtaStatusRequestId;
    // Bounded: an OTA download/write is expected to finish well within a
    // few minutes (see OtaWorker::kUpdateOverallDeadlineMs); this stops
    // polling on its own after ~10 minutes even if something wedges,
    // rather than polling forever in a background tab.
    systemOtaPollAttemptsRemaining = 200;
    resumeSystemOtaPollTimer();
  }

  systemRefresh.addEventListener("click", () => loadSystemStatus());

  systemPasswordResetForm.addEventListener("submit", async (event) => {
    event.preventDefault();
    const newPassword = systemNewPassword.value;
    const confirmPassword = systemConfirmPassword.value;
    const newPasswordBytes = new TextEncoder().encode(newPassword).length;
    if (newPasswordBytes < 8 || newPasswordBytes > 128) {
      systemPasswordResetStatus.className = "save-status error";
      systemPasswordResetStatus.textContent = t("system.password.error.length");
      return;
    }
    if (newPassword !== confirmPassword) {
      systemPasswordResetStatus.className = "save-status error";
      systemPasswordResetStatus.textContent = t("system.password.error.mismatch");
      systemConfirmPassword.focus();
      return;
    }
    if (!csrfToken) {
      showAuthForm(true);
      return;
    }
    systemPasswordReset.disabled = true;
    systemPasswordResetStatus.className = "save-status";
    systemPasswordResetStatus.textContent = t("system.password.status.saving");
    try {
      const response = await fetch("/api/v1/auth/password", {
        method: "POST",
        headers: {
          "Content-Type": "application/x-www-form-urlencoded",
          "X-CSRF-Token": csrfToken,
        },
        body: new URLSearchParams({
          new_password: newPassword,
          confirm_password: confirmPassword,
        }).toString(),
      });
      const payload = await response.json();
      if (response.status === 401) {
        showAuthForm(true);
        return;
      }
      if (response.status === 403) {
        csrfToken = "";
        showAuthForm(true);
        return;
      }
      if (response.status === 409) throw new Error("busy");
      if (response.status === 503) throw new Error("device_busy");
      if (!response.ok || !payload.ok) throw new Error(payload.error || "password_reset_failed");
      systemPasswordResetStatus.className = "save-status success";
      systemPasswordResetStatus.textContent = t("system.password.status.saved");
      systemNewPassword.value = "";
      systemConfirmPassword.value = "";
      csrfToken = "";
      window.setTimeout(() => window.location.reload(), 1300);
    } catch (error) {
      systemPasswordResetStatus.className = "save-status error";
      if (error && error.message === "busy") {
        systemPasswordResetStatus.textContent = t("system.password.error.busy");
      } else if (error && error.message === "device_busy") {
        systemPasswordResetStatus.textContent = t("auth.error.device_busy");
      } else {
        systemPasswordResetStatus.textContent = t("system.password.error.failed");
      }
    } finally {
      systemPasswordReset.disabled = false;
    }
  });

  systemReboot.addEventListener("click", async () => {
    if (!csrfToken) return;
    if (!window.confirm(t("system.reboot.confirm"))) return;
    systemReboot.disabled = true;
    systemRebootStatus.className = "save-status";
    systemRebootStatus.textContent = t("system.reboot.status.rebooting");
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
      systemRebootStatus.textContent = t("system.reboot.status.sent");
    } catch (error) {
      systemRebootStatus.className = "save-status error";
      systemRebootStatus.textContent = t("system.reboot.status.failed", { reason: error.message || t("common.retry_later") });
      systemReboot.disabled = false;
    }
  });

  systemOtaCheck.addEventListener("click", async () => {
    if (!csrfToken) return;
    systemOtaCheck.disabled = true;
    systemOtaStatus.className = "save-status";
    systemOtaStatus.textContent = t("system.ota.status.checking");
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
        systemOtaStatus.textContent = t("system.ota.status.busy");
        return;
      }
      if (!response.ok || !payload.ok) throw new Error(payload.error || "ota_check_failed");
      systemOtaStatus.className = "save-status success";
      systemOtaStatus.textContent = t("system.ota.status.checking");
      startSystemOtaPoll();
    } catch (error) {
      systemOtaStatus.className = "save-status error";
      systemOtaStatus.textContent = t("system.ota.status.check_failed", { reason: error.message || t("common.retry_later") });
    } finally {
      systemOtaCheck.disabled = false;
    }
  });

  systemOtaUpdate.addEventListener("click", async () => {
    if (!csrfToken) return;
    if (!window.confirm(t("system.ota.update_confirm"))) return;
    systemOtaUpdate.disabled = true;
    systemOtaStatus.className = "save-status";
    systemOtaStatus.textContent = t("system.ota.status.downloading");
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
        systemOtaStatus.textContent = t("system.ota.status.busy");
        systemOtaUpdate.disabled = false;
        return;
      }
      if (!response.ok || !payload.ok) throw new Error(payload.error || "ota_update_failed");
      systemOtaStatus.className = "save-status success";
      systemOtaStatus.textContent = t("system.ota.status.started");
      startSystemOtaPoll();
    } catch (error) {
      systemOtaStatus.className = "save-status error";
      systemOtaStatus.textContent = t("system.ota.status.update_failed", { reason: error.message || t("common.retry_later") });
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
    if (refresh && view === "weather") {
      loadWeatherConfig();
      loadDeviceTimezone();
    }
    if (refresh && view === "image") {
      loadImageLibrary();
      loadImageCarouselConfig();
    }
    if (refresh && view === "environment") loadEnvironmentConfig();
    if (refresh && view === "system") loadSystemStatus();
  }

  $$(".nav-link[data-view]").forEach((link) => link.addEventListener("click", () => showView(link.dataset.view)));
  refreshDashboard.addEventListener("click", () => loadDashboard());
  imageSourceInput.addEventListener("change", () => {
    clearImageSourceDragState();
    void selectImageFile(imageSourceInput.files[0]);
  });
  imageSourceDropzone.addEventListener("dragover", handleImageSourceDragOver);
  imageSourceDropzone.addEventListener("dragleave", handleImageSourceDragLeave);
  imageSourceDropzone.addEventListener("drop", handleImageSourceDrop);
  imageSourceDropzone.addEventListener("click", openImageSourcePicker);
  imageSourceDropzone.addEventListener("keydown", handleImageSourceDropzoneKeydown);
  [imageOrientation, imageFit, imageDither].forEach((control) => {
    control.addEventListener("change", () => { if (imageBaseRaster) processImage(); });
  });
  imageFilename.addEventListener("change", () => { if (imageBaseRaster) processImage(); });
  imageProcessButton.addEventListener("click", () => processImage());
  imageCropZoomInput.addEventListener("input", handleCropZoomInput);
  imageCropZoomInput.addEventListener("change", handleCropZoomChange);
  imageMirrorX.addEventListener("click", () => applyImageTransform("mirror-x"));
  imageMirrorY.addEventListener("click", () => applyImageTransform("mirror-y"));
  imageRotate.addEventListener("click", () => applyImageTransform("rotate-90-cw"));
  downloadPfr1.addEventListener("click", downloadImagePfr1);
  uploadPfr1.addEventListener("click", uploadImagePfr1);
  previewProcessed.addEventListener("pointerdown", handleCropPointerDown);
  previewProcessed.addEventListener("pointermove", handleCropPointerMove);
  previewProcessed.addEventListener("pointerup", handleCropPointerEnd);
  previewProcessed.addEventListener("pointercancel", handleCropPointerCancel);
  previewProcessed.addEventListener("lostpointercapture", handleCropPointerCancel);
  imageLibraryRefresh.addEventListener("click", () => loadImageLibrary());
  imageLibraryList.addEventListener("click", (event) => {
    const button = event.target.closest("button[data-image-action]");
    if (!button) return;
    const name = button.dataset.imageName;
    const action = button.dataset.imageAction;
    if (action === "activate") {
      mutateImageLibrary(`/api/v1/images/${encodeURIComponent(name)}/activate`, "POST");
    } else if (action === "remove") {
      if (window.confirm(t("image.library.delete_confirm", { name }))) {
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
    imageLibraryStatus.textContent = t("auth.image_library_locked");
    authGate.hidden = false;
    appShell.hidden = true;
    topNavigation.hidden = true;
    authenticatedActions.hidden = true;
    uploadPfr1.disabled = true;
    authTitle.textContent = passwordConfigured ? t("auth.title.login") : t("auth.title.setup");
    authPasswordLabel.textContent = passwordConfigured
      ? t("auth.field.password.login")
      : t("auth.field.password.setup");
    authCopy.textContent = passwordConfigured ? t("auth.copy.login") : t("auth.copy.setup");
    authButton.textContent = passwordConfigured ? t("auth.button.login") : t("auth.button.setup");
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
      authCopy.textContent = t("auth.copy.load_failed");
      authForm.hidden = true;
    }
  }

  authForm.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (authPassword.value.length < 8) {
      authStatus.className = "save-status error";
      authStatus.textContent = t("auth.error.short_password");
      return;
    }
    authButton.disabled = true;
    authStatus.className = "save-status";
    authStatus.textContent = t("auth.status.verifying");
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
        authStatus.textContent = t("auth.error.busy");
      } else if (error && error.message === "invalid_credentials") {
        authStatus.textContent = t("auth.error.invalid_credentials");
      } else if (error && error.message === "device_busy") {
        authStatus.textContent = t("auth.error.device_busy");
      } else {
        authStatus.textContent = t("auth.error.login_failed");
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
    passwordToggle.textContent = reveal ? t("auth.toggle.hide") : t("auth.toggle.show");
    passwordToggle.setAttribute("aria-pressed", reveal ? "true" : "false");
  });
  authPasswordToggle.addEventListener("click", () => {
    const reveal = authPassword.type === "password";
    authPassword.type = reveal ? "text" : "password";
    authPasswordToggle.textContent = reveal ? t("auth.toggle.hide") : t("auth.toggle.show");
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
    if (!ssid) { saveStatus.className = "save-status error"; saveStatus.textContent = t("wifi.error.no_ssid"); return; }
    if (password.value.length > 0 && password.value.length < 8) { saveStatus.className = "save-status error"; saveStatus.textContent = t("wifi.error.short_password"); return; }
    saveButton.disabled = true;
    saveStatus.className = "save-status";
    saveStatus.textContent = t("wifi.status.saving");
    try {
      const response = await fetch("/api/v1/wifi/config", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded", "X-CSRF-Token": csrfToken },
        body: new URLSearchParams({ ssid, password: password.value }).toString(),
      });
      const payload = await response.json();
      if (!response.ok || !payload.data || !payload.data.request_id) throw new Error(payload.error || "save_failed");
      saveStatus.textContent = t("wifi.status.confirming");
      await waitForCredentialCommit(payload.data.request_id);
      saveStatus.className = "save-status success";
      saveStatus.textContent = t("wifi.status.saved");
    } catch {
      saveButton.disabled = false;
      saveStatus.className = "save-status error";
      saveStatus.textContent = t("wifi.status.save_failed");
    }
  });

  loadTheme();
  loadAuthStatus();
})();
