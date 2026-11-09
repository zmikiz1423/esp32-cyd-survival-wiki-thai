const installer = document.querySelector("#installer");
const status = document.querySelector("#install-status");
const version = document.querySelector("#version");
const library = "https://unpkg.com/esp-web-tools@10.4.0/dist/web/install-button.js?module";

async function prepareInstaller() {
  const response = await fetch(installer.getAttribute("manifest"), {
    cache: "no-cache",
    signal: AbortSignal.timeout(10000),
  });
  if (!response.ok) throw new Error("Manifest unavailable");
  const manifest = await response.json();
  if (typeof manifest.version !== "string" ||
      !manifest.builds?.some(build => build.chipFamily === "ESP32" &&
        build.parts?.some(part => part.offset === 0 && part.path === "firmware/survival-wiki-28-full.bin"))) {
    throw new Error("Unexpected firmware manifest");
  }
  version.textContent = manifest.version;
  if (!window.isSecureContext || !navigator.serial) {
    status.textContent = "เปิดหน้านี้ใน Chrome หรือ Edge บนคอมพิวเตอร์เพื่อแฟรช หรือดาวน์โหลดไฟล์ .bin";
    return;
  }
  const timeout = setTimeout(() => {
    status.textContent = "เครื่องมือแฟรชใช้เวลาโหลดนาน ลองรีเฟรชหน้านี้หรือดาวน์โหลดไฟล์ .bin";
  }, 12000);
  try {
    await import(library);
    await customElements.whenDefined("esp-web-install-button");
    installer.hidden = false;
    status.textContent = "ต่อบอร์ดด้วยสาย USB ที่ส่งข้อมูลได้ แล้วกดปุ่มเพื่อเลือกพอร์ต";
  } finally {
    clearTimeout(timeout);
  }
}
prepareInstaller().catch(() => {
  status.textContent = "โหลดเครื่องมือแฟรชไม่ได้ ลองรีเฟรชหน้านี้หรือดาวน์โหลดไฟล์ .bin";
});
