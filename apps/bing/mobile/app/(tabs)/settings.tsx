import { Ionicons } from "@expo/vector-icons";
import React, { useState } from "react";
import { Pressable, ScrollView, StyleSheet, Text, TextInput, View } from "react-native";
import { Button, Card, Muted, SectionTitle } from "../../src/components/ui";
import { scannerMode } from "../../src/scanner/discovery";
import { useSettings, type ScanMode } from "../../src/store/settings";
import { colors, font, radius, spacing } from "../../src/theme";

const MODES: { key: ScanMode; label: string; desc: string }[] = [
  { key: "auto", label: "Auto", desc: "Engine if reachable, else on-device" },
  { key: "device", label: "On-device", desc: "Scan directly from this phone" },
  { key: "engine", label: "Engine", desc: "Use a Bing Engine server" },
];

export default function SettingsScreen() {
  const settings = useSettings();
  const [urlDraft, setUrlDraft] = useState(settings.engineUrl);
  const [cidrDraft, setCidrDraft] = useState(settings.customCidr);
  const [testing, setTesting] = useState(false);

  const test = async () => {
    settings.setEngineUrl(urlDraft);
    setTesting(true);
    await settings.testEngine();
    setTesting(false);
  };

  return (
    <ScrollView style={styles.container} contentContainerStyle={{ padding: spacing.lg, gap: spacing.md, paddingBottom: 40 }}>
      <Card>
        <SectionTitle>Scan mode</SectionTitle>
        <View style={{ gap: spacing.sm }}>
          {MODES.map((m) => {
            const active = settings.mode === m.key;
            return (
              <Pressable key={m.key} onPress={() => settings.setMode(m.key)} style={[styles.modeRow, active && styles.modeRowActive]}>
                <Ionicons name={active ? "radio-button-on" : "radio-button-off"} size={20} color={active ? colors.accent : colors.muted} />
                <View style={{ flex: 1 }}>
                  <Text style={styles.modeLabel}>{m.label}</Text>
                  <Muted style={{ fontSize: font.small }}>{m.desc}</Muted>
                </View>
              </Pressable>
            );
          })}
        </View>
      </Card>

      <Card>
        <SectionTitle>Bing Engine</SectionTitle>
        <Muted style={{ fontSize: font.small, marginBottom: spacing.sm }}>
          Run the engine on a computer / Raspberry Pi on the same network
          (`bing web --host 0.0.0.0`) for full ARP scans, MAC vendors, traceroute
          and Wake-on-LAN.
        </Muted>
        <TextInput
          style={styles.input}
          placeholder="192.168.1.10:8787"
          placeholderTextColor={colors.muted}
          autoCapitalize="none"
          autoCorrect={false}
          keyboardType="url"
          value={urlDraft}
          onChangeText={setUrlDraft}
        />
        <View style={styles.statusRow}>
          {settings.engineOnline === true && <Text style={[styles.status, { color: colors.good }]}>● Connected</Text>}
          {settings.engineOnline === false && <Text style={[styles.status, { color: colors.bad }]}>● Not reachable</Text>}
          {settings.engineOnline === null && <Text style={[styles.status, { color: colors.muted }]}>● Not tested</Text>}
          <Button title={testing ? "Testing…" : "Test connection"} onPress={test} loading={testing} variant="ghost" style={{ flex: 1 }} />
        </View>
      </Card>

      <Card>
        <SectionTitle>Target subnet (optional)</SectionTitle>
        <Muted style={{ fontSize: font.small, marginBottom: spacing.sm }}>
          Override the subnet to scan. Leave blank to auto-detect the /24 your
          phone is on.
        </Muted>
        <TextInput
          style={styles.input}
          placeholder="192.168.1.0/24"
          placeholderTextColor={colors.muted}
          autoCapitalize="none"
          autoCorrect={false}
          value={cidrDraft}
          onChangeText={(t) => { setCidrDraft(t); settings.setCustomCidr(t.trim()); }}
        />
      </Card>

      <Card>
        <SectionTitle>Scanner capability</SectionTitle>
        <Muted style={{ fontSize: font.small }}>
          {scannerMode() === "native-tcp"
            ? "✅ Native TCP sockets available — full on-device scanning."
            : "⚠️ Running without the native TCP module (Expo Go). Discovery is limited to HTTP-reachable devices. Build a dev client (`expo run:ios` / `expo run:android`) or use Engine mode for full scans."}
        </Muted>
      </Card>

      <Muted style={{ textAlign: "center", fontSize: 12 }}>Bing Mobile v1.0.0 · iOS & Android</Muted>
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: colors.bg },
  modeRow: { flexDirection: "row", alignItems: "center", gap: 12, padding: spacing.md, borderRadius: radius.md, borderColor: colors.border, borderWidth: 1, backgroundColor: colors.bg2 },
  modeRowActive: { borderColor: colors.accent, backgroundColor: colors.panel2 },
  modeLabel: { color: colors.text, fontSize: font.body, fontWeight: "700" },
  input: { backgroundColor: colors.bg2, borderColor: colors.border, borderWidth: 1, borderRadius: radius.sm, color: colors.text, paddingHorizontal: 12, paddingVertical: 10, fontSize: font.body },
  statusRow: { flexDirection: "row", alignItems: "center", gap: 12, marginTop: spacing.sm },
  status: { fontSize: font.small, fontWeight: "700" },
});
