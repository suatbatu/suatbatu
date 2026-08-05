import React, { useCallback, useEffect, useState } from "react";
import { RefreshControl, ScrollView, StyleSheet } from "react-native";
import { Card, KV, Muted, SectionTitle } from "../../src/components/ui";
import { localNetwork } from "../../src/scanner/netinfo";
import { useSettings } from "../../src/store/settings";
import { colors, spacing } from "../../src/theme";
import type { NetworkInfo } from "../../src/types";

interface PublicInfo { public_ip?: string | null; isp?: string | null; location?: string | null; }

export default function NetworkScreen() {
  const settings = useSettings();
  const [net, setNet] = useState<NetworkInfo | null>(null);
  const [pub, setPub] = useState<PublicInfo>({});
  const [refreshing, setRefreshing] = useState(false);

  const load = useCallback(async () => {
    setRefreshing(true);
    try {
      if (settings.effectiveMode() === "engine" && settings.engine) {
        const n = await settings.engine.net(true);
        setNet(n);
        setPub({ public_ip: n.public_ip, isp: n.isp, location: n.location });
      } else {
        setNet(await localNetwork());
        // Public info via a simple HTTP endpoint (works without the engine).
        try {
          const r = await fetch("https://ipapi.co/json/");
          const d = await r.json();
          setPub({ public_ip: d.ip, isp: d.org, location: [d.city, d.region, d.country_name].filter(Boolean).join(", ") });
        } catch { /* offline is fine */ }
      }
    } finally {
      setRefreshing(false);
    }
  }, [settings]);

  useEffect(() => { load(); }, [load]);

  return (
    <ScrollView
      style={styles.container}
      contentContainerStyle={{ padding: spacing.lg, gap: spacing.md, paddingBottom: 40 }}
      refreshControl={<RefreshControl refreshing={refreshing} onRefresh={load} tintColor={colors.accent} />}
    >
      <Card>
        <SectionTitle>This device</SectionTitle>
        <KV k="Local IP" v={net?.primary_ipv4 || "—"} />
        <KV k="Gateway" v={net?.gateway || "—"} />
        <KV k="Netmask" v={net?.netmask || "—"} />
        <KV k="Subnet" v={net?.cidr || "—"} />
        {net?.dns_servers && net.dns_servers.length > 0 && <KV k="DNS" v={net.dns_servers.join(", ")} />}
      </Card>

      <Card>
        <SectionTitle>Internet</SectionTitle>
        <KV k="Public IP" v={pub.public_ip || "—"} />
        <KV k="ISP" v={pub.isp || "—"} />
        <KV k="Location" v={pub.location || "—"} />
      </Card>

      {net?.interfaces && net.interfaces.length > 0 && (
        <Card>
          <SectionTitle>Interfaces</SectionTitle>
          {net.interfaces.map((i) => (
            <KV key={i.name} k={i.name} v={`${i.ipv4 || "—"}${i.mac ? " · " + i.mac : ""}`} />
          ))}
        </Card>
      )}

      <Muted style={{ textAlign: "center", fontSize: 12 }}>
        Pull to refresh · {settings.effectiveMode() === "engine" ? "via engine" : "on-device"}
      </Muted>
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: colors.bg },
});
