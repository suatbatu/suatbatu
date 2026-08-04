# ForestGuard — Deployment on Raspberry Pi 5 (8 GB) + SSD HAT

This is the target platform: a Raspberry Pi 5 (8 GB) with an **NVMe SSD HAT**
(the M.2 board that sits under/over the Pi and connects the SSD to the Pi 5's
PCIe lane). The SSD gives you fast, durable storage that — unlike the SD card —
won't wear out under a constant append workload.

> The Pi 5 exposes a single PCIe 2.0 lane on the FPC connector that these HATs
> use (Pimoroni NVMe Base, Pineberry HatDrive!, the official M.2 HAT+, 52Pi,
> etc.). Steps below are HAT-agnostic; consult your HAT's guide for the physical
> install and jumper/FPC orientation.

## 0. Prerequisites

- Raspberry Pi OS (64-bit, Bookworm or newer) on the SD card, updated:
  ```bash
  sudo apt update && sudo apt full-upgrade -y && sudo reboot
  ```
- The SSD HAT physically installed with an NVMe M.2 2230/2242/2280 SSD seated.
- `python3` (preinstalled on Raspberry Pi OS — no pip packages are needed).

## 1. Enable PCIe / NVMe

The Pi 5's PCIe connector is not enabled for third-party devices by default.
Add to `/boot/firmware/config.txt`:

```ini
# Enable the external PCIe connector for the NVMe HAT
dtparam=pciex1
# Optional: force PCIe Gen 3 (faster; most HAT+SSD combos are stable, test yours)
# dtparam=pciex1_gen=3
```

Reboot, then confirm the SSD appears:

```bash
sudo reboot
# after reboot:
lsblk            # expect an nvme0n1 device
sudo nvme list   # or this, if nvme-cli is installed
```

## 2. Partition, format, and mount the SSD

> This erases the SSD. Skip formatting if it already holds a filesystem you want.

```bash
sudo parted /dev/nvme0n1 --script mklabel gpt
sudo parted /dev/nvme0n1 --script mkpart primary ext4 0% 100%
sudo mkfs.ext4 -L forestguard /dev/nvme0n1p1

sudo mkdir -p /mnt/ssd
# Mount by label so it survives device renaming:
echo 'LABEL=forestguard  /mnt/ssd  ext4  defaults,noatime  0  2' | sudo tee -a /etc/fstab
sudo mount -a
df -h /mnt/ssd    # confirm it's mounted
```

`noatime` avoids a metadata write on every read — good for SSD longevity.

*(Optional: you can instead boot the whole Pi from the NVMe SSD. That's great for
performance but out of scope here; ForestGuard only needs the SSD mounted at
`/mnt/ssd` for its database.)*

## 3. Install ForestGuard

Clone the repo (or copy the `forestguard/` directory) onto the Pi, then:

```bash
cd forestguard
sudo ./deploy/install.sh
```

The installer:
- creates a locked-down `forestguard` service user,
- copies the app to `/opt/forestguard`,
- creates the data dir `/mnt/ssd/forestguard`,
- installs `/etc/forestguard.env` from the template,
- installs + enables the hardened systemd unit, and starts it.

## 4. Configure secrets

```bash
sudo nano /etc/forestguard.env
```

At minimum set:
```ini
FG_INGEST_KEYS=<random-key-your-nodes-will-send>
FG_ADMIN_TOKEN=<random-token-for-the-dashboard>
FG_DB_PATH=/mnt/ssd/forestguard/forestguard.db
```
Generate strong values with `openssl rand -hex 24`. Then:
```bash
sudo systemctl restart forestguard
journalctl -u forestguard -f      # watch it come up
```

Visit `http://<pi-ip>:8080/`, click 🔑, paste the admin token.

## 5. Point your sensor nodes at the Pi

Each node POSTs to `http://<pi-ip>:8080/api/v1/telemetry` with header
`X-API-Key: <FG_INGEST_KEYS value>`. See [`API.md`](API.md) for the payload.
No hardware yet? Prove the pipeline with the simulator from any machine:

```bash
python3 scripts/simulate.py --url http://<pi-ip>:8080 --key <key> --nodes 5 --fire ridge-2
```

## 6. TLS for remote access (do this before exposing it)

ForestGuard serves plain HTTP. For anything beyond a trusted LAN, terminate TLS
in front of it — either a VPN (WireGuard/Tailscale) or nginx:

```bash
sudo apt install -y nginx
sudo cp deploy/nginx.forestguard.conf /etc/nginx/sites-available/forestguard
sudo ln -s /etc/nginx/sites-available/forestguard /etc/nginx/sites-enabled/
# edit server_name + certs, then:
sudo nginx -t && sudo systemctl reload nginx
```
Use certbot for a real certificate, or a self-signed cert / your VPN's CA.

## 7. Turn on cloud (AWS) redundancy — optional

To make the Pi mirror to AWS while remaining the local source of truth, set in
`/etc/forestguard.env`:

```ini
FG_MODE=redundancy
FG_CLOUD_URL=https://<api-id>.execute-api.<region>.amazonaws.com/prod/ingest
FG_CLOUD_KEY=<cloud api key>
```
Restart, then watch the queue drain:
```bash
sudo systemctl restart forestguard
curl -s -H "Authorization: Bearer $FG_ADMIN_TOKEN" http://localhost:8080/api/v1/cloud/status
```
The cloud endpoint just needs to accept the batch described in
[`ARCHITECTURE.md` §6](ARCHITECTURE.md). If AWS is unreachable, readings queue on
the SSD and sync automatically when it returns — nothing is lost.

## 8. Operations

```bash
# logs
journalctl -u forestguard -f
# status / restart
sudo systemctl status forestguard
sudo systemctl restart forestguard
# update the code (re-run the installer; it copies over /opt/forestguard)
cd forestguard && sudo ./deploy/install.sh
```

**Backups.** Back up the SQLite DB safely with the online backup API:
```bash
sudo sqlite3 /mnt/ssd/forestguard/forestguard.db \
  ".backup '/mnt/ssd/forestguard/backup-$(date +%F).db'"
```
Copy the backup off-box (rsync/scp) on a schedule. Retention pruning
(`FG_RETENTION_DAYS`) keeps the live DB bounded.

**Resilience checklist.**
- SSD mounted at `/mnt/ssd` and DB path points there (not the SD card). ✔
- `FG_INGEST_KEYS` and `FG_ADMIN_TOKEN` set to real secrets. ✔
- TLS/VPN in front if reachable beyond the LAN. ✔
- Off-box backups scheduled. ✔
- (Redundancy) cloud queue draining — check `/api/v1/cloud/status`. ✔

## Troubleshooting

| Symptom | Check |
|---|---|
| SSD not detected | `dtparam=pciex1` in config.txt; reseat FPC cable; `lsblk`. |
| Service won't start | `journalctl -u forestguard -e`; is `/mnt/ssd` mounted (unit waits on it)? |
| Dashboard rejects everything | `FG_ADMIN_TOKEN` unset → all reads 401. Set it, or `FG_DASHBOARD_PUBLIC=true` on a LAN. |
| Nodes get 401 on ingest | `X-API-Key` must match a value in `FG_INGEST_KEYS`. |
| Nodes get 422 | Payload/clock issue — see the error message; check the node's RTC. |
