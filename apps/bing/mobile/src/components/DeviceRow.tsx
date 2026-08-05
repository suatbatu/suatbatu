/** A single device row in the discovery list. */
import { Link } from "expo-router";
import React from "react";
import { Pressable, StyleSheet, Text, View } from "react-native";
import { colors, font, glyphFor, radius, spacing } from "../theme";
import type { Device } from "../types";
import { Chip } from "./ui";

export function DeviceRow({ device }: { device: Device }) {
  const name = device.hostname || device.vendor || device.device_type || "Unknown device";
  const sub = [device.vendor, device.mac].filter(Boolean).join(" · ") || device.device_type;
  const ports = (device.open_ports || []).slice(0, 5).join(", ");
  return (
    <Link
      href={{
        pathname: "/device/[ip]",
        params: {
          ip: device.ip,
          name,
          icon: device.icon,
          vendor: device.vendor || "",
          type: device.device_type,
          mac: device.mac || "",
        },
      }}
      asChild
    >
      <Pressable style={({ pressed }) => [styles.row, pressed && styles.rowPressed]}>
        <View style={styles.icon}>
          <Text style={{ fontSize: 22 }}>{glyphFor(device.icon)}</Text>
        </View>
        <View style={styles.main}>
          <View style={styles.nameRow}>
            <Text style={styles.name} numberOfLines={1}>{name}</Text>
            {device.is_gateway && <Chip label="gateway" tone="gw" />}
            {device.is_self && <Chip label="you" tone="self" />}
          </View>
          <Text style={styles.sub} numberOfLines={1}>{device.device_type} · {sub}</Text>
        </View>
        <View style={styles.right}>
          <Text style={styles.ip}>{device.ip}</Text>
          {!!ports && <Text style={styles.ports}>:{ports}</Text>}
        </View>
      </Pressable>
    </Link>
  );
}

const styles = StyleSheet.create({
  row: {
    flexDirection: "row", alignItems: "center", gap: spacing.md,
    backgroundColor: colors.panel, borderColor: colors.border, borderWidth: 1,
    borderRadius: radius.lg, padding: spacing.md, marginBottom: spacing.sm,
  },
  rowPressed: { backgroundColor: colors.panel2, borderColor: colors.accent },
  icon: {
    width: 46, height: 46, borderRadius: 12, alignItems: "center", justifyContent: "center",
    backgroundColor: colors.bg2, borderColor: colors.border, borderWidth: 1,
  },
  main: { flex: 1, minWidth: 0 },
  nameRow: { flexDirection: "row", alignItems: "center", gap: 8 },
  name: { color: colors.text, fontSize: font.h3, fontWeight: "700", flexShrink: 1 },
  sub: { color: colors.muted, fontSize: font.small, marginTop: 2 },
  right: { alignItems: "flex-end" },
  ip: { color: colors.text, fontFamily: font.mono, fontSize: font.body },
  ports: { color: colors.muted, fontFamily: font.mono, fontSize: 10, marginTop: 2 },
});
