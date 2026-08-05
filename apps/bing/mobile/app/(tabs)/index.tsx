import { Ionicons } from "@expo/vector-icons";
import React, { useMemo } from "react";
import { FlatList, Pressable, StyleSheet, Text, View } from "react-native";
import { useSafeAreaInsets } from "react-native-safe-area-context";
import { DeviceRow } from "../../src/components/DeviceRow";
import { Muted, StatTile } from "../../src/components/ui";
import { useScan } from "../../src/hooks/useScan";
import { colors, font, radius, spacing } from "../../src/theme";

export default function DevicesScreen() {
  const insets = useSafeAreaInsets();
  const scan = useScan();

  const subtitle = useMemo(() => {
    if (scan.activeMode === "engine") return "Engine mode · full ARP scan";
    return scan.scannerCapability === "native-tcp"
      ? "On-device · native TCP scan"
      : "On-device · fetch fallback (build a dev client for full scans)";
  }, [scan.activeMode, scan.scannerCapability]);

  const pct = scan.progress
    ? Math.round((scan.progress.scanned / Math.max(1, scan.progress.total)) * 100)
    : 0;

  return (
    <View style={[styles.container, { paddingTop: insets.top ? 0 : spacing.md }]}>
      <FlatList
        data={scan.devices}
        keyExtractor={(d) => d.ip}
        renderItem={({ item }) => <DeviceRow device={item} />}
        contentContainerStyle={styles.listContent}
        ListHeaderComponent={
          <View>
            <View style={styles.tiles}>
              <StatTile label="Network" value={scan.net?.cidr || "—"} />
              <StatTile label="Gateway" value={scan.net?.gateway || "—"} />
              <StatTile label="Devices" value={String(scan.devices.length)} accent />
            </View>

            <Pressable
              onPress={scan.scanning ? scan.stop : scan.start}
              style={({ pressed }) => [styles.scanBtn, pressed && { opacity: 0.9 }]}
            >
              <Ionicons name={scan.scanning ? "stop-circle" : "radio"} size={20} color="#fff" />
              <Text style={styles.scanBtnText}>
                {scan.scanning ? "Stop scan" : "Scan network"}
              </Text>
            </Pressable>
            <Text style={styles.modeLine}>{subtitle}</Text>

            {scan.scanning && (
              <View style={styles.progressWrap}>
                <View style={styles.progressBar}>
                  <View style={[styles.progressFill, { width: `${Math.max(6, pct)}%` }]} />
                </View>
                <Muted style={{ fontSize: font.small }}>
                  {scan.progress?.phase === "classify" ? "Identifying devices…" : `Sweeping… ${scan.progress?.found ?? 0} found`}
                </Muted>
              </View>
            )}

            {scan.error && (
              <View style={styles.errorBox}>
                <Ionicons name="warning" size={16} color={colors.bad} />
                <Text style={styles.errorText}>{scan.error}</Text>
              </View>
            )}
          </View>
        }
        ListEmptyComponent={
          !scan.scanning ? (
            <View style={styles.empty}>
              <Text style={styles.emptyIcon}>📡</Text>
              <Muted style={{ textAlign: "center" }}>
                Tap <Text style={{ color: colors.text, fontWeight: "700" }}>Scan network</Text> to
                discover the devices on your Wi-Fi.
              </Muted>
            </View>
          ) : null
        }
      />
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: colors.bg },
  listContent: { padding: spacing.lg, paddingBottom: 40 },
  tiles: { flexDirection: "row", gap: spacing.sm, marginBottom: spacing.md },
  scanBtn: {
    flexDirection: "row", alignItems: "center", justifyContent: "center", gap: 10,
    backgroundColor: colors.accent, borderRadius: radius.md, paddingVertical: 14,
  },
  scanBtnText: { color: "#fff", fontWeight: "800", fontSize: font.h3 },
  modeLine: { color: colors.muted, fontSize: font.small, textAlign: "center", marginTop: 8, marginBottom: spacing.md },
  progressWrap: { gap: 6, marginBottom: spacing.md },
  progressBar: { height: 6, backgroundColor: colors.panel, borderRadius: 4, overflow: "hidden" },
  progressFill: { height: "100%", backgroundColor: colors.accent2 },
  errorBox: {
    flexDirection: "row", alignItems: "center", gap: 8, backgroundColor: "rgba(255,92,108,0.12)",
    borderColor: colors.bad, borderWidth: 1, borderRadius: radius.md, padding: spacing.md, marginBottom: spacing.md,
  },
  errorText: { color: colors.bad, flex: 1, fontSize: font.small },
  empty: { alignItems: "center", paddingVertical: 60, gap: 12 },
  emptyIcon: { fontSize: 46 },
});
