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
  const imageView = $("#image-view");
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
  const maxSourceBytes = 32 * 1024 * 1024;
  const maxSourcePixels = 16 * 1024 * 1024;

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
    $("#weather-state").textContent = (data.weather || {}).state === "unavailable" ? "尚未提供" : "未知";
    $("#sensor-state").textContent = data.sensors && data.sensors.temperature_c != null ? "已讀取" : "未安裝／未知";
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

  function formatImageSize(bytes) {
    if (!Number.isFinite(bytes)) return "未知";
    if (bytes >= 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
    return `${Math.max(1, Math.ceil(bytes / 1024))} KB`;
  }

  function renderImageLibrary(images) {
    imageLibraryList.replaceChildren();
    if (images.length === 0) {
      const empty = document.createElement("p");
      empty.className = "image-library-empty";
      empty.textContent = "裝置目前沒有可用的 PFR1 圖片。";
      imageLibraryList.append(empty);
      return;
    }
    images.forEach((image) => {
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
      meta.textContent = `${dimensions} · ${orientation} · ${formatImageSize(Number(image.file_bytes))}`;
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
      row.append(copy, actions);
      imageLibraryList.append(row);
    });
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
    if (file.size > maxSourceBytes) {
      throw new RangeError("source_file_too_large");
    }
    const bytes = await file.arrayBuffer();
    const exif = window.PaperFrameImage.readExifOrientation(bytes);
    let bitmap;
    let decoderAppliedOrientation = false;
    if (window.createImageBitmap) {
      try {
        bitmap = await window.createImageBitmap(file, { imageOrientation: "none" });
      } catch {
        bitmap = await window.createImageBitmap(file);
        decoderAppliedOrientation = true;
      }
    } else {
      bitmap = await new Promise((resolve, reject) => {
        const image = new Image();
        const url = URL.createObjectURL(file);
        image.onload = () => { URL.revokeObjectURL(url); resolve(image); };
        image.onerror = () => { URL.revokeObjectURL(url); reject(new Error("image_decode_failed")); };
        image.src = url;
      });
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
      const packed = window.PaperFramePfr1.packPfr1(quantized, {
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
    } catch {
      if (selectionRevision !== imageSelectionRevision) return;
      imageSourceRaster = null;
      imageBaseRaster = null;
      imageWorkingRaster = null;
      imageTransformFlags = 0;
      imageTransformButtons.forEach((button) => { button.disabled = true; });
      imageProcessButton.disabled = true;
      imageStatus.className = "save-status error";
      imageStatus.textContent = "無法讀取圖片；請選擇瀏覽器支援的本機格式。";
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

  function showView(view, refresh = true) {
    currentView = view;
    dashboardView.hidden = view !== "dashboard";
    wifiView.hidden = view !== "wifi";
    imageView.hidden = view !== "image";
    $$(".nav-link[data-view]").forEach((link) => {
      const active = link.dataset.view === view;
      link.classList.toggle("active", active);
      if (active) link.setAttribute("aria-current", "page");
      else link.removeAttribute("aria-current");
    });
    if (refresh && view === "dashboard") loadDashboard();
    if (refresh && view === "wifi") scan(true);
    if (refresh && view === "image") loadImageLibrary();
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

  async function waitForLogin(requestToken) {
    const deadline = Date.now() + 180000;
    while (Date.now() < deadline) {
      const response = await fetch("/api/v1/auth/login/status", { cache: "no-store", headers: { "X-Auth-Request": requestToken } });
      const payload = await response.json();
      if (response.status === 202) {
        await new Promise((resolve) => window.setTimeout(resolve, 300));
        continue;
      }
      if (!response.ok || !payload.data) throw new Error(payload.error || "login_failed");
      return payload.data;
    }
    throw new Error("login_timeout");
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
      if (response.status !== 202 || !payload.data || !payload.data.request_token) throw new Error(payload.error || "login_failed");
      const authenticated = await waitForLogin(payload.data.request_token);
      showAuthenticated(authenticated.csrf_token);
    } catch {
      authStatus.className = "save-status error";
      authStatus.textContent = "登入失敗，請確認密碼後再試一次。";
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
