/** Design tokens shared across the app — mirrors the Bing web dashboard. */
export const colors = {
  bg: "#0b1020",
  bg2: "#121a30",
  panel: "#16203a",
  panel2: "#1b2748",
  border: "#263256",
  text: "#e8edf9",
  muted: "#8fa0c4",
  accent: "#4f8cff",
  accent2: "#38e0c8",
  good: "#35d07f",
  warn: "#ffb020",
  bad: "#ff5c6c",
  gradientA: "#4f8cff",
  gradientB: "#6b5bff",
};

export const radius = { sm: 8, md: 12, lg: 16, xl: 22 };
export const spacing = { xs: 4, sm: 8, md: 12, lg: 16, xl: 22, xxl: 32 };

export const font = {
  h1: 24,
  h2: 20,
  h3: 16,
  body: 14,
  small: 12,
  mono: "ui-monospace" as const,
};

/** Emoji glyphs for device categories (keys match the engine's `icon` field). */
export const deviceGlyph: Record<string, string> = {
  router: "📶",
  phone: "📱",
  tablet: "📱",
  laptop: "💻",
  desktop: "🖥️",
  server: "🖧",
  printer: "🖨️",
  tv: "📺",
  cast: "📡",
  camera: "🎥",
  speaker: "🔊",
  media: "🎬",
  storage: "🗄️",
  iot: "💡",
  device: "📟",
};

export const glyphFor = (icon?: string) => deviceGlyph[icon || "device"] || deviceGlyph.device;
