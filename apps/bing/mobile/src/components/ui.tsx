/** Small reusable UI primitives styled with the Bing theme. */
import React from "react";
import {
  ActivityIndicator, Pressable, StyleSheet, Text, TextStyle, View, ViewStyle,
} from "react-native";
import { colors, font, radius, spacing } from "../theme";

export function Card({ children, style }: { children: React.ReactNode; style?: ViewStyle }) {
  return <View style={[styles.card, style]}>{children}</View>;
}

export function SectionTitle({ children }: { children: React.ReactNode }) {
  return <Text style={styles.sectionTitle}>{children}</Text>;
}

export function Muted({ children, style }: { children: React.ReactNode; style?: TextStyle }) {
  return <Text style={[styles.muted, style]}>{children}</Text>;
}

export function Chip({ label, tone = "default" }: { label: string; tone?: "default" | "gw" | "self" | "warn" }) {
  const toneStyle =
    tone === "gw" ? styles.chipGw : tone === "self" ? styles.chipSelf : tone === "warn" ? styles.chipWarn : styles.chipDefault;
  const textTone =
    tone === "gw" ? { color: colors.warn } : tone === "self" ? { color: colors.good } : tone === "warn" ? { color: colors.warn } : { color: colors.muted };
  return (
    <View style={[styles.chip, toneStyle]}>
      <Text style={[styles.chipText, textTone]}>{label}</Text>
    </View>
  );
}

export function Button({
  title, onPress, variant = "primary", loading, disabled, style,
}: {
  title: string; onPress: () => void; variant?: "primary" | "ghost";
  loading?: boolean; disabled?: boolean; style?: ViewStyle;
}) {
  return (
    <Pressable
      onPress={onPress}
      disabled={disabled || loading}
      style={({ pressed }) => [
        styles.btn, variant === "primary" ? styles.btnPrimary : styles.btnGhost,
        (disabled || loading) && { opacity: 0.5 }, pressed && { opacity: 0.85 }, style,
      ]}
    >
      {loading ? (
        <ActivityIndicator color="#fff" size="small" />
      ) : (
        <Text style={[styles.btnText, variant === "ghost" && { color: colors.text }]}>{title}</Text>
      )}
    </Pressable>
  );
}

export function KV({ k, v }: { k: string; v: React.ReactNode }) {
  return (
    <View style={styles.kvRow}>
      <Text style={styles.kvK}>{k}</Text>
      <Text style={styles.kvV} numberOfLines={1}>{v ?? "—"}</Text>
    </View>
  );
}

export function StatTile({ label, value, accent }: { label: string; value: string; accent?: boolean }) {
  return (
    <View style={[styles.tile, accent && { backgroundColor: colors.panel2 }]}>
      <Text style={styles.tileLabel}>{label.toUpperCase()}</Text>
      <Text style={[styles.tileValue, accent && { color: colors.accent2 }]} numberOfLines={1}>{value}</Text>
    </View>
  );
}

export function Mono({ children, style }: { children: React.ReactNode; style?: TextStyle }) {
  return <Text style={[styles.mono, style]}>{children}</Text>;
}

const styles = StyleSheet.create({
  card: {
    backgroundColor: colors.panel, borderColor: colors.border, borderWidth: 1,
    borderRadius: radius.lg, padding: spacing.lg,
  },
  sectionTitle: {
    color: colors.muted, fontSize: font.small, fontWeight: "700",
    letterSpacing: 0.8, textTransform: "uppercase", marginBottom: spacing.sm,
  },
  muted: { color: colors.muted, fontSize: font.body },
  chip: { paddingHorizontal: 8, paddingVertical: 3, borderRadius: 20 },
  chipDefault: { backgroundColor: colors.bg2 },
  chipGw: { backgroundColor: "rgba(255,176,32,0.16)" },
  chipSelf: { backgroundColor: "rgba(53,208,127,0.16)" },
  chipWarn: { backgroundColor: "rgba(255,176,32,0.16)" },
  chipText: { fontSize: 10, fontWeight: "800", letterSpacing: 0.5, textTransform: "uppercase" },
  btn: { paddingVertical: 11, paddingHorizontal: 18, borderRadius: radius.md, alignItems: "center", justifyContent: "center", minHeight: 44 },
  btnPrimary: { backgroundColor: colors.accent },
  btnGhost: { backgroundColor: colors.panel, borderColor: colors.border, borderWidth: 1 },
  btnText: { color: "#fff", fontWeight: "700", fontSize: font.body },
  kvRow: { flexDirection: "row", justifyContent: "space-between", paddingVertical: 7, borderBottomColor: "rgba(38,50,86,0.5)", borderBottomWidth: 1, gap: 12 },
  kvK: { color: colors.muted, fontSize: font.body },
  kvV: { color: colors.text, fontSize: font.body, fontWeight: "600", flexShrink: 1, textAlign: "right" },
  tile: {
    backgroundColor: colors.panel, borderColor: colors.border, borderWidth: 1,
    borderRadius: radius.md, padding: spacing.md, minWidth: 108, flex: 1, gap: 3,
  },
  tileLabel: { color: colors.muted, fontSize: 10, letterSpacing: 0.6, fontWeight: "700" },
  tileValue: { color: colors.text, fontSize: font.h3, fontWeight: "800" },
  mono: { color: colors.text, fontFamily: font.mono, fontSize: font.small },
});
