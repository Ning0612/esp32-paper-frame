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

  let selected = "";
  let polling = 0;

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
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
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

  scan(true);
})();
