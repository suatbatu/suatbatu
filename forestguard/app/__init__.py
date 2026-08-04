"""ForestGuard server — a zero-dependency, self-hosted backend for a
wildfire / forest monitoring sensor network.

Designed to run on a Raspberry Pi 5 (8 GB) with an NVMe SSD HAT, either as a
full replacement for a cloud (AWS) backend or as a local redundancy / failover
tier that mirrors telemetry to the cloud.

The whole server uses only the Python standard library so it installs and runs
on any Raspberry Pi OS image with `python3` and nothing else — no pip, no ARM
wheels, no dependency drift in the field.
"""

__version__ = "0.1.0"
