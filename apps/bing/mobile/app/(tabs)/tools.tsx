import React, { useState } from "react";
import { ScrollView, StyleSheet, Text, TextInput, View } from "react-native";
import { Button, Card, Muted } from "../../src/components/ui";
import { vendorForMac, normalizeMac } from "../../src/scanner/oui";
import { TOP_PORTS } from "../../src/scanner/portlist";
import { dohLookup, portScan, speedTest, tcpPing } from "../../src/tools/onDevice";
import { useSettings } from "../../src/store/settings";
import { colors, font, radius, spacing } from "../../src/theme";

export default function ToolsScreen() {
  const settings = useSettings();
  const engineMode = () => settings.effectiveMode() === "engine" && settings.engine;

  return (
    <ScrollView style={styles.container} contentContainerStyle={{ padding: spacing.lg, paddingBottom: 40, gap: spacing.md }}>
      <Muted style={{ marginBottom: 4 }}>
        {engineMode() ? "Using Bing Engine for full-power tools." : "Running tools on-device. Set an engine in Settings for traceroute & Wake-on-LAN."}
      </Muted>

      <ToolCard
        title="🏓 Ping"
        desc="Measure latency & packet loss to a host."
        inputs={[{ key: "host", placeholder: "host or IP (e.g. 1.1.1.1)" }]}
        run={async ({ host }) => {
          if (!host) return "Enter a host.";
          const r = engineMode() ? await settings.engine!.pingHost(host, 5) : await tcpPing(host, 443, 5);
          if (!r.address) return "Could not resolve host.";
          const s = r.summary;
          return `${r.host} [${r.address}] via ${r.method}\n${r.transmitted} sent, ${r.received} received, ${r.loss_pct}% loss\n` +
            (s.avg != null ? `rtt min/avg/max = ${s.min}/${s.avg}/${s.max} ms · jitter ${s.jitter} ms` : "no replies");
        }}
      />

      <ToolCard
        title="🔌 Port scan"
        desc="Find open TCP ports & services."
        inputs={[{ key: "host", placeholder: "host or IP" }, { key: "spec", placeholder: "common / top100", value: "common" }]}
        run={async ({ host, spec }) => {
          if (!host) return "Enter a host.";
          if (engineMode()) {
            const r = await settings.engine!.ports(host, spec || "common");
            const lines = r.open.length ? r.open.map((p) => `OPEN ${String(p.port).padStart(5)}/tcp  ${p.service || "?"}`).join("\n") : "No open ports.";
            return `${lines}\n\nSecurity: ${r.security.score}/100 (${r.security.grade})`;
          }
          const open = await portScan(host, TOP_PORTS);
          return open.length ? open.map((p) => `OPEN ${String(p.port).padStart(5)}/tcp  ${p.service || "?"}`).join("\n") : "No open ports found.";
        }}
      />

      <ToolCard
        title="🌐 DNS lookup"
        desc="Resolve records (A, AAAA, MX, TXT, NS…)."
        inputs={[{ key: "name", placeholder: "example.com" }, { key: "type", placeholder: "A,AAAA,MX", value: "A,AAAA,MX" }]}
        run={async ({ name, type }) => {
          if (!name) return "Enter a name.";
          const types = (type || "A").split(",");
          if (engineMode()) {
            const r = await settings.engine!.dns(name, type || "A");
            return r.records.length ? r.records.map((x) => `${x.type.padEnd(6)} ${String(x.ttl).padStart(6)}  ${x.value}`).join("\n") : "No records.";
          }
          const recs = await dohLookup(name, types);
          return recs.length ? recs.map((x) => `${x.type.padEnd(6)} ${String(x.ttl).padStart(6)}  ${x.value}`).join("\n") : "No records.";
        }}
      />

      <ToolCard
        title="⚡ Speed test"
        desc="Latency, download & upload of your line."
        inputs={[]}
        buttonLabel="Run test"
        run={async () => {
          const r = engineMode() ? await settings.engine!.speed(true) : await speedTest(true);
          if (r.error) return r.error;
          return `Server:   ${r.server || "—"}\nISP:      ${r.isp || "—"}\nLatency:  ${r.latency_ms ?? "—"} ms\nDownload: ${r.download_mbps ?? "—"} Mbps\nUpload:   ${r.upload_mbps ?? "—"} Mbps`;
        }}
      />

      <ToolCard
        title="🛰️ Traceroute"
        desc="See the network path to a host. (Engine mode)"
        inputs={[{ key: "host", placeholder: "host or IP" }]}
        run={async ({ host }) => {
          if (!engineMode()) return "Traceroute needs raw sockets — configure a Bing Engine in Settings.";
          if (!host) return "Enter a host.";
          const r = await settings.engine!.trace(host);
          if (!r.address) return r.note || "Could not resolve host.";
          let t = (r.hops || []).map((h: any) => `${String(h.ttl).padStart(2)}. ${(h.address || "*").padEnd(16)} ${h.rtt_ms != null ? h.rtt_ms + " ms" : "*"}${h.final ? " ✔" : ""}`).join("\n");
          if (r.note) t += `\n${r.note}`;
          return t;
        }}
      />

      <ToolCard
        title="⏰ Wake-on-LAN"
        desc="Power on a sleeping device by MAC. (Engine mode)"
        inputs={[{ key: "mac", placeholder: "AA:BB:CC:DD:EE:FF" }]}
        run={async ({ mac }) => {
          if (!mac) return "Enter a MAC address.";
          if (!engineMode()) return "Wake-on-LAN sends a UDP broadcast — configure a Bing Engine in Settings.";
          const r = await settings.engine!.wol(mac);
          return r.sent ? `Magic packet sent to ${r.mac}.` : "Failed to send.";
        }}
      />

      <ToolCard
        title="🏷️ MAC vendor"
        desc="Identify a device maker from its MAC."
        inputs={[{ key: "mac", placeholder: "AA:BB:CC:DD:EE:FF" }]}
        run={async ({ mac }) => {
          if (!mac) return "Enter a MAC address.";
          if (engineMode()) {
            const r = await settings.engine!.vendor(mac);
            return r.mac ? `${r.mac} → ${r.vendor || "unknown"}` : "Invalid MAC address.";
          }
          const norm = normalizeMac(mac);
          if (!norm) return "Invalid MAC address.";
          return `${norm} → ${vendorForMac(mac) || "unknown (try Engine mode for full DB)"}`;
        }}
      />
    </ScrollView>
  );
}

type InputCfg = { key: string; placeholder: string; value?: string };

function ToolCard({
  title, desc, inputs, run, buttonLabel = "Run",
}: {
  title: string; desc: string; inputs: InputCfg[];
  run: (vals: Record<string, string>) => Promise<string>; buttonLabel?: string;
}) {
  const [vals, setVals] = useState<Record<string, string>>(() =>
    Object.fromEntries(inputs.map((i) => [i.key, i.value || ""])));
  const [out, setOut] = useState<string>("");
  const [busy, setBusy] = useState(false);

  const onRun = async () => {
    setBusy(true);
    setOut("");
    try {
      setOut(await run(vals));
    } catch (e: any) {
      setOut(`Error: ${e?.message || String(e)}`);
    } finally {
      setBusy(false);
    }
  };

  return (
    <Card>
      <Text style={styles.toolTitle}>{title}</Text>
      <Muted style={{ marginBottom: spacing.md, fontSize: font.small }}>{desc}</Muted>
      <View style={{ gap: spacing.sm }}>
        {inputs.map((i) => (
          <TextInput
            key={i.key}
            style={styles.input}
            placeholder={i.placeholder}
            placeholderTextColor={colors.muted}
            autoCapitalize="none"
            autoCorrect={false}
            value={vals[i.key]}
            onChangeText={(t) => setVals((v) => ({ ...v, [i.key]: t }))}
          />
        ))}
        <Button title={busy ? "Working…" : buttonLabel} onPress={onRun} loading={busy} />
      </View>
      {!!out && (
        <View style={styles.outBox}>
          <Text style={styles.outText}>{out}</Text>
        </View>
      )}
    </Card>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: colors.bg },
  toolTitle: { color: colors.text, fontSize: font.h3, fontWeight: "700" },
  input: {
    backgroundColor: colors.bg2, borderColor: colors.border, borderWidth: 1,
    borderRadius: radius.sm, color: colors.text, paddingHorizontal: 12, paddingVertical: 10, fontSize: font.body,
  },
  outBox: { backgroundColor: "#0a0f1e", borderColor: colors.border, borderWidth: 1, borderRadius: radius.sm, padding: spacing.md, marginTop: spacing.md },
  outText: { color: "#cdd8f0", fontFamily: font.mono, fontSize: font.small },
});
