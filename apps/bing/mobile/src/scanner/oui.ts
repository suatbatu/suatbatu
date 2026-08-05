/** Compact MAC-vendor lookup for on-device use. A representative subset of the
 *  engine's table — the engine (or `update-oui`) has the full IEEE registry. */

const OUI: Record<string, string> = {
  "000C29": "VMware", "005056": "VMware", "080027": "VirtualBox", "525400": "QEMU/KVM",
  "001451": "Apple", "0017F2": "Apple", "3C0754": "Apple", "4C57CA": "Apple",
  "60FACD": "Apple", "7CD1C3": "Apple", "A85C2C": "Apple", "DCA904": "Apple",
  "F0DBF8": "Apple", "F82793": "Apple", "AC87A3": "Apple",
  "0021D1": "Samsung", "1C5A3E": "Samsung", "34BE00": "Samsung", "5001BB": "Samsung",
  "B857D8": "Samsung", "E8508B": "Samsung", "F409D8": "Samsung",
  "3C5AB4": "Google", "54600A": "Google", "6466B3": "Google", "941882": "Google",
  "A4778D": "Google Nest", "F4F5D8": "Google", "F4F5E8": "Google",
  "0871B0": "Amazon", "34D270": "Amazon", "44650D": "Amazon", "74C246": "Amazon",
  "84D6D0": "Amazon", "F0272D": "Amazon", "FC65DE": "Amazon",
  "B827EB": "Raspberry Pi", "DCA632": "Raspberry Pi", "E45F01": "Raspberry Pi",
  "2CCF67": "Raspberry Pi", "D83ADD": "Raspberry Pi",
  "240AC4": "Espressif (ESP32)", "3C71BF": "Espressif", "5CCF7F": "Espressif",
  "84F3EB": "Espressif", "A020A6": "Espressif", "BCDDC2": "Espressif",
  "D8A01D": "Espressif", "ECFABC": "Espressif", "7CDFA1": "Espressif",
  "F81A67": "TP-Link", "1C61B4": "TP-Link", "50C7BF": "TP-Link", "A42BB0": "TP-Link",
  "C46E1F": "TP-Link", "E894F6": "TP-Link", "AC84C6": "TP-Link",
  "001560": "D-Link", "1CBDB9": "D-Link", "C8BE19": "D-Link",
  "002401": "Netgear", "20E52A": "Netgear", "9C3DCF": "Netgear", "C40415": "Netgear",
  "002369": "Cisco", "00259C": "Cisco", "0CD996": "Cisco", "F09E63": "Cisco",
  "24A43C": "Ubiquiti", "44D9E7": "Ubiquiti", "788A20": "Ubiquiti", "FCECDA": "Ubiquiti",
  "04D9F5": "ASUS", "2C56DC": "ASUS", "AC220B": "ASUS", "D850E6": "ASUS", "1C872C": "ASUS",
  "001B21": "Intel", "3C970E": "Intel", "7C7A91": "Intel", "8CA982": "Intel", "E0946C": "Intel",
  "0026B9": "Dell", "18DBF2": "Dell", "B885B3": "Dell",
  "00215A": "HP", "3822D6": "HP", "9457A5": "HP", "70106F": "HP",
  "3CA82A": "Lenovo", "88708C": "Lenovo",
  "3499AC": "Brother", "008077": "Brother", "F0D5BF": "Canon", "0021B7": "Lexmark",
  "B0C554": "Sonos", "5CAAFD": "Sonos", "949F3E": "Sonos", "347E5C": "Sonos", "78286D": "Sonos",
  "88C9D0": "LG Electronics", "3CBDD8": "LG Electronics", "A816B2": "LG",
  "286C07": "Xiaomi", "34CE00": "Xiaomi", "640980": "Xiaomi", "F8A45F": "Xiaomi",
  "0011AC": "Huawei", "48435A": "Huawei", "D40737": "Huawei",
  "AC3743": "OnePlus", "94652D": "OnePlus",
};

export function normalizeMac(mac: string): string | null {
  const hex = mac.replace(/[^0-9a-fA-F]/g, "").toUpperCase();
  if (hex.length !== 12) return null;
  return hex.match(/.{2}/g)!.join(":");
}

export function vendorForMac(mac: string | null): string | null {
  if (!mac) return null;
  const hex = mac.replace(/[^0-9a-fA-F]/g, "").toUpperCase();
  if (hex.length < 6) return null;
  const prefix = hex.slice(0, 6);
  // Locally-administered / randomised MAC (2nd nibble 2/6/A/E)
  if ("26AE".includes(prefix[1]) && !OUI[prefix]) return "Randomised MAC";
  return OUI[prefix] ?? null;
}
