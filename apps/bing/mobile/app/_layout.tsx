import { Stack } from "expo-router";
import { StatusBar } from "expo-status-bar";
import React from "react";
import { SettingsProvider } from "../src/store/settings";
import { colors } from "../src/theme";

export default function RootLayout() {
  return (
    <SettingsProvider>
      <StatusBar style="light" />
      <Stack
        screenOptions={{
          headerStyle: { backgroundColor: colors.bg },
          headerTintColor: colors.text,
          headerTitleStyle: { fontWeight: "700" },
          contentStyle: { backgroundColor: colors.bg },
        }}
      >
        <Stack.Screen name="(tabs)" options={{ headerShown: false }} />
        <Stack.Screen
          name="device/[ip]"
          options={{ title: "Device", presentation: "card" }}
        />
      </Stack>
    </SettingsProvider>
  );
}
