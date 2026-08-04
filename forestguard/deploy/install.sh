#!/usr/bin/env bash
# Install ForestGuard as a systemd service on Raspberry Pi OS (or any Debian).
# Idempotent: safe to re-run to update the code in place.
#
#   sudo ./deploy/install.sh
#
# Assumes the NVMe SSD (from the SSD HAT) is mounted at /mnt/ssd. See
# docs/DEPLOYMENT.md for the SSD + PCIe setup that comes first.
set -euo pipefail

APP_USER=forestguard
APP_DIR=/opt/forestguard
DATA_DIR=/mnt/ssd/forestguard
ENV_FILE=/etc/forestguard.env
UNIT=/etc/systemd/system/forestguard.service
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ $EUID -ne 0 ]]; then
  echo "Please run as root (sudo)." >&2; exit 1
fi

echo "==> Checking Python 3"
command -v python3 >/dev/null || { echo "python3 not found"; exit 1; }
python3 --version

echo "==> Creating service user '$APP_USER'"
id -u "$APP_USER" >/dev/null 2>&1 || useradd --system --no-create-home --shell /usr/sbin/nologin "$APP_USER"

echo "==> Checking SSD data directory ($DATA_DIR)"
if ! mountpoint -q /mnt/ssd; then
  echo "WARNING: /mnt/ssd is not a mount point. The database will live on the SD"
  echo "         card, which wears out. See docs/DEPLOYMENT.md to mount the SSD first."
fi
mkdir -p "$DATA_DIR"
chown -R "$APP_USER:$APP_USER" "$DATA_DIR"

echo "==> Installing application to $APP_DIR"
mkdir -p "$APP_DIR"
cp -r "$SRC_DIR/app" "$SRC_DIR/web" "$SRC_DIR/scripts" "$SRC_DIR/run.py" "$APP_DIR/"
chown -R "$APP_USER:$APP_USER" "$APP_DIR"

echo "==> Installing environment file ($ENV_FILE)"
if [[ ! -f "$ENV_FILE" ]]; then
  cp "$SRC_DIR/.env.example" "$ENV_FILE"
  chmod 600 "$ENV_FILE"
  chown root:root "$ENV_FILE"
  echo "    Created $ENV_FILE from template."
  echo "    >>> EDIT IT NOW: set FG_INGEST_KEYS and FG_ADMIN_TOKEN to real secrets."
else
  echo "    $ENV_FILE already exists; leaving it untouched."
fi

echo "==> Installing systemd unit"
cp "$SRC_DIR/deploy/forestguard.service" "$UNIT"
systemctl daemon-reload
systemctl enable forestguard.service

echo "==> Starting service"
systemctl restart forestguard.service
sleep 1
systemctl --no-pager --lines=8 status forestguard.service || true

echo
echo "Done. ForestGuard is installed."
echo "  - Config:   $ENV_FILE   (edit secrets, then: systemctl restart forestguard)"
echo "  - Data:     $DATA_DIR"
echo "  - Logs:     journalctl -u forestguard -f"
echo "  - Dashboard: http://<pi-ip>:8080/  (put it behind TLS for anything remote)"
