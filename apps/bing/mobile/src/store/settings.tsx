/** App-wide settings: which scanning mode to use and the engine address.
 *
 *  Kept in React context (in-memory for this version — wire up AsyncStorage or
 *  expo-secure-store to persist across launches). */

import React, { createContext, useCallback, useContext, useMemo, useState } from "react";
import { EngineClient, normalizeBaseUrl } from "../api/engineClient";

export type ScanMode = "auto" | "device" | "engine";

interface SettingsState {
  mode: ScanMode;
  engineUrl: string;
  engineOnline: boolean | null;
  customCidr: string;
  setMode: (m: ScanMode) => void;
  setEngineUrl: (u: string) => void;
  setCustomCidr: (c: string) => void;
  testEngine: () => Promise<boolean>;
  /** The client if a URL is configured, else null. */
  engine: EngineClient | null;
  /** Resolve the mode actually in effect right now. */
  effectiveMode: () => "device" | "engine";
}

const Ctx = createContext<SettingsState | null>(null);

export function SettingsProvider({ children }: { children: React.ReactNode }) {
  const [mode, setMode] = useState<ScanMode>("auto");
  const [engineUrl, setEngineUrlRaw] = useState("");
  const [engineOnline, setEngineOnline] = useState<boolean | null>(null);
  const [customCidr, setCustomCidr] = useState("");

  const engine = useMemo(
    () => (engineUrl.trim() ? new EngineClient(engineUrl) : null),
    [engineUrl],
  );

  const setEngineUrl = useCallback((u: string) => {
    setEngineUrlRaw(u);
    setEngineOnline(null);
  }, []);

  const testEngine = useCallback(async () => {
    if (!engine) {
      setEngineOnline(false);
      return false;
    }
    const ok = await engine.ping();
    setEngineOnline(ok);
    return ok;
  }, [engine]);

  const effectiveMode = useCallback((): "device" | "engine" => {
    if (mode === "engine") return "engine";
    if (mode === "device") return "device";
    // auto: use engine when configured and last known to be online
    return engine && engineOnline ? "engine" : "device";
  }, [mode, engine, engineOnline]);

  const value: SettingsState = {
    mode, engineUrl, engineOnline, customCidr,
    setMode, setEngineUrl: (u) => setEngineUrl(normalizeBaseUrl(u) === "" ? "" : u),
    setCustomCidr, testEngine, engine, effectiveMode,
  };
  return <Ctx.Provider value={value}>{children}</Ctx.Provider>;
}

export function useSettings(): SettingsState {
  const ctx = useContext(Ctx);
  if (!ctx) throw new Error("useSettings must be used within SettingsProvider");
  return ctx;
}
