const I18N = {
  en: {
    tabBoot: "Boot",
    tabStorage: "Install",
    tabMaint: "Tools",
    nextLang: "TH",
    current: "Current System",
    reading: "Checking...",
    choose: "Select Startup System",
    androidDesc: "MyOS / Stock",
    uefiDesc: "Win11 ARM / Linux / Menu",
    entry: "Target",
    btnWin11: "Windows 11 ARM",
    btnLinux: "Linux (DTB/ACPI)",
    btnUefiMenu: "UEFI Boot Menu",
    btnBios: "Unlocked BIOS Setup",
    bootDesc: "Writes a one-shot NEXTv1 request into the SPLITv1 combined boot image and reboots directly.",
    saveReboot: "Save & Reboot",
    deviceStatusTitle: "Device & Boot Status",
    refresh: "Refresh",
    deviceLabel: "Device",
    slotLabel: "Slot",
    modeLabel: "Boot Mode",
    touchLabel: "Touch IC",
    storageTitle: "Combined Boot Installer",
    storageDesc: "Backup stock Android boot v4, install pre-UEFI touch BootSelect + Mu-Silicium UEFI, or restore stock byte-for-byte.",
    timeoutLabel: "Pre-UEFI Selector Timeout (sec):",
    btnInstall: "Backup Stock + Install Combined SunUEFI Boot",
    btnRestore: "Restore Stock Android Boot (RSTRv1)",
    maintTitle: "Hardware Reset Modes & Diagnostics",
    btnClearReq: "Clear Pending NEXTv1 Request",
    clearDesc: "Clear pending one-shot UEFI/Windows/Linux request",
    btnRecovery: "Reboot Recovery",
    btnBootloader: "Reboot Bootloader / Fastboot",
    btnEdl: "Reboot Qualcomm EDL (9008)",
    consoleLabel: "Output Log"
  },
  th: {
    tabBoot: "สลับระบบบูต",
    tabStorage: "ติดตั้ง",
    tabMaint: "บำรุงรักษา",
    nextLang: "ZH",
    current: "ระบบปัจจุบัน",
    reading: "กำลังตรวจสอบ...",
    choose: "เลือกระบบที่จะบูต",
    androidDesc: "รอมศูนย์ Android",
    uefiDesc: "Win11 ARM / Linux / เมนู UEFI",
    entry: "ปลายทาง UEFI",
    btnWin11: "Windows 11 ARM",
    btnLinux: "Linux (DTB/ACPI)",
    btnUefiMenu: "เมนู UEFI (ทัชสกรีน)",
    btnBios: "BIOS Setup (ปลดล็อค)",
    bootDesc: "เขียนคำสั่ง NEXTv1 ลงใน boot ที่รวม BootSelect + UEFI แล้วรีบูตเข้าปลายทางทันที",
    saveReboot: "บันทึกและรีบูต",
    deviceStatusTitle: "สถานะเครื่องและพาร์ติชัน",
    refresh: "รีเฟรช",
    deviceLabel: "อุปกรณ์",
    slotLabel: "สล็อต",
    modeLabel: "โหมดบูต",
    touchLabel: "ทัชสกรีน",
    storageTitle: "ติดตั้ง Combined SunUEFI Boot",
    storageDesc: "สำรอง boot เดิม (v4), ติดตั้ง BootSelect + Mu-Silicium UEFI หรือกู้คืนรอมเดิม 100%",
    timeoutLabel: "เวลานับถอยหลังหน้าเลือกบูต (วินาที):",
    btnInstall: "สำรอง Stock + ติดตั้ง Combined SunUEFI Boot",
    btnRestore: "กู้คืน Stock Android Boot (RSTRv1)",
    maintTitle: "โหมดรีเซ็ตฮาร์ดแวร์และการตรวจสอบ",
    btnClearReq: "ล้างคำสั่ง NEXTv1 ที่ค้างอยู่",
    clearDesc: "ล้างคำสั่งบูต UEFI/Windows/Linux ที่ตั้งค้างไว้",
    btnRecovery: "รีบูตเข้า Recovery",
    btnBootloader: "รีบูตเข้า Bootloader / Fastboot",
    btnEdl: "เข้าโหมด Qualcomm EDL (9008)",
    consoleLabel: "บันทึกการทำงาน"
  },
  zh: {
    tabBoot: "启动",
    tabStorage: "安装与还原",
    tabMaint: "维护",
    nextLang: "EN",
    current: "当前系统",
    reading: "读取中...",
    choose: "选择启动系统",
    androidDesc: "原厂 Android",
    uefiDesc: "Win11 ARM / Linux / 启动菜单",
    entry: "进入目标",
    btnWin11: "Windows 11 ARM",
    btnLinux: "Linux (DTB/ACPI)",
    btnUefiMenu: "触控 UEFI 启动菜单",
    btnBios: "全解锁 BIOS 设置",
    bootDesc: "写入一次性 NEXTv1 引导请求到 SPLITv1 复合分区后直接重启。",
    saveReboot: "保存并重启",
    deviceStatusTitle: "设备与引导状态",
    refresh: "刷新",
    deviceLabel: "设备",
    slotLabel: "槽位",
    modeLabel: "引导状态",
    touchLabel: "触控芯片",
    storageTitle: "SunUEFI 复合引导安装",
    storageDesc: "备份原厂 boot v4，安装触控 BootSelect + Mu-Silicium UEFI，或字节级还原原厂。",
    timeoutLabel: "启动选择器等待时间 (秒):",
    btnInstall: "备份原厂并安装 SunUEFI 复合引导",
    btnRestore: "还原原厂 Android Boot (RSTRv1)",
    maintTitle: "硬件重启模式与诊断",
    btnClearReq: "清除待执行的 NEXTv1 请求",
    clearDesc: "取消已写入的单次 UEFI/Windows/Linux 启动请求",
    btnRecovery: "重启到 Recovery",
    btnBootloader: "重启到 Bootloader / Fastboot",
    btnEdl: "重启到 Qualcomm EDL (9008)",
    consoleLabel: "运行日志"
  }
};

const MOD_HELPER = "/data/adb/modules/np05j_sunuefi_helper/common/np05j-helper.sh";
const logEl = document.getElementById("logOut");
const msgEl = document.getElementById("message");
let curLang = "en";
let activeTab = "boot";
let selectedTarget = "windows";

function appendLog(msg) {
  logEl.textContent = (logEl.textContent === "Ready." ? "" : logEl.textContent + "\n") + msg;
  logEl.scrollTop = logEl.scrollHeight;
}

function setStatusMsg(text, isErr = false) {
  msgEl.textContent = text;
  msgEl.classList.toggle("error", isErr);
}

function setLang(lang) {
  curLang = I18N[lang] ? lang : "en";
  const dict = I18N[curLang];
  document.querySelectorAll("[data-i18n]").forEach((el) => {
    const k = el.getAttribute("data-i18n");
    if (dict[k]) el.textContent = dict[k];
  });
  document.getElementById("language").textContent = dict.nextLang;
  const activeTabBtn = document.querySelector(`.tabs button[data-tab="${activeTab}"] span[data-i18n]`);
  if (activeTabBtn) {
    document.getElementById("page-title").textContent = activeTabBtn.textContent;
  }
}

function execShell(cmd) {
  return new Promise((resolve) => {
    if (window.ksu && typeof window.ksu.exec === "function") {
      const cb = "_ksu_cb_" + Math.random().toString(36).slice(2);
      window[cb] = (code, stdout, stderr) => {
        delete window[cb];
        resolve({ code, stdout: stdout || "", stderr: stderr || "" });
      };
      window.ksu.exec(cmd, "{}", cb);
      return;
    }
    resolve({
      code: 0,
      stdout: "[Preview Mode] " + cmd + "\nslot=_a\nsunuefi_combined=0 (preview)\n",
      stderr: ""
    });
  });
}

async function runHelper(args) {
  appendLog("$ np05j-helper.sh " + args);
  const res = await execShell(`sh ${MOD_HELPER} ${args}`);
  if (res.stdout) appendLog(res.stdout.trim());
  if (res.stderr) appendLog("[stderr] " + res.stderr.trim());
  if (res.code !== 0) {
    setStatusMsg(res.stderr || res.stdout || "Command failed", true);
  } else if (!args.startsWith("status")) {
    setStatusMsg(res.stdout ? res.stdout.trim().split("\n")[0] : "OK", false);
  }
  if (args.startsWith("status")) {
    let slot = "_a";
    let mode = "Stock / Raw";
    for (const line of res.stdout.split("\n")) {
      if (line.startsWith("slot=")) {
        slot = line.slice(5).trim();
        document.getElementById("stSlot").textContent = slot;
      }
      if (line.startsWith("device=")) {
        document.getElementById("stDevice").textContent = line.slice(7).trim();
      }
      if (line.startsWith("sunuefi_combined=")) {
        mode = line.includes("1") ? "SunUEFI SPLITv1" : "Stock / Raw";
        document.getElementById("stMode").textContent = mode;
      }
    }
    document.getElementById("health").textContent = `${slot} | ${mode}`;
  }
}

document.getElementById("language").addEventListener("click", () => {
  const order = ["en", "th", "zh"];
  setLang(order[(order.indexOf(curLang) + 1) % order.length]);
});

document.querySelectorAll(".tabs button[data-tab]").forEach((tab) => {
  tab.addEventListener("click", () => {
    activeTab = tab.getAttribute("data-tab");
    document.querySelectorAll(".tabs button[data-tab]").forEach((t) => {
      const sel = t === tab;
      t.setAttribute("aria-selected", sel ? "true" : "false");
      t.tabIndex = sel ? 0 : -1;
    });
    document.querySelectorAll("main > [role='tabpanel']").forEach((p) => {
      p.hidden = p.id !== `panel-${activeTab}`;
    });
    setLang(curLang);
  });
});

document.querySelectorAll("input[name='route']").forEach((radio) => {
  radio.addEventListener("change", () => {
    const isUefi = document.querySelector("input[name='route']:checked").value === "uefi";
    document.getElementById("entry-group").hidden = !isUefi;
  });
});

document.querySelectorAll("#entries .entry").forEach((chip) => {
  chip.addEventListener("click", () => {
    selectedTarget = chip.getAttribute("data-entry") || "uefi";
    document.querySelectorAll("#entries .entry").forEach((c) => {
      c.setAttribute("aria-pressed", c === chip ? "true" : "false");
    });
  });
});

document.getElementById("btnSaveReboot").addEventListener("click", () => {
  const route = document.querySelector("input[name='route']:checked").value;
  const target = route === "android" ? "android" : selectedTarget;
  runHelper(`reboot ${target}`);
});

document.querySelectorAll("[data-reboot-raw]").forEach((btn) => {
  btn.addEventListener("click", () => runHelper(`reboot ${btn.getAttribute("data-reboot-raw")}`));
});

document.getElementById("btnRefresh").addEventListener("click", () => runHelper("status"));
document.getElementById("btnInstall").addEventListener("click", () => {
  const sec = Number(document.getElementById("selTimeout").value || 3);
  runHelper(`install ${sec}`);
});
document.getElementById("btnRestore").addEventListener("click", () => runHelper("restore"));
document.getElementById("btnClearReq").addEventListener("click", () => runHelper("clear-oneshot"));

setLang("en");
runHelper("status");
