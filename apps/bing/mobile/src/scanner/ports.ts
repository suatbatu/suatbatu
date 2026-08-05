/** Well-known TCP port → service, plus the probe sets used on-device.
 *  Kept in sync with the engine's ports.py (a representative subset). */

export const SERVICES: Record<number, string> = {
  20: "ftp-data", 21: "ftp", 22: "ssh", 23: "telnet", 25: "smtp", 53: "dns",
  67: "dhcp", 80: "http", 110: "pop3", 111: "rpcbind", 135: "msrpc",
  137: "netbios-ns", 139: "netbios-ssn", 143: "imap", 161: "snmp", 389: "ldap",
  443: "https", 445: "smb", 465: "smtps", 515: "printer", 548: "afp",
  554: "rtsp", 587: "smtp", 631: "ipp", 636: "ldaps", 873: "rsync",
  993: "imaps", 995: "pop3s", 1080: "socks", 1433: "mssql", 1521: "oracle",
  1883: "mqtt", 1900: "upnp/ssdp", 2049: "nfs", 2375: "docker", 3000: "dev-http",
  3128: "http-proxy", 3306: "mysql", 3389: "rdp", 5000: "upnp", 5060: "sip",
  5353: "mdns", 5432: "postgresql", 5555: "adb", 5900: "vnc", 5984: "couchdb",
  6379: "redis", 7000: "airplay", 8000: "http-alt", 8008: "chromecast",
  8009: "chromecast", 8080: "http-proxy", 8096: "jellyfin/emby",
  8123: "home-assistant", 8443: "https-alt", 8888: "http-alt", 9000: "portainer",
  9100: "jetdirect", 9200: "elasticsearch", 27017: "mongodb", 32400: "plex",
  62078: "apple-sync",
};

export const serviceName = (port: number): string | null => SERVICES[port] ?? null;

/** Ports probed to decide whether a host is alive — kept small for speed. */
export const LIVENESS_PORTS = [80, 443, 22, 445, 139, 5000, 7000, 8009, 62078,
  548, 53, 3389, 9100, 1883, 8080, 5555, 32400];

/** Ports probed per live host to classify it. */
export const CLASSIFY_PORTS = [22, 80, 443, 445, 139, 135, 8080, 62078, 9100,
  8009, 8008, 554, 32400, 5000, 1883, 3389, 53, 548, 7000, 5900, 631, 515];

/** Ports HTTP(S)-reachable via fetch() — the Expo-Go-only fallback path. */
export const FETCH_PROBE_PORTS = [80, 443, 8080, 8443, 8008, 8009, 8123, 8096,
  5000, 3000, 9000, 32400, 631];
