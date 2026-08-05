import { Ionicons } from "@expo/vector-icons";
import { Stack, useLocalSearchParams } from "expo-router";
import React, { useCallback, useState } from "react";
import { ScrollView, StyleSheet, Text, View } from "react-native";
import { Button, Card, KV, Muted, SectionTitle } from "../../src/components/ui";
import { assess } from "../../src/scanner/security";
import { TOP_PORTS } from "../../src/scanner/portlist";
import { portScan } from "../../src/tools/onDevice";
import { useSettings } from "../../src/store/settings";
import { colors, font, glyphFor, spacing } from "../../src/theme";
import type { PortResult, SecurityReport } from "../../src/types";

export default function DeviceDetail() {
  const params = useLocalSearchParams<{ ip: string; name?: string; icon?: string; vendor?: string; type?: string; mac?: string }>();
  const settings = useSettings();
  const ip = String(params.ip);

  const [loading, setLoading] = useState(false);
  const [ports, setPorts] = useState<PortResult[] | null>(null);
  const [security, setSecurity] = useState<SecurityReport | null>(null);
  const [hostname, setHostname] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);

  const deepScan = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      if (settings.effectiveMode() === "engine" && settings.engine) {
        const r = await settings.engine.device(ip);
        setPorts(r.open_ports);
        setSecurity(r.security);
        setHostname(r.hostname);
      } else {
        const open = await portScan(ip, TOP_PORTS);
        setPorts(open);
        setSecurity(assess(ip, open.map((p) => p.port)));
      }
    } catch (e: any) {
      setError(e?.message || String(e));
    } finally {
      setLoading(false);
    }
  }, [ip, settings]);

  const gradeColor = security
    ? { A: colors.good, B: colors.good, C: colors.warn, D: colors.warn, F: colors.bad }[security.grade] || colors.muted
    : colors.muted;

  return (
    <ScrollView style={styles.container} contentContainerStyle={{ padding: spacing.lg, paddingBottom: 48 }}>
      <Stack.Screen options={{ title: params.name ? String(params.name) : ip }} />

      <View style={styles.header}>
        <View style={styles.icon}><Text style={{ fontSize: 30 }}>{glyphFor(String(params.icon || "device"))}</Text></View>
        <View style={{ flex: 1 }}>
          <Text style={styles.title}>{params.name || params.vendor || params.type || "Device"}</Text>
          <Muted style={{ fontFamily: font.mono }}>{ip}{params.mac ? ` · ${params.mac}` : ""}</Muted>
        </View>
      </View>

      <Card style={{ marginBottom: spacing.md }}>
        <KV k="Type" v={String(params.type || "—")} />
        <KV k="Vendor" v={String(params.vendor || "—")} />
        <KV k="Hostname" v={hostname || "—"} />
        <KV k="IP address" v={ip} />
      </Card>

      <Button title={loading ? "Scanning…" : "🔍 Deep scan (ports + security)"} onPress={deepScan} loading={loading} />

      {error && <Text style={styles.error}>{error}</Text>}

      {ports && (
        <View style={styles.section}>
          <SectionTitle>Open ports ({ports.length})</SectionTitle>
          {ports.length === 0 ? (
            <Muted>No open ports found.</Muted>
          ) : (
            <View style={styles.portWrap}>
              {ports.map((p) => (
                <View key={p.port} style={styles.portPill}>
                  <Text style={styles.portNum}>{p.port}</Text>
                  <Text style={styles.portSvc}>{p.service || "?"}</Text>
                </View>
              ))}
            </View>
          )}
        </View>
      )}

      {security && (
        <View style={styles.section}>
          <SectionTitle>Security</SectionTitle>
          <View style={styles.scoreRow}>
            <Text style={[styles.scoreNum, { color: gradeColor }]}>{security.score}</Text>
            <View>
              <Text style={[styles.grade, { color: gradeColor }]}>Grade {security.grade}</Text>
              <Muted style={{ fontSize: font.small }}>{security.findings.length} issue(s) flagged</Muted>
            </View>
          </View>
          {security.findings.map((f) => (
            <View key={f.port} style={styles.finding}>
              <View style={[styles.sev, sevStyle(f.severity)]}>
                <Text style={[styles.sevText, { color: sevColor(f.severity) }]}>{f.severity}</Text>
              </View>
              <Text style={styles.findingText}>:{f.port} {f.message}</Text>
            </View>
          ))}
          {security.encrypted_alternatives.map((a, i) => (
            <View key={i} style={styles.finding}>
              <View style={[styles.sev, { backgroundColor: "rgba(79,140,255,0.16)" }]}>
                <Text style={[styles.sevText, { color: colors.accent }]}>tip</Text>
              </View>
              <Text style={styles.findingText}>{a}</Text>
            </View>
          ))}
        </View>
      )}
    </ScrollView>
  );
}

const sevColor = (s: string) => (s === "high" ? colors.bad : s === "medium" ? colors.warn : colors.muted);
const sevStyle = (s: string) => ({
  backgroundColor: s === "high" ? "rgba(255,92,108,0.16)" : s === "medium" ? "rgba(255,176,32,0.16)" : "rgba(143,160,196,0.16)",
});

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: colors.bg },
  header: { flexDirection: "row", alignItems: "center", gap: spacing.md, marginBottom: spacing.lg },
  icon: { width: 60, height: 60, borderRadius: 16, alignItems: "center", justifyContent: "center", backgroundColor: colors.panel, borderColor: colors.border, borderWidth: 1 },
  title: { color: colors.text, fontSize: font.h2, fontWeight: "800" },
  section: { marginTop: spacing.xl },
  error: { color: colors.bad, marginTop: spacing.md },
  portWrap: { flexDirection: "row", flexWrap: "wrap", gap: 8 },
  portPill: { flexDirection: "row", alignItems: "center", gap: 6, backgroundColor: colors.panel, borderColor: colors.border, borderWidth: 1, borderRadius: 8, paddingHorizontal: 10, paddingVertical: 6 },
  portNum: { color: colors.text, fontFamily: font.mono, fontSize: font.body, fontWeight: "700" },
  portSvc: { color: colors.accent2, fontSize: font.small },
  scoreRow: { flexDirection: "row", alignItems: "center", gap: 14, marginBottom: spacing.md },
  scoreNum: { fontSize: 40, fontWeight: "800" },
  grade: { fontSize: font.h3, fontWeight: "800" },
  finding: { flexDirection: "row", gap: 8, alignItems: "flex-start", paddingVertical: 7, borderBottomColor: "rgba(38,50,86,0.5)", borderBottomWidth: 1 },
  sev: { paddingHorizontal: 7, paddingVertical: 2, borderRadius: 12 },
  sevText: { fontSize: 10, fontWeight: "800", textTransform: "uppercase" },
  findingText: { color: colors.text, fontSize: font.small, flex: 1 },
});
