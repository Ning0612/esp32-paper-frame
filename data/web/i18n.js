(() => {
  "use strict";

  const STORAGE_KEY = "iot-ui-lang";

  const dict = {
    "zh-Hant": {
      "app.title": "PaperFrame 管理介面",
      "app.heading": "離線電子紙管理台",
      "nav.aria_label": "主要導覽",
      "nav.dashboard": "總覽",
      "nav.wifi": "Wi‑Fi",
      "nav.weather": "天氣",
      "nav.image": "圖片",
      "nav.environment": "環境",
      "nav.system": "系統",
      "topbar.logout": "登出",
      "auth.title.login": "管理員登入",
      "auth.title.setup": "建立管理密碼",
      "auth.copy.checking": "正在確認裝置的管理員狀態…",
      "auth.copy.login": "請先登入，才能存取 Dashboard、Wi‑Fi 與裝置管理功能。",
      "auth.copy.setup": "首次設定必須先建立管理密碼，接著才會開啟管理介面。",
      "auth.copy.load_failed": "無法讀取管理員狀態。請確認仍連著 PaperFrame 後重新整理。",
      "auth.field.account": "管理帳號",
      "auth.field.password.login": "管理密碼",
      "auth.field.password.setup": "新管理密碼",
      "auth.hint.password": "至少 8 個字元；只保存加鹽後的強雜湊。",
      "auth.toggle.show": "顯示",
      "auth.toggle.hide": "隱藏",
      "auth.button.login": "登入",
      "auth.button.setup": "建立密碼並繼續",
      "auth.error.short_password": "管理密碼至少需要 8 個字元。",
      "auth.status.verifying": "正在驗證…",
      "auth.error.busy": "已有另一個登入嘗試進行中，請稍候再試。",
      "auth.error.invalid_credentials": "密碼錯誤，請重新輸入。",
      "auth.error.device_busy": "裝置忙碌中（可能正在刷新面板），請稍後再試。",
      "auth.error.login_failed": "登入失敗，請確認密碼後再試一次。",
      "auth.image_library_locked": "請重新登入後查看裝置圖片庫。",
    },
    en: {
      "app.title": "PaperFrame Console",
      "app.heading": "Offline E-Paper Console",
      "nav.aria_label": "Main navigation",
      "nav.dashboard": "Overview",
      "nav.wifi": "Wi‑Fi",
      "nav.weather": "Weather",
      "nav.image": "Image",
      "nav.environment": "Environment",
      "nav.system": "System",
      "topbar.logout": "Log out",
      "auth.title.login": "Admin Login",
      "auth.title.setup": "Create Admin Password",
      "auth.copy.checking": "Checking device admin status…",
      "auth.copy.login": "Log in to access the dashboard, Wi‑Fi, and device management.",
      "auth.copy.setup": "Set an admin password before the console unlocks.",
      "auth.copy.load_failed": "Could not read admin status. Confirm you're still connected to PaperFrame and reload.",
      "auth.field.account": "Admin account",
      "auth.field.password.login": "Admin password",
      "auth.field.password.setup": "New admin password",
      "auth.hint.password": "At least 8 characters; only a salted hash is stored.",
      "auth.toggle.show": "Show",
      "auth.toggle.hide": "Hide",
      "auth.button.login": "Log in",
      "auth.button.setup": "Create password & continue",
      "auth.error.short_password": "Admin password must be at least 8 characters.",
      "auth.status.verifying": "Verifying…",
      "auth.error.busy": "Another login attempt is already in progress; try again shortly.",
      "auth.error.invalid_credentials": "Incorrect password; try again.",
      "auth.error.device_busy": "Device is busy (possibly refreshing the panel); try again shortly.",
      "auth.error.login_failed": "Login failed; check your password and try again.",
      "auth.image_library_locked": "Log in again to view the device image library.",
    },
  };

  let current = "zh-Hant";

  function t(key, vars) {
    const table = dict[current] || dict["zh-Hant"];
    let value = table[key];
    if (value === undefined) {
      console.warn(`[i18n] missing key: ${key}`);
      return key;
    }
    if (vars) {
      for (const [name, replacement] of Object.entries(vars)) {
        value = value.replaceAll(`{${name}}`, String(replacement));
      }
    }
    return value;
  }

  function applyI18n(root) {
    const scope = root || document;
    scope.querySelectorAll("[data-i18n]").forEach((el) => {
      el.textContent = t(el.getAttribute("data-i18n"));
    });
    scope.querySelectorAll("[data-i18n-aria-label]").forEach((el) => {
      el.setAttribute("aria-label", t(el.getAttribute("data-i18n-aria-label")));
    });
    scope.querySelectorAll("[data-i18n-placeholder]").forEach((el) => {
      el.setAttribute("placeholder", t(el.getAttribute("data-i18n-placeholder")));
    });
  }

  function setLang(lang) {
    current = dict[lang] ? lang : "zh-Hant";
    document.documentElement.lang = current === "en" ? "en" : "zh-Hant";
    document.documentElement.dataset.lang = current;
    const toggle = document.getElementById("lang-toggle");
    if (toggle) {
      toggle.textContent = current === "en" ? "中文" : "EN";
      toggle.setAttribute("aria-pressed", current === "en" ? "true" : "false");
    }
  }

  let stored = "zh-Hant";
  try {
    stored = window.localStorage.getItem(STORAGE_KEY) || "zh-Hant";
  } catch {}
  setLang(stored === "en" ? "en" : "zh-Hant");
  applyI18n(document);

  window.PaperFrameI18n = { t, setLang, getLang: () => current, applyI18n, STORAGE_KEY };
})();
