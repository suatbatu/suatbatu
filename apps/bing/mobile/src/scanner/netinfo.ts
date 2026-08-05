/** Read this device's network context via expo-network.
 *
 *  Mobile OSes don't expose the subnet mask to sandboxed apps, so we assume a
 *  /24 (the near-universal home/office default). Users can override the target
 *  subnet in Settings if their LAN is larger. */

import * as Network from "expo-network";
import type { NetworkInfo } from "../types";
import { cidrForIp } from "./subnet";

export async function localNetwork(): Promise<NetworkInfo> {
  let ip: string | null = null;
  try {
    ip = await Network.getIpAddressAsync();
  } catch {
    ip = null;
  }
  const isValid = ip && /^\d+\.\d+\.\d+\.\d+$/.test(ip) && ip !== "0.0.0.0";
  const cidr = isValid ? cidrForIp(ip!, 24) : null;
  // The gateway is conventionally .1 of the subnet on consumer routers.
  const gateway = cidr ? cidr.replace(/\.0\/24$/, ".1") : null;

  return {
    hostname: null,
    primary_ipv4: isValid ? ip : null,
    gateway,
    netmask: cidr ? "255.255.255.0" : null,
    cidr,
  };
}

export async function isWifi(): Promise<boolean> {
  try {
    const state = await Network.getNetworkStateAsync();
    return state.type === Network.NetworkStateType.WIFI;
  } catch {
    return false;
  }
}
