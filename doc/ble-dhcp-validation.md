# BLE-only develop firmware: C6 network regression

Physical target: ESP32-C6-DevKitM-1, COM23, IDF6.1, develop UART at
1000000 baud. COM24 and historical release6 artifacts were not flashed.
The initial signed develop8 application is 1642496 bytes; each existing OTA
slot is 1703936 bytes (61440 bytes spare). The build warns about the small
remaining slot margin. The partition table and persistent record formats did
not change. A fresh private 4 MiB flash backup preceded flashing.

## Implementation

The product now uses BLE/BTP commissioning and a BLE-only QR rendezvous bit.
It runs as a Wi-Fi station, with no SoftAP and no commissionable on-network
DNS-SD record. UDP CASE and operational DNS-SD remain enabled. The new shared
transport and admission rules belong to `matter-c-public`, documented in
`doc/oss/ble-transport.md`; this repository owns application policy and HTTP.

Generated initial factory NVS now includes identity as well as bootstrap, so a
blank device can attest over BLE before receiving Wi-Fi credentials. Existing
identity, keys, PIN and discriminator were preserved on COM23; only its QR
rendezvous data changed. An independent Matter.js decoder checked the QR and
manual code. No private identity, credentials, device backup or pairing payload
belongs in this report.

HTTP listens only after station association and IPv6 readiness, independent of
DHCPv4. It stops after disconnect and active-handler cleanup. Bearer checking
and local station destination checking remain mandatory. Status GET is available
online; writes also require the maintenance window. The provisioning client
requires an explicit local station URL and bypasses workstation HTTP proxies.

## Executed host checks

* Windows: full native CTest 123/123; portable product tests 2/2.
* Linux: normal and separate ASan/UBSan/LSan CTest 120/120 each.
* Memcheck: BTP, dispatcher, Root, runtime, allocator-fault runtime, reception,
  light groups and SDK Wi-Fi retry tests passed.
* Two runtimes in one process: full stock-chip-tool controller lifecycle passed
  under Memcheck, then separately under sanitizers; both instances were deleted.
* Added QueryImage metadata-shape regressions passed on Windows and Linux,
  under Memcheck and separately sanitizers. A present anonymous empty TLV list
  is valid; zero bytes and an empty structure are rejected. Production OTA
  validation was not relaxed.

The source archive for these production changes had SHA256
`d998c1d8fd2c44f224b0cd27442e84201eceae1999c618de8e0466dedb6a6a85`.
The metadata-shape test and documentation were added after that snapshot.
An initial Windows UDP test timed out in the restricted sandbox; the same test
and the final full suite passed outside that restriction.

## Executed physical checks

| Case | Observed result |
|---|---|
| Initial commissioning | Unmodified `chip-tool pairing ble-wifi`, complete DAC attestation, operational CASE and CommissioningComplete passed; no attestation bypass |
| Additional fabric | Enhanced commissioning window, real BLE/BTP and a second controller/fabric passed with full attestation |
| Bluetooth lifetime | UART recorded host initialization, confirmed TX, disconnect and host/controller deinitialization after commissioning; a later window initialized it again |
| Start without DHCPv4 | Target-MAC DHCP drop rule plus one EN restart; IPv4 remained `0.0.0.0`, with IPv6, control, reads and subscription reports working |
| DHCP recovery | Removing that rule restored IPv4 automatically, without a device restart |
| Lease expiry | Packet capture confirmed a 120-second ACK lease; failed renew/rebind led to IPv4 `0.0.0.0`; toggle/read and subscription reports still worked |
| Recovery after expiry | IPv4 returned automatically after DHCP was restored; fresh control/read/report checks passed |
| Short Wi-Fi interruption | Target-only two-second AP ban; control, read, subscription reporting, complete physical DNS-SD and HTTP recovered |
| Longer interruption | Target-only twenty-second AP ban; HTTP stopped; station association, IPv4, fresh CASE/control, physical DNS-SD and HTTP recovered |
| Automatic resubscription | Stock chip-tool eventually resubscribed after the long interruption; it temporarily selected an unreachable physical DUT address on the VM interface before returning to the lab relay |
| HTTP authorization | Missing/wrong bearer returned403, correct bearer returned200; offline request timed out while the listener was stopped |
| Commissioning policy on develop9 | Correct enhanced-window PIN over UDP PASE timed out while BLE advertising was active; no PBKDF response or new fabric |
| Host reuse on develop9 | Two enhanced-window open/revoke cycles initialized and deinitialized the same NimBLE host successfully; Revoke used required Timed Invoke |

Physical DNS-SD checks received PTR/SRV/TXT/AAAA directly from the C6 Wi-Fi
link-local source. VM chip-tool traffic used a byte-preserving SSH/UDP bridge
with a DNS-SD alias. This is not a native VM-to-WLAN routing test. Automatic
resubscription timing includes that bridge's address-selection artifact and
must not be generalized to a phone on the local network.

The temporary dnsmasq host entry requested one minute; this router clamps it to
120 seconds, confirmed in the actual ACK. An initial harness timeout was too
short for the next 30-second UART sample; a longer, separate expiry test passed.
AP UCI and other clients' leases were preserved. Only the target MAC was blocked.
The temporary host fragment and drop rule were removed after the DHCP test.

One early helper for the reverse OTA bridge mistakenly repeated its own mDNS
announcement upon reception. It was stopped, its timer was corrected, and
discovery/reconnect were repeated without that helper. These harness failures
are not counted as product passes or hidden as successful first attempts.

## OTA interoperability finding

The unmodified CHIP1.6.1 provider intentionally sends a zero-length
`MetadataForRequestor` for even requestor node IDs. Core1.6.1 section11.20
requires a present field to contain an anonymous TLV list with fully qualified
vendor tags; zero bytes are not that list. COM23 with node80 correctly rejected
that response and stayed on develop8. The same stock provider omits this optional
field for odd node IDs, so the installation test uses the existing second
fabric/node81. The provider's basic commissioning-window command is unsupported;
its enhanced window and full attestation were used for that second fabric.

An initial query also reached its bounded 60-second outgoing timeout after many
short-lived controller sessions. The dispatcher does not evict an existing
idle CASE slot merely to start an optional outgoing query. A controlled EN
restart retained both fabrics, and subsequent CASE/QueryImage reached the
provider. This session-capacity limitation remains distinct from the metadata
interoperability finding; limits were not increased.

The completed installation used the unchanged official provider on the second
fabric: signed develop8 to develop9, BDX transfer, ApplyUpdateRequest, native
handoff to the inactive ESP partition, restart, application recovery/confirmation
and successful NotifyUpdateApplied. The provider independently logged Apply
and Notify for version9. Chip-tool read SoftwareVersion9 afterwards and HTTP
reported both fabrics retained. The signed application is1642496 bytes; its
Matter OTA envelope is1642573 bytes, SHA256
`510b4d50fd155567a9fc7ebd151494ad2a7da68ba519f5ec303f894c879a3004`.
The final transfer took approximately331 seconds over the lab bridge, followed
by restart/Notify in about16 seconds. No release image was used.

The existing subscription automatically re-established after the OTA restart.
Nine unchanged upstream YAML cases passed: OO2.1/2.2, LVL2.1/2.2,
CC3.1/4.1/5.1/6.1/8.1. The first cases ran concurrently with the download; the
parent runner was paused before the OTA reboot to avoid starting another case
across that expected restart. CC4.1 completed before reboot; its parent-recorded
elapsed time includes the pause. Remaining cases ran on develop9. Original
upstream test files and the existing feature PICS were retained. The tests are
engineering regressions, not CSA certification.

## Measurement scope

After the first BLE commissioning, total free 8-bit heap returned to about
184 kB; its observed minimum was123660 bytes and largest free block131072 bytes.
During second-fabric BLE commissioning the observed minimum was115780 bytes;
afterwards free heap was about173 kB. These are bounded samples including SDK
Wi-Fi/Bluetooth/HTTP and task allocations, not a proof of the worst case at all
configured limits. Main-task unused stack samples were about9 kB of12 kB.

Manual Apple/Google Home, physical multicast group control, new SDK-wide OOM
injection and a full certification campaign were not performed. Release was
neither rebuilt nor tested for this task.

## Final develop12 and blank-device commissioning

After removing the test fabrics, the new application HTTP gate was found to
call `esp_wifi_sta_get_ap_info` every tick before checking IPv6 readiness, causing
SDK warning spam on an unconfigured station. Develop10 reverses these same two
boolean checks. Association and IPv6 remain required, while the SDK AP query is
skipped before IPv6 exists. Offline warning spam disappeared; one initial SDK
warning remained. The shared core is unchanged from the tested develop8/9
snapshot. The final pin adds credential-free SDK Wi-Fi operation flags:
`4c4fb5c74931f00d9d5ae6c522458c8ab14bb2e4`.

The subsequent genuinely unconfigured commissioning attempt exposed an
application default: an absent saved network record left InterfaceEnabled false,
so ConnectNetwork returned BUSY before trying Wi-Fi. Earlier BLE cases used an
already provisioned station. Develop11 sets the initial interface enabled before
loading saved settings. A saved record, including an explicit disable, still
overrides this default. Flashing over an older persisted disabled Matter image
does not change that image; the blank-device test therefore resets using the
corrected defaults before commissioning. These failed attempts are retained.

Develop12 adds only non-secret Wi-Fi initialization/configuration/start flags
to establish the port state. The final test logged enabled=1, no busy job/restore,
and accepted ConnectNetwork. Real BLE commissioning with full attestation
completed with DHCPv4 blocked for the target MAC. Chip-tool read version12,
toggled the bulb and read OnOff while IPv4 remained `0.0.0.0`; IPv6 was ready.
Removing the rule restored IPv4 automatically. Three unchanged YAML cases
(OO2.1, LVL2.1, CC7.1) also passed on develop12. The earlier complete nine-case
lighting/network/OTA campaign remains explicitly attributed to develop8/9.
OTA installation was not repeated on develop12.
An interactive chip-tool subscription on develop12 produced26 reports and a
changed OnOff value after Toggle, with subsequent readback agreeing.

Develop12 compiled and was flashed with hash verification. Builds report IDF
component-private-include validation warnings and the existing4% slot margin
warning; no release was built or tested.

The SDK compiler stack-usage files report240 bytes for `matter_idf_ble_process`,
48 bytes for its GATT access callback and64 bytes for the network poll. These
are individual frames, not total call-chain or interrupt-stack measurements.

The two test fabrics were removed with successful NOCResponse and controller
ACK before the automatic reset. Factory identity and the BLE QR were preserved.
The final flashed develop12 starts uncommissioned and offline. The DHCP fragment,
target-only drop rule and AP bans were removed; the AP lease setting remains1h.
Own UART/controller/provider/bridge processes were stopped, and the temporary
Windows Wi-Fi profile was deleted after disconnecting it. IPv6 remains enabled
and the user's Bluetooth adapter remains attached to the VM. COM24 was untouched.
See the [machine-readable receipt](ble-dhcp-validation.json) for artifact hashes
and individual YAML source hashes/check counts.
