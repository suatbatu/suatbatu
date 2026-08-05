"""Bing command-line interface.

A Fing-style toolbox in your terminal:

    bing scan                 discover devices on your network
    bing net                  show your network context
    bing ports <host>         scan a host's TCP ports
    bing ping <host>          measure latency
    bing trace <host>         trace the route to a host
    bing dns <name>           DNS lookups (A/AAAA/MX/TXT/...)
    bing wol <mac>            wake a device (Wake-on-LAN)
    bing speed                internet speed test
    bing vendor <mac>         MAC-address vendor lookup
    bing web                  launch the web dashboard
    bing update-oui           refresh the full MAC-vendor database
"""

from __future__ import annotations

import argparse
import json
import sys
from typing import List, Optional

from . import __version__, discovery, dnsr, netinfo, oui, ports, security, wol
from . import ping as ping_mod
from . import speedtest as speedtest_mod
from . import traceroute as traceroute_mod
from .util import color, human_bits_per_sec, human_duration, table


def _print_json(obj) -> None:
    print(json.dumps(obj, indent=2, default=lambda o: getattr(o, "to_dict", lambda: str(o))()))


def _banner(text: str) -> str:
    return color(f"◆ {text}", "bold", "cyan")


# --------------------------------------------------------------------------- #
# scan                                                                        #
# --------------------------------------------------------------------------- #

def cmd_scan(args: argparse.Namespace) -> int:
    if not args.json:
        target = args.cidr or "(local subnet)"
        print(_banner(f"Scanning {target} — this may take a moment…"))

    seen: List[str] = []

    def on_device(dev: discovery.Device) -> None:
        if args.json:
            return
        tag = ""
        if dev.is_gateway:
            tag = color(" [gateway]", "yellow")
        elif dev.is_self:
            tag = color(" [this device]", "green")
        name = dev.hostname or dev.vendor or dev.device_type
        print(f"  {color('●', 'green')} {dev.ip:<15} {name}{tag}")
        seen.append(dev.ip)

    try:
        devices = discovery.scan(
            cidr=args.cidr,
            timeout=args.timeout,
            with_ports=not args.no_ports,
            resolve_names=not args.no_resolve,
            on_device=on_device,
        )
    except (ValueError, RuntimeError) as exc:
        print(color(f"error: {exc}", "red"), file=sys.stderr)
        return 1

    if args.json:
        _print_json({"count": len(devices), "devices": [d.to_dict() for d in devices]})
        return 0

    rows = []
    for d in devices:
        flags = "🌐" if d.is_gateway else ("★" if d.is_self else "")
        rows.append([
            d.ip,
            d.mac or "—",
            (d.vendor or "—")[:22],
            (d.hostname or "—")[:26],
            d.device_type,
            ",".join(str(p) for p in d.open_ports[:6]) or "—",
            flags,
        ])
    print()
    print(table(rows, ["IP", "MAC", "Vendor", "Hostname", "Type", "Open ports", ""]))
    print(color(f"\n{len(devices)} device(s) found.", "bold"))
    return 0


# --------------------------------------------------------------------------- #
# net                                                                         #
# --------------------------------------------------------------------------- #

def cmd_net(args: argparse.Namespace) -> int:
    info = netinfo.gather(include_public=not args.no_public)
    if args.json:
        _print_json(info.to_dict())
        return 0
    print(_banner("Network information"))
    rows = [
        ["Hostname", info.hostname],
        ["Local IP", info.primary_ipv4 or "—"],
        ["Gateway", info.gateway or "—"],
        ["Netmask", info.netmask or "—"],
        ["Subnet", info.cidr or "—"],
        ["DNS servers", ", ".join(info.dns_servers) or "—"],
    ]
    if not args.no_public:
        rows += [
            ["Public IP", info.public_ip or "—"],
            ["ISP", info.isp or "—"],
            ["Location", info.location or "—"],
        ]
    print(table(rows, ["Field", "Value"]))
    if info.interfaces:
        iface_rows = [[i.name, i.ipv4 or "—", i.netmask or "—", i.mac or "—"]
                      for i in info.interfaces]
        print("\n" + _banner("Interfaces"))
        print(table(iface_rows, ["Name", "IPv4", "Netmask", "MAC"]))
    return 0


# --------------------------------------------------------------------------- #
# ports                                                                       #
# --------------------------------------------------------------------------- #

def cmd_ports(args: argparse.Namespace) -> int:
    port_list = ports.parse_port_spec(args.ports)
    if not args.json:
        print(_banner(f"Scanning {len(port_list)} ports on {args.host}…"))

    def on_open(res: ports.PortResult) -> None:
        if args.json:
            return
        svc = res.service or "?"
        banner = f"  {color(res.banner, 'grey')}" if res.banner else ""
        print(f"  {color('OPEN', 'green')} {res.port:>5}/tcp  {svc:<16}{banner}")

    results = ports.scan(args.host, ports=port_list, timeout=args.timeout,
                         banner=not args.no_banner, on_open=on_open)
    if args.json:
        report = security.assess(args.host, [r.port for r in results])
        _print_json({
            "host": args.host,
            "open": [r.to_dict() for r in results],
            "security": report.to_dict(),
        })
        return 0

    if not results:
        print(color("  No open ports found.", "yellow"))
    report = security.assess(args.host, [r.port for r in results])
    color_for = {"A": "green", "B": "green", "C": "yellow", "D": "yellow", "F": "red"}
    print(color(f"\nSecurity: {report.score}/100 (grade {report.grade})",
                "bold", color_for.get(report.grade, "white")))
    for f in report.findings:
        sev_color = {"high": "red", "medium": "yellow", "low": "grey"}[f.severity]
        print(f"  {color('!', sev_color)} {f.port}: {f.message}")
    return 0


# --------------------------------------------------------------------------- #
# ping                                                                        #
# --------------------------------------------------------------------------- #

def cmd_ping(args: argparse.Namespace) -> int:
    if not args.json:
        print(_banner(f"Pinging {args.host}…"))

    def on_reply(r: ping_mod.PingReply) -> None:
        if args.json:
            return
        if r.success:
            print(f"  seq={r.seq} time={color(f'{r.rtt_ms} ms', 'green')}")
        else:
            print(f"  seq={r.seq} {color(r.error or 'no reply', 'red')}")

    stats = ping_mod.ping(args.host, count=args.count, port=args.port,
                          force_tcp=args.tcp, on_reply=on_reply)
    if args.json:
        _print_json(stats.to_dict())
        return 0
    if stats.address is None:
        print(color(f"  could not resolve {args.host}", "red"))
        return 1
    s = stats.summary()
    print(color(f"\n{args.host} [{stats.address}] via {stats.method}", "bold"))
    print(f"  {stats.transmitted} sent, {stats.received} received, "
          f"{stats.loss_pct}% loss")
    if s["avg"] is not None:
        print(f"  rtt min/avg/max = {s['min']}/{s['avg']}/{s['max']} ms, "
              f"jitter {s['jitter']} ms")
    return 0 if stats.received else 1


# --------------------------------------------------------------------------- #
# trace                                                                       #
# --------------------------------------------------------------------------- #

def cmd_trace(args: argparse.Namespace) -> int:
    if not args.json:
        print(_banner(f"Tracing route to {args.host}…"))
    result = traceroute_mod.trace(args.host, max_hops=args.max_hops)
    if args.json:
        _print_json(result.to_dict())
        return 0
    if result.address is None:
        print(color(f"  {result.note}", "red"))
        return 1
    for hop in result.hops:
        addr = hop.address or "*"
        name = f" ({hop.hostname})" if hop.hostname else ""
        rtt = f"{hop.rtt_ms} ms" if hop.rtt_ms is not None else "*"
        marker = color(" ✔", "green") if hop.final else ""
        print(f"  {hop.ttl:>2}. {addr}{name}  {rtt}{marker}")
    if result.note:
        print(color(f"\n  note: {result.note}", "grey"))
    return 0


# --------------------------------------------------------------------------- #
# dns                                                                         #
# --------------------------------------------------------------------------- #

def cmd_dns(args: argparse.Namespace) -> int:
    if args.reverse:
        name = dnsr.reverse(args.name)
        if args.json:
            _print_json({"ip": args.name, "hostname": name})
        else:
            print(f"{args.name} -> {name or color('no PTR record', 'yellow')}")
        return 0 if name else 1

    types = args.type.split(",") if args.type else ["A"]
    all_records = []
    for rtype in types:
        try:
            recs = dnsr.query(args.name, rtype.strip().upper(), server=args.server)
            all_records.extend(recs)
        except dnsr.DNSError as exc:
            if not args.json:
                print(color(f"  {rtype}: {exc}", "yellow"))
    if args.json:
        _print_json({"name": args.name, "records": [r.to_dict() for r in all_records]})
        return 0
    if not all_records:
        print(color("  no records", "yellow"))
        return 1
    rows = [[r.rtype, str(r.ttl), r.value] for r in all_records]
    print(_banner(f"DNS records for {args.name}"))
    print(table(rows, ["Type", "TTL", "Value"]))
    return 0


# --------------------------------------------------------------------------- #
# wol                                                                         #
# --------------------------------------------------------------------------- #

def cmd_wol(args: argparse.Namespace) -> int:
    try:
        wol.wake(args.mac, broadcast=args.broadcast, port=args.port)
    except ValueError as exc:
        print(color(f"error: {exc}", "red"), file=sys.stderr)
        return 1
    msg = f"Magic packet sent to {oui.normalize(args.mac)} via {args.broadcast}:{args.port}"
    print(color(msg, "green") if not args.json else "")
    if args.json:
        _print_json({"mac": oui.normalize(args.mac), "broadcast": args.broadcast,
                     "sent": True})
    return 0


# --------------------------------------------------------------------------- #
# speed                                                                       #
# --------------------------------------------------------------------------- #

def cmd_speed(args: argparse.Namespace) -> int:
    if not args.json:
        print(_banner("Running internet speed test…"))
    result = speedtest_mod.run(quick=args.quick)
    if args.json:
        _print_json(result.to_dict())
        return 0
    if result.error:
        print(color(f"  {result.error}", "red"))
        return 1
    print(f"  Server:    {result.server or '—'}")
    print(f"  ISP:       {result.isp or '—'}  ({result.client_ip or '—'})")
    print(f"  Latency:   {color(str(result.latency_ms) + ' ms', 'cyan')} "
          f"(jitter {result.jitter_ms} ms)")
    down = human_bits_per_sec((result.download_mbps or 0) * 1e6)
    up = human_bits_per_sec((result.upload_mbps or 0) * 1e6)
    print(f"  Download:  {color(down, 'green', 'bold')}")
    print(f"  Upload:    {color(up, 'green', 'bold')}")
    return 0


# --------------------------------------------------------------------------- #
# vendor / update-oui                                                         #
# --------------------------------------------------------------------------- #

def cmd_vendor(args: argparse.Namespace) -> int:
    norm = oui.normalize(args.mac)
    if not norm:
        print(color(f"error: invalid MAC {args.mac!r}", "red"), file=sys.stderr)
        return 1
    vendor = oui.lookup(args.mac)
    if args.json:
        _print_json({"mac": norm, "vendor": vendor})
    else:
        print(f"{norm} -> {vendor or color('unknown', 'yellow')}")
    return 0 if vendor else 1


def cmd_update_oui(args: argparse.Namespace) -> int:
    print(_banner("Downloading full IEEE OUI database…"))
    try:
        n = oui.update()
    except Exception as exc:
        print(color(f"error: {exc}", "red"), file=sys.stderr)
        return 1
    print(color(f"Cached {n} vendor prefixes.", "green"))
    return 0


# --------------------------------------------------------------------------- #
# web                                                                         #
# --------------------------------------------------------------------------- #

def cmd_web(args: argparse.Namespace) -> int:
    from .server import serve
    serve(host=args.host, port=args.port, open_browser=args.open)
    return 0


# --------------------------------------------------------------------------- #
# argument parser                                                             #
# --------------------------------------------------------------------------- #

def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="bing",
        description="Bing — a Fing-like network scanner (pure-Python, no deps).",
    )
    p.add_argument("--version", action="version", version=f"bing {__version__}")
    sub = p.add_subparsers(dest="command", required=True)

    def add_json(sp):
        sp.add_argument("--json", action="store_true", help="output machine-readable JSON")

    sp = sub.add_parser("scan", help="discover devices on the local network")
    sp.add_argument("--cidr", help="subnet to scan, e.g. 192.168.1.0/24 (default: auto)")
    sp.add_argument("--timeout", type=float, default=0.4, help="per-probe timeout (s)")
    sp.add_argument("--no-ports", action="store_true", help="skip per-device port scan")
    sp.add_argument("--no-resolve", action="store_true", help="skip hostname resolution")
    add_json(sp)
    sp.set_defaults(func=cmd_scan)

    sp = sub.add_parser("net", help="show local & public network information")
    sp.add_argument("--no-public", action="store_true", help="skip public IP/ISP lookup")
    add_json(sp)
    sp.set_defaults(func=cmd_net)

    sp = sub.add_parser("ports", help="scan a host's TCP ports")
    sp.add_argument("host")
    sp.add_argument("--ports", default="common",
                    help="ports: 'common', 'top100', 'all', '1-1024', or '22,80,443'")
    sp.add_argument("--timeout", type=float, default=1.0)
    sp.add_argument("--no-banner", action="store_true", help="skip banner grabbing")
    add_json(sp)
    sp.set_defaults(func=cmd_ports)

    sp = sub.add_parser("ping", help="measure latency to a host")
    sp.add_argument("host")
    sp.add_argument("-c", "--count", type=int, default=4)
    sp.add_argument("--port", type=int, default=443, help="TCP port for TCP-ping")
    sp.add_argument("--tcp", action="store_true", help="force TCP ping (skip ICMP)")
    add_json(sp)
    sp.set_defaults(func=cmd_ping)

    sp = sub.add_parser("trace", help="trace the network route to a host")
    sp.add_argument("host")
    sp.add_argument("--max-hops", type=int, default=30)
    add_json(sp)
    sp.set_defaults(func=cmd_trace)

    sp = sub.add_parser("dns", help="DNS lookups")
    sp.add_argument("name")
    sp.add_argument("-t", "--type", default="A",
                    help="record type(s), comma-separated: A,AAAA,MX,TXT,NS,CNAME,SOA")
    sp.add_argument("--server", help="DNS server to query (default: system resolver)")
    sp.add_argument("-x", "--reverse", action="store_true", help="reverse (PTR) lookup")
    add_json(sp)
    sp.set_defaults(func=cmd_dns)

    sp = sub.add_parser("wol", help="wake a device via Wake-on-LAN")
    sp.add_argument("mac")
    sp.add_argument("--broadcast", default="255.255.255.255")
    sp.add_argument("--port", type=int, default=9)
    add_json(sp)
    sp.set_defaults(func=cmd_wol)

    sp = sub.add_parser("speed", help="internet speed test")
    sp.add_argument("--quick", action="store_true", help="faster, less accurate")
    add_json(sp)
    sp.set_defaults(func=cmd_speed)

    sp = sub.add_parser("vendor", help="look up the vendor of a MAC address")
    sp.add_argument("mac")
    add_json(sp)
    sp.set_defaults(func=cmd_vendor)

    sp = sub.add_parser("update-oui", help="download the full IEEE OUI vendor database")
    add_json(sp)
    sp.set_defaults(func=cmd_update_oui)

    sp = sub.add_parser("web", help="launch the web dashboard")
    sp.add_argument("--host", default="127.0.0.1")
    sp.add_argument("--port", type=int, default=8787)
    sp.add_argument("--open", action="store_true", help="open a browser window")
    sp.set_defaults(func=cmd_web)

    return p


def main(argv: Optional[List[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except KeyboardInterrupt:
        print("\naborted", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
