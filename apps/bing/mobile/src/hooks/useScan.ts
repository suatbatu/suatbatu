/** Orchestrates a network scan, transparently using Engine mode or the
 *  on-device scanner depending on settings. Streams devices as they arrive. */

import { useCallback, useRef, useState } from "react";
import { scanNetwork, scannerMode, type ScanProgress } from "../scanner/discovery";
import { localNetwork } from "../scanner/netinfo";
import { useSettings } from "../store/settings";
import type { Device, NetworkInfo } from "../types";

export interface ScanState {
  devices: Device[];
  net: NetworkInfo | null;
  scanning: boolean;
  progress: ScanProgress | null;
  error: string | null;
  activeMode: "device" | "engine";
  scannerCapability: "native-tcp" | "fetch-fallback";
}

export function useScan() {
  const settings = useSettings();
  const [state, setState] = useState<ScanState>({
    devices: [], net: null, scanning: false, progress: null, error: null,
    activeMode: "device", scannerCapability: scannerMode(),
  });
  const stopRef = useRef(false);

  const start = useCallback(async () => {
    stopRef.current = false;
    const mode = settings.effectiveMode();
    setState((s) => ({ ...s, scanning: true, devices: [], progress: null, error: null, activeMode: mode }));

    try {
      if (mode === "engine" && settings.engine) {
        const net = await settings.engine.net(true).catch(() => null);
        setState((s) => ({ ...s, net }));
        const devices = await settings.engine.scan(settings.customCidr || undefined);
        setState((s) => ({ ...s, devices, scanning: false, progress: { scanned: 1, total: 1, found: devices.length, phase: "done" } }));
        return;
      }

      // On-device scan
      const net = await localNetwork();
      setState((s) => ({ ...s, net }));
      const byIp = new Map<string, Device>();
      const devices = await scanNetwork({
        net,
        cidr: settings.customCidr || undefined,
        onDevice: (d) => {
          byIp.set(d.ip, d);
          setState((s) => ({ ...s, devices: Array.from(byIp.values()).sort(sortIp) }));
        },
        onProgress: (p) => setState((s) => ({ ...s, progress: p })),
        shouldStop: () => stopRef.current,
      });
      setState((s) => ({ ...s, devices: devices.sort(sortIp), scanning: false }));
    } catch (e: any) {
      setState((s) => ({ ...s, scanning: false, error: e?.message || String(e) }));
    }
  }, [settings]);

  const stop = useCallback(() => {
    stopRef.current = true;
    setState((s) => ({ ...s, scanning: false }));
  }, []);

  return { ...state, start, stop };
}

const sortIp = (a: Device, b: Device) =>
  (ipNum(a.ip) - ipNum(b.ip));
const ipNum = (ip: string) =>
  ip.split(".").reduce((acc, o) => (acc << 8) + (parseInt(o, 10) || 0), 0) >>> 0;
