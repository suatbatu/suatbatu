/** IPv4 / subnet helpers — no dependencies. */

export function ipToInt(ip: string): number {
  const parts = ip.split(".").map((p) => parseInt(p, 10));
  if (parts.length !== 4 || parts.some((n) => Number.isNaN(n) || n < 0 || n > 255)) {
    throw new Error(`invalid IPv4: ${ip}`);
  }
  return ((parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3]) >>> 0;
}

export function intToIp(n: number): string {
  return [(n >>> 24) & 255, (n >>> 16) & 255, (n >>> 8) & 255, n & 255].join(".");
}

export function maskToPrefix(mask: string): number {
  const bits = ipToInt(mask).toString(2).padStart(32, "0");
  return bits.split("").filter((b) => b === "1").length;
}

/** Enumerate usable host addresses for a CIDR (skips network & broadcast). */
export function hostsInCidr(cidr: string): string[] {
  const [base, prefixStr] = cidr.split("/");
  const prefix = parseInt(prefixStr, 10);
  if (Number.isNaN(prefix) || prefix < 8 || prefix > 32) {
    throw new Error(`unsupported prefix: /${prefixStr}`);
  }
  const baseInt = ipToInt(base);
  const maskInt = prefix === 0 ? 0 : (0xffffffff << (32 - prefix)) >>> 0;
  const network = (baseInt & maskInt) >>> 0;
  const broadcast = (network | (~maskInt >>> 0)) >>> 0;
  const hosts: string[] = [];
  if (prefix >= 31) {
    for (let a = network; a <= broadcast; a++) hosts.push(intToIp(a >>> 0));
    return hosts;
  }
  for (let a = network + 1; a < broadcast; a++) hosts.push(intToIp(a >>> 0));
  return hosts;
}

/** Derive the /24 (or given-prefix) CIDR that contains an IP. */
export function cidrForIp(ip: string, prefix = 24): string {
  const maskInt = (0xffffffff << (32 - prefix)) >>> 0;
  const network = (ipToInt(ip) & maskInt) >>> 0;
  return `${intToIp(network)}/${prefix}`;
}
