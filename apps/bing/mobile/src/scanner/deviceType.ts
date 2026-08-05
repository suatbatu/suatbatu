/** Classify a device from vendor / open ports / hostname.
 *  Ported from the engine's discovery.guess_device so on-device and
 *  engine-mode results agree. Returns { type, icon }. */

import type { Device } from "../types";

export function classify(dev: Partial<Device>): { device_type: string; icon: string } {
  const vendor = (dev.vendor || "").toLowerCase();
  const host = (dev.hostname || "").toLowerCase();
  const ports = new Set(dev.open_ports || []);
  const has = (...ps: number[]) => ps.some((p) => ports.has(p));

  if (dev.is_gateway) return t("Router / Gateway", "router");

  const hostSignals: [string, string, string][] = [
    ["iphone", "iPhone", "phone"], ["ipad", "iPad", "tablet"],
    ["macbook", "MacBook", "laptop"], ["android", "Android device", "phone"],
    ["printer", "Printer", "printer"], ["chromecast", "Chromecast", "cast"],
    ["roku", "Streaming device", "cast"], ["camera", "IP Camera", "camera"],
    ["nas", "NAS / Storage", "storage"], ["router", "Router / Gateway", "router"],
    ["-tv", "Smart TV", "tv"], ["appletv", "Apple TV", "tv"],
  ];
  for (const [needle, name, icon] of hostSignals) {
    if (host.includes(needle)) return t(name, icon);
  }

  if (has(9100, 515, 631)) return t("Printer", "printer");
  if (ports.has(32400)) return t("Plex media server", "media");
  if (has(8009, 8008)) return t("Chromecast / Cast", "cast");
  if (ports.has(554) && (vendor.includes("camera") || ports.has(80))) return t("IP Camera", "camera");
  if (vendor.includes("sonos")) return t("Sonos speaker", "speaker");

  if (vendor.includes("espressif")) return t("IoT device (ESP)", "iot");
  if (vendor.includes("raspberry")) return t("Raspberry Pi", "server");
  if (vendor.includes("apple")) {
    if (ports.has(62078)) return t("iPhone / iPad", "phone");
    return t("Apple device", "laptop");
  }
  if (vendor.includes("samsung")) return t("Samsung device", "phone");
  if (["xiaomi", "huawei", "oneplus"].some((v) => vendor.includes(v))) return t("Mobile device", "phone");
  if (vendor.includes("amazon")) return t("Amazon device", "speaker");
  if (vendor.includes("google") || vendor.includes("nest")) return t("Google device", "cast");

  if (has(445, 139, 135, 3389)) return t("Windows PC", "desktop");
  if (ports.has(22) && has(80, 443)) return t("Server", "server");
  if (ports.has(22)) return t("Computer / Linux", "desktop");
  if (has(80, 443)) return t("Web device", "device");
  if (has(1883, 8883, 5683)) return t("IoT device", "iot");
  return t("Network device", "device");
}

const t = (device_type: string, icon: string) => ({ device_type, icon });
