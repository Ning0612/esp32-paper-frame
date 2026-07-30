(() => {
  "use strict";

  const scanButton = document.querySelector("#scan-button");
  const scanStatus = document.querySelector("#scan-status");
  const networkList = document.querySelector("#network-list");
  const manualToggle = document.querySelector("#manual-toggle");
  const manualField = document.querySelector("#manual-field");
  const manualSsid = document.querySelector("#manual-ssid");
  const selectedSsid = document.querySelector("#selected-ssid");
  const password = document.querySelector("#password");
  const passwordToggle = document.querySelector("#password-toggle");
  const form = document.querySelector("#wifi-form");
  const saveButton = document.querySelector("#save-button");
  const saveStatus = document.querySelector("#save-status");
  const authGate = document.querySelector("#auth-gate");
  const authForm = document.querySelector("#auth-form");
  const authTitle = document.querySelector("#auth-title");
  const authCopy = document.querySelector("#auth-copy");
  const authPasswordLabel = document.querySelector("#auth-password-label");
  const authPassword = document.querySelector("#auth-password");
  const authPasswordToggle = document.querySelector("#auth-password-toggle");
  const authButton = document.querySelector("#auth-button");
  const authStatus = document.querySelector("#auth-status");
  const authenticatedActions = document.querySelector("#authenticated-actions");
  const logoutButton = document.querySelector("#logout-button");

  let selected = "";
  let polling = 0;
  let csrfToken = "";

  function chooseNetwork(ssid) {
    selected = ssid;
    selectedSsid.textContent = ssid || "尚未選擇";
    document.querySelectorAll(".network-option").forEach((option) => {
      option.setAttribute(
        "aria-checked",
        option.dataset.ssid === selected ? "true" : "false"
      );
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
      option.setAttribute(
        "aria-checked",
        network.ssid === selected ? "true" : "false"
      );

      const name = document.createElement("strong");
      name.textContent = network.ssid;
      const detail = document.createElement("small");
      detail.textContent = `${network.security.toUpperCase()} · ${network.rssi} dBm`;
      const signal = document.createElement("span");
      signal.className = "signal";
      signal.textContent = signalLabel(network.rssi);
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
      const response = await fetch(
        refresh ? "/api/v1/wifi/scan?refresh=1" : "/api/v1/wifi/scan",
        { cache: "no-store" }
      );
      const payload = await response.json();
      if (!response.ok && response.status !== 202) {
        throw new Error(payload.error || "scan_failed");
      }
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

  scanButton.addEventListener("click", () => scan(true));

  manualToggle.addEventListener("change", () => {
    manualField.hidden = !manualToggle.checked;
    if (manualToggle.checked) {
      chooseNetwork(manualSsid.value.trim());
      manualSsid.focus();
    }
  });

  manualSsid.addEventListener("input", () => {
    if (manualToggle.checked) chooseNetwork(manualSsid.value.trim());
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
    authPasswordToggle.setAttribute(
      "aria-pressed",
      reveal ? "true" : "false"
    );
  });

  function showAuthenticated(token) {
    csrfToken = token || "";
    authPassword.value = "";
    authGate.hidden = true;
    authenticatedActions.hidden = false;
    form.hidden = false;
    scan(true);
  }

  function showAuthForm(passwordConfigured) {
    authGate.hidden = false;
    authenticatedActions.hidden = true;
    form.hidden = true;
    authTitle.textContent = passwordConfigured
      ? "管理員登入"
      : "建立管理密碼";
    authPasswordLabel.textContent = passwordConfigured
      ? "管理密碼"
      : "新管理密碼";
    authCopy.textContent = passwordConfigured
      ? "請先登入，才能存取 Wi‑Fi 與裝置管理功能。"
      : "首次設定必須先建立管理密碼，接著才會開啟 Wi‑Fi 配網。";
    authButton.textContent = passwordConfigured
      ? "登入"
      : "建立密碼並繼續";
    authPassword.autocomplete = passwordConfigured
      ? "current-password"
      : "new-password";
    authPassword.focus();
  }

  async function loadAuthStatus() {
    try {
      const response = await fetch("/api/v1/auth/status", {
        cache: "no-store",
      });
      const payload = await response.json();
      if (!response.ok || !payload.data) throw new Error("auth_status");
      if (payload.data.authenticated) {
        showAuthenticated(payload.data.csrf_token);
      } else {
        showAuthForm(payload.data.password_configured);
      }
    } catch {
      authCopy.textContent =
        "無法讀取管理員狀態。請確認仍連著 PaperFrame 後重新整理。";
      authForm.hidden = true;
    }
  }

  async function waitForLogin(requestToken) {
    const deadline = Date.now() + 65000;
    while (Date.now() < deadline) {
      const response = await fetch("/api/v1/auth/login/status", {
        cache: "no-store",
        headers: { "X-Auth-Request": requestToken },
      });
      const payload = await response.json();
      if (response.status === 202) {
        await new Promise((resolve) => window.setTimeout(resolve, 300));
        continue;
      }
      if (!response.ok || !payload.data) {
        throw new Error(payload.error || "login_failed");
      }
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
        body: new URLSearchParams({
          username: "admin",
          password: authPassword.value,
        }).toString(),
      });
      const payload = await response.json();
      if (
        response.status !== 202 ||
        !payload.data ||
        !payload.data.request_token
      ) {
        throw new Error(payload.error || "login_failed");
      }
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
      await fetch("/api/v1/auth/logout", {
        method: "POST",
        headers: { "X-CSRF-Token": csrfToken },
      });
    } finally {
      csrfToken = "";
      window.location.reload();
    }
  });

  async function waitForCredentialCommit(requestId) {
    const deadline = Date.now() + 25000;
    while (Date.now() < deadline) {
      const response = await fetch(
        `/api/v1/wifi/config/status?request_id=${requestId}`,
        { cache: "no-store" }
      );
      const payload = await response.json();
      if (!response.ok && response.status !== 202) {
        throw new Error(payload.error || "save_failed");
      }
      if (
        payload.data.state === "committed" ||
        payload.data.state === "reboot_pending"
      ) {
        return;
      }
      await new Promise((resolve) => window.setTimeout(resolve, 300));
    }
    throw new Error("save_timeout");
  }

  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    const ssid = manualToggle.checked ? manualSsid.value.trim() : selected;
    if (!ssid) {
      saveStatus.className = "save-status error";
      saveStatus.textContent = "請先選擇或輸入 Wi‑Fi 名稱。";
      return;
    }
    if (password.value.length > 0 && password.value.length < 8) {
      saveStatus.className = "save-status error";
      saveStatus.textContent = "加密網路的密碼至少需要 8 個字元。";
      return;
    }

    saveButton.disabled = true;
    saveStatus.className = "save-status";
    saveStatus.textContent = "正在安全保存設定…";
    const body = new URLSearchParams({
      ssid,
      password: password.value,
    });
    try {
      const response = await fetch("/api/v1/wifi/config", {
        method: "POST",
        headers: {
          "Content-Type": "application/x-www-form-urlencoded",
          "X-CSRF-Token": csrfToken,
        },
        body: body.toString(),
      });
      const payload = await response.json();
      if (!response.ok) throw new Error(payload.error || "save_failed");
      if (!payload.data || !payload.data.request_id) {
        throw new Error("missing_request_id");
      }
      saveStatus.textContent = "設定已接收，正在確認寫入完成…";
      await waitForCredentialCommit(payload.data.request_id);
      saveStatus.className = "save-status success";
      saveStatus.textContent =
        "已保存。PaperFrame 即將重新啟動，請稍候再連回家中網路。";
    } catch {
      saveButton.disabled = false;
      saveStatus.className = "save-status error";
      saveStatus.textContent = "保存失敗，請保持 AP 連線後再試一次。";
    }
  });

  loadAuthStatus();
})();
