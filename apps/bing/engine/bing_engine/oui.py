"""MAC-address vendor lookup (IEEE OUI).

Ships with a curated table of ~150 common vendor prefixes so lookups work
offline out of the box.  ``update()`` downloads the full IEEE registry
(~35k entries) to the user cache dir for exhaustive coverage.
"""

from __future__ import annotations

import os
import re
import urllib.request
from typing import Dict, Optional

# A compact, curated slice of the IEEE OUI registry — the vendors you actually
# meet on a home / office LAN.  Keys are the upper-case 6-hex-digit prefix.
_BUILTIN: Dict[str, str] = {
    "000000": "Xerox", "000C29": "VMware", "005056": "VMware", "000569": "VMware",
    "080027": "Oracle VirtualBox", "0A0027": "VirtualBox Host", "525400": "QEMU/KVM",
    "001C42": "Parallels", "00155D": "Microsoft Hyper-V", "00163E": "Xen",
    # Apple
    "001451": "Apple", "0017F2": "Apple", "001B63": "Apple", "001EC2": "Apple",
    "002332": "Apple", "0025BC": "Apple", "3C0754": "Apple", "40331A": "Apple",
    "4C57CA": "Apple", "5855CA": "Apple", "60FACD": "Apple", "7CD1C3": "Apple",
    "8866A5": "Apple", "A85C2C": "Apple", "AC87A3": "Apple", "B8E856": "Apple",
    "D0817A": "Apple", "F0DBF8": "Apple", "F82793": "Apple", "DCA904": "Apple",
    # Samsung
    "0021D1": "Samsung", "002454": "Samsung", "00E064": "Samsung", "081195": "Samsung",
    "1C5A3E": "Samsung", "34BE00": "Samsung", "5001BB": "Samsung", "8425DB": "Samsung",
    "B857D8": "Samsung", "E8508B": "Samsung", "F409D8": "Samsung",
    # Google / Nest
    "3C5AB4": "Google", "54600A": "Google", "6466B3": "Google", "941882": "Google",
    "A4778D": "Google Nest", "F4F5D8": "Google", "F4F5E8": "Google",
    "1CF29A": "Google", "DA8FCA": "Google",
    # Amazon
    "0871B0": "Amazon", "34D270": "Amazon", "44650D": "Amazon", "68546D": "Amazon",
    "74C246": "Amazon", "84D6D0": "Amazon", "A002DC": "Amazon Kindle/Echo",
    "F0272D": "Amazon", "FC65DE": "Amazon",
    # Raspberry Pi
    "B827EB": "Raspberry Pi", "DCA632": "Raspberry Pi", "E45F01": "Raspberry Pi",
    "2CCF67": "Raspberry Pi", "D83ADD": "Raspberry Pi",
    # Espressif (ESP32 / ESP8266 — IoT)
    "240AC4": "Espressif (ESP32)", "3C71BF": "Espressif", "5CCF7F": "Espressif",
    "84F3EB": "Espressif", "A020A6": "Espressif", "BCDDC2": "Espressif",
    "D8A01D": "Espressif", "ECFABC": "Espressif", "E8DB84": "Espressif",
    "7CDFA1": "Espressif", "C44F33": "Espressif",
    # Routers / networking
    "001A2B": "Ayecom / Router", "0018E7": "Cameo / Router", "F81A67": "TP-Link",
    "1C61B4": "TP-Link", "50C7BF": "TP-Link", "A42BB0": "TP-Link", "C46E1F": "TP-Link",
    "E894F6": "TP-Link", "AC84C6": "TP-Link", "60634C": "TP-Link",
    "001560": "D-Link", "0022B0": "D-Link", "1CBDB9": "D-Link", "340804": "D-Link",
    "C8BE19": "D-Link", "002401": "Netgear", "008EF2": "Netgear", "20E52A": "Netgear",
    "9C3DCF": "Netgear", "A040A0": "Netgear", "C40415": "Netgear",
    "001217": "Cisco-Linksys", "002369": "Cisco", "00259C": "Cisco",
    "0CD996": "Cisco", "3C0518": "Cisco", "F09E63": "Cisco",
    "245EBE": "QNAP", "00089B": "ICP Electronics",
    "24A43C": "Ubiquiti", "44D9E7": "Ubiquiti", "788A20": "Ubiquiti",
    "802AA8": "Ubiquiti", "F492BF": "Ubiquiti", "FCECDA": "Ubiquiti",
    "18E829": "Ubiquiti", "687251": "Ubiquiti", "E063DA": "Ubiquiti",
    "001A11": "ASUS", "04D9F5": "ASUS", "2C56DC": "ASUS", "50465D": "ASUS",
    "AC220B": "ASUS", "D850E6": "ASUS", "F832E4": "ASUS", "1C872C": "ASUS",
    # PC / laptop NICs
    "001B21": "Intel", "0024D7": "Intel", "3C970E": "Intel", "7C7A91": "Intel",
    "8CA982": "Intel", "A0C589": "Intel", "E0946C": "Intel", "F8632D": "Intel",
    "001A73": "Gemtek", "001E58": "WistronNeweb", "086698": "Compal",
    "0026B9": "Dell", "00219B": "Dell", "18DBF2": "Dell", "B885B3": "Dell",
    "441319": "Foxconn / HP", "00215A": "HP", "3822D6": "HP", "9457A5": "HP",
    "3CD92B": "HP", "70106F": "HP", "A45D36": "HP",
    "3CA82A": "Lenovo", "54EE75": "Wistron/Lenovo", "88708C": "Lenovo",
    # Realtek / generic NICs
    "525400": "QEMU/KVM", "000EC6": "ASIX/Realtek", "52540A": "Realtek",
    # Printers
    "0000AA": "Xerox", "0021B7": "Lexmark", "3499AC": "Brother", "008077": "Brother",
    "0080A3": "Lantronix", "9CB70D": "Liteon", "F0D5BF": "Canon",
    # Phones / TVs / media
    "F0EF86": "Google/Chromecast", "F88FCA": "Google", "88C9D0": "LG Electronics",
    "3CBDD8": "LG Electronics", "001E75": "LG", "A816B2": "LG",
    "0009B0": "Onkyo", "B0C554": "Sonos", "5CAAFD": "Sonos", "949F3E": "Sonos",
    "347E5C": "Sonos", "78286D": "Sonos", "AC63BE": "Amazon Fire TV",
    "E4F042": "Google", "B4F1DA": "LG Innotek",
    # Xiaomi / Huawei / OnePlus
    "286C07": "Xiaomi", "34CE00": "Xiaomi", "640980": "Xiaomi", "7451BA": "Xiaomi",
    "F8A45F": "Xiaomi", "0011AC": "Huawei", "00E0FC": "Huawei", "48435A": "Huawei",
    "D40737": "Huawei", "AC3743": "OnePlus", "94652D": "OnePlus",
}


def _cache_path() -> str:
    base = os.environ.get("XDG_CACHE_HOME") or os.path.expanduser("~/.cache")
    return os.path.join(base, "bing", "oui.txt")


def normalize(mac: str) -> Optional[str]:
    """Return the canonical ``AA:BB:CC:DD:EE:FF`` form, or ``None`` if invalid."""
    if not mac:
        return None
    hexdigits = re.sub(r"[^0-9A-Fa-f]", "", mac).upper()
    if len(hexdigits) != 12:
        return None
    return ":".join(hexdigits[i:i + 2] for i in range(0, 12, 2))


def _prefix(mac: str) -> Optional[str]:
    norm = normalize(mac)
    if not norm:
        return None
    return norm.replace(":", "")[:6]


class OuiDatabase:
    """Lazy vendor lookup: curated built-ins plus optional downloaded registry."""

    def __init__(self) -> None:
        self._extra: Dict[str, str] = {}
        self._loaded = False

    def _ensure_loaded(self) -> None:
        if self._loaded:
            return
        self._loaded = True
        path = _cache_path()
        try:
            with open(path, "r", encoding="utf-8", errors="ignore") as fh:
                for row in fh:
                    row = row.strip()
                    if not row or row.startswith("#"):
                        continue
                    pfx, _, name = row.partition("\t")
                    if len(pfx) == 6 and name:
                        self._extra[pfx.upper()] = name
        except OSError:
            pass

    def lookup(self, mac: str) -> Optional[str]:
        pfx = _prefix(mac)
        if not pfx:
            return None
        # Locally administered / random MACs (2nd hex nibble is 2/6/A/E) have no
        # meaningful vendor — many phones randomise these for privacy.
        if len(pfx) == 6 and pfx[1] in "26AE":
            builtin = _BUILTIN.get(pfx)
            if builtin:
                return builtin
            return "Locally administered (randomised)"
        if pfx in _BUILTIN:
            return _BUILTIN[pfx]
        self._ensure_loaded()
        return self._extra.get(pfx)

    def update(self, url: str = "https://standards-oui.ieee.org/oui/oui.csv") -> int:
        """Download the full IEEE OUI CSV and cache a compact prefix→name map.

        Returns the number of entries written.  Honours ``HTTPS_PROXY`` via the
        default urllib proxy handling.
        """
        req = urllib.request.Request(url, headers={"User-Agent": "bing-oui/1.0"})
        with urllib.request.urlopen(req, timeout=60) as resp:
            data = resp.read().decode("utf-8", errors="ignore")

        entries: Dict[str, str] = {}
        for line in data.splitlines():
            # CSV columns: Registry,Assignment,Organization Name,Organization Address
            parts = _csv_split(line)
            if len(parts) < 3:
                continue
            assignment = parts[1].strip().upper().replace("-", "").replace(":", "")
            org = parts[2].strip().strip('"')
            if len(assignment) == 6 and org and assignment[0] in "0123456789ABCDEF":
                entries[assignment] = org

        path = _cache_path()
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write("# Bing OUI cache — prefix<TAB>vendor\n")
            for pfx, org in sorted(entries.items()):
                fh.write(f"{pfx}\t{org}\n")
        self._extra = entries
        self._loaded = True
        return len(entries)


def _csv_split(line: str) -> list:
    """Minimal CSV field splitter that respects double-quoted commas."""
    out, cur, in_q = [], [], False
    for ch in line:
        if ch == '"':
            in_q = not in_q
        elif ch == "," and not in_q:
            out.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
    out.append("".join(cur))
    return out


# Module-level singleton for convenience.
_DB = OuiDatabase()


def lookup(mac: str) -> Optional[str]:
    """Vendor name for a MAC address, or ``None`` if unknown."""
    return _DB.lookup(mac)


def update(url: str = "https://standards-oui.ieee.org/oui/oui.csv") -> int:
    return _DB.update(url)
