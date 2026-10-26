# Main Matter integration, 2026-10-25

The submodule now follows the combined `main` history of the Matter repository.
The previously uncommitted CSA74/75 work was preserved as `6ab4e1a`, then merged
with `work/bulb-integration` (`21859f9`) as `ad7a272`. No CSA change was dropped;
the only text conflict preserves both accepted-Revoke reply authorization and
the group-cleanup admission gate. CSA75 compatibility remains OFF for the bulb.

The exact executable source tested is `ad7a272d194c7c358e06bf1b0ecdab5a0632a775`.
The final pin is main `5377c7d0950a178275f58c3bf4b19148ea16e856`, which adds
only the regression reports/status to that executable source. The original
`work/bulb-integration` worktree was also fast-forwarded to this combined tip.
See the shared
[merge regression report](../matter/doc/oss/bulb-main-integration.md) and
[numeric receipt](../matter/doc/oss/bulb-main-integration.json) for the four
119/119 CTest configurations, separate memory-tool results and wire coverage.
All four faulted `after` wire runs pass: strict and CSA75, each under Memcheck
and separately sanitizers. Each completes 80 full Reads, 75 subscription
snapshots, three OOM positions and the missing-ACK recovery, then drains with
zero tracked core allocations/bytes.

Application CTest and bridge-parser checks pass 2/2 each. Develop and release
version7 compile with ESP-IDF6.1, without compiler warnings, against the pinned
submodule. Their signed application sizes are 1314816 and 1249280 bytes.
No application-specific code change was needed for the combined main API.

These version7 builds were **not flashed**. The earlier release6 artifacts and
[physical validation receipt](validation.json) are unchanged and refer to their
original source pin. No new C6, TH/CSA, Apple Home or Google Home result is
claimed by this integration. No push was performed.

Next: physical acceptance of the merged firmware, including startup without
DHCPv4, expired lease and DHCP recovery while IPv6 control/subscriptions remain
active. This work does not change the prototype's existing production gates.
