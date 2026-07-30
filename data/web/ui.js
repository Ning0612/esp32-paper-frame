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
  const sidebar = $("#sidebar");
  const dashboardView = $("#dashboard-view");
  const wifiView = $("#wifi-view");
  const dashboardStatus = $("#dashboard-status");
  const refreshDashboard = $("#refresh-dashboard");
  const themeToggle = $("#theme-toggle");

  let selected = "";
  let polling = 0;
  let csrfToken = "";
  let currentView = "dashboard";
  let lastRuntime = null;

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

  function showView(view, refresh = true) {
    currentView = view;
    dashboardView.hidden = view !== "dashboard";
    wifiView.hidden = view !== "wifi";
    $$(".nav-link[data-view]").forEach((link) => {
      const active = link.dataset.view === view;
      link.classList.toggle("active", active);
      if (active) link.setAttribute("aria-current", "page");
      else link.removeAttribute("aria-current");
    });
    if (refresh && view === "dashboard") loadDashboard();
    if (refresh && view === "wifi") scan(true);
  }

  $$(".nav-link[data-view]").forEach((link) => link.addEventListener("click", () => showView(link.dataset.view)));
  refreshDashboard.addEventListener("click", () => loadDashboard());

  function showAuthenticated(token) {
    csrfToken = token || "";
    authPassword.value = "";
    authGate.hidden = true;
    appShell.hidden = false;
    sidebar.hidden = false;
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
    authGate.hidden = false;
    appShell.hidden = true;
    sidebar.hidden = true;
    authenticatedActions.hidden = true;
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
    const deadline = Date.now() + 65000;
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
