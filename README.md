# Matter C Wi-Fi RGB bulb

ESP32-C6-DevKitM-1 / GPIO8 WS2812. Separate device application using the
standalone Matter C repository as a pinned submodule. No upstream source copy
or private credentials belong in this repository.

The submodule pins the main Matter repository after merging `work/bulb-integration`
into `main`, including Memory40-43, shared lighting/ESP ports and the preserved
CSA74/75 work. Protocol and reusable port repairs belong in that repository; this
repository owns LED output, application persistence, factory API, partitions and
firmware packaging. CSA75 compatibility exceptions remain OFF by default; the
initial PASE fail-safe repair applies to both profiles. See the
[merge regression report](doc/main-integration.md) for the exact tested source.

This is an experimental test-identity bulb, not a certified product. Physical
acceptance and validation receipts are recorded separately; no Apple/Google Home
manual result is inferred from controller tests.

The [physical validation report](doc/validation.md) and
[machine-readable receipt](doc/validation.json) describe the earlier release6
image. They do not cover the subsequent BLE-only develop firmware. Release5
supplies the nine original lighting YAML results, with the final regulatory
default correction checked separately on release6.
The later develop checks are recorded in the
[BLE/DHCP regression report](doc/ble-dhcp-validation.md).

## Device and storage

Endpoint1 is Extended Color Light `0x010d`, device revision5, for Matter1.6.1.
Its server clusters are Descriptor, Identify, Groups, Lighting OnOff,
OnOff/Lighting Level Control, Scenes Management and HS/XY/CT Color Control.
The root includes the shared library's Root Node and OTA Requestor composition,
plus Groupcast Listener/PerGroup. Color Loop, Enhanced Hue, scene/group names,
Groupcast Sender and CacheAndSync are not selected. See the pinned library's
[profile contract](matter/doc/oss/bulb-integration.md) and [engineering PICS](config/bulb.pics).

The WS2812 uses GPIO8. BOOT is GPIO9. The two variants use identical partitions:

| Partition | Offset | Size | Purpose |
|---|---:|---:|---|
| nvs_keys | `0xd000` | 4KiB | Reserved NVS key area |
| otadata | `0xe000` | 8KiB | SDK OTA selection/rollback |
| nvs | `0x10000` | 256KiB | Application light/groups, Wi-Fi bootstrap and reset count |
| matter | `0x50000` | 256KiB | Atomic Matter image, events, fail-safe and OTA journal |
| factory_data | `0x90000` | 128KiB | Per-unit bootstrap/identity and durable reset intent |
| phy_init | `0xb0000` | 4KiB | SDK PHY initialization |
| ota_0 | `0xc0000` | 1664KiB | Signed application slot |
| ota_1 | `0x260000` | 1664KiB | Signed application slot |

The partition table is at `0xc000`, on a 4MiB ESP32-C6. Light state and scene
metadata publish together in one versioned CRC-protected application record.
A factory-reset marker in `factory_data` makes interrupted erasure of the other
two partitions resumable. Factory identity and onboarding codes survive reset.
Corrupt storage is retained for diagnosis; boot never silently erases it.

## Build

Use ESP-IDF6.1 and its exported Python environment, a native Windows toolchain,
and the pinned `espressif/mdns`1.14.0 component. Initialize the Matter submodule:

```powershell
git submodule update --init
python -m pip install cryptography Pillow
New-Item -ItemType Directory -Force local | Out-Null
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:3072 -out local/ota-signing-key.pem
python tools/build.py develop --version 1 --idf-driver "$env:IDF_PATH/tools/idf.py"
python tools/build.py release --version 2 --idf-driver "$env:IDF_PATH/tools/idf.py"
```

Create `local/` before generating the signing key. Generate that key only once
and preserve it for all later signed updates. `build.py` refuses to invent it.
`--jobs` defaults to4. `--mdns <component-directory>` supports an already pinned
offline component. The workspace-specific default IDF driver may be overridden
as above. Each variant has its own generated SDK configuration/build directory.
After changing defaults, use `--fresh-config` to regenerate that variant's SDK
configuration; the previous generated file is saved in `build/`.

Develop enables UART0 at1000000baud, TX16/RX17, heap/stack counters and limited
transport error traces. Release disables application/SDK/bootloader logging,
console input and panic output. Release defaults apply when the **release
bootloader** is flashed too: OTA updates only the application. ESP ROM startup
output is separate; no irreversible eFuse operation is performed by this project.

Signed-on-update validation and SDK rollback are enabled without burning hardware
secure-boot eFuses. This does not provide hardware protection against physical
flash replacement. NVS encryption is not enabled by merely reserving `nvs_keys`.
Without a trusted RTC/time source, certificate validation explicitly uses Last
Known Good UTC (LKG), initially the build timestamp. The atomic `app/cert_lkg`
NVS value advances only from committed own operational certificates after
commissioning, never from pending fail-safe material or a peer's certificate.
Boot recovery reconstructs a missing newer floor from committed fabrics. The
LKG policy permits later NotBefore dates while still checking expiry, overlapping
validity periods, signatures and trust. It does not provide synchronized UTC;
adding trusted time requires selecting strict UTC and rechecking credentials.
See the [Google Home investigation](doc/google-home-validation.md).

The Wi-Fi station uses ESP-IDF's802.11d AUTO policy, including passive discovery
on channels12–14 and adoption of AP country information. The configured country
follows the published Matter Location; unknown countries use the world-safe
default. Applying a country is cached only after SDK success and retried on
failure. The application reads published/restored state outside IM callbacks;
it creates no independent country journal. Develop logs include association
channel/auth mode and disconnect reason, without SSIDs or credentials in the
application traces. SDK development logs remain private test artifacts.

## Per-unit factory provisioning and QR

`local/` contains secrets and device backups. `build/`, `output/`, generated SDK
files and managed components are ignored. Never commit a device manifest, DAC
private key, Wi-Fi password or signing key.

Build the portable QR helper and application storage tests:

```powershell
cmake -S . -B build/host -G Ninja -DBULB_HOST_TESTS=ON
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

For production identity input, prepare a private directory with `dac.der`,
`pai.der`, `paa.der`, `cd.der` and the raw32-byte `dac-private.bin`. Use the actual
assigned VID/PID. For the prototype, `--test-identity --test-cd <official development
CD>` creates an individual TEST FFF1/8000 PKI instead. No test key represents CSA
certification. Example with supplied identity:

```powershell
python tools/factory.py --output local/unit --mac AA:BB:CC:DD:EE:00 --serial BULB-0001 --vendor 0x1234 --product 1 --identity local/pki --qr-tool build/host/qr_grid.exe
python "$env:IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py" generate local/unit/bootstrap-nvs.csv local/unit/bootstrap-nvs.bin 0x20000
```

The output includes a per-unit HTTP bearer token, `factory.bin`, a standard
BLE-only Matter QR payload/manual code and `onboarding.png`. Legacy AP fields
remain in the bootstrap record for format compatibility; no SoftAP is started.
The bootstrap record is MAC-bound. The generated bootstrap NVS CSV includes
the identity record, so BLE commissioning can attest before Wi-Fi is configured.
On a new/blank device, flash the selected complete IDF image and the generated
bootstrap NVS image to`0x90000`.
Use OTA for existing installations; full initial `flash_args` also rewrites OTA
selection. Back up an existing board before changing its partition layout.

Scan the QR with a Matter controller supporting BLE commissioning. It supplies
the Wi-Fi credentials over the authenticated commissioning channel. Once the
bulb is connected to that network, the optional factory/maintenance API accepts
its explicit station address:

```powershell
python tools/provision.py status --device local/unit --url http://192.168.1.123
python tools/provision.py wifi --device local/unit --url http://192.168.1.123 --ssid YOUR_AP --password-file local/wifi-password.txt
```

The API validates identity shape, DAC chain and private/public-key match before
publishing it. Identity replacement is rejected while commissioned or fail-safe
is armed. Wi-Fi replacement stops/drains Matter, atomically changes network
settings, retains fabrics and restarts. Wi-Fi connection uses a shared SDK
adapter with bounded jittered reconnect, IPv6 readiness and fail-safe gating.
The radio remains in world-safe channel1..11 mode; supported country codes from
the SDK, including PL/US and unspecified XX, do not widen that policy.

Commissioning uses BLE/BTP only. New UDP PASE requests and commissionable
DNS-SD advertising are disabled; operational discovery and CASE use IPv6.
With no saved network record, the interface starts enabled and without
credentials, ready for initial BLE provisioning. An explicitly saved disable
still takes precedence.
The NimBLE host/controller are initialized for an open commissioning window
and released after its link has drained and closed. Existing protocol limits
are shared between transports. A new unit needs the preloaded identity above;
the HTTP API is unavailable until the station has connected.

## Factory REST API and reset

The HTTP listener runs only while the station is associated and has IPv6 ready;
DHCPv4 is not a prerequisite. Disconnection closes admission immediately and
stops the listener after active handlers finish. Requests must use a current
local station address and the per-unit `Authorization: Bearer <64hex>` credential.
GET status remains available online; changes also require the bounded
maintenance window. Requests never travel through workstation proxy settings
in `provision.py`. There is no unauthenticated credential readback.

| Method/path | Body | Result |
|---|---|---|
| GET `/api/v1/status` | none | Identity/fabric/Wi-Fi status and total8-bit heap counters |
| POST `/api/v1/factory/identity` | `factory.bin` | Validate/publish uncommissioned identity, restart |
| POST `/api/v1/wifi` | Versioned `MWIFI001` binary+CRC | Stop/drain, retain fabrics, store network, restart |
| POST `/api/v1/reset` | empty | Stop/drain, durable factory reset intent, restart |
| POST `/api/v1/ota/query` | empty | Request normal Matter OTA discovery/query |

Bodies are bounded to4096bytes. Malformed bodies fail400, missing authorization
fails403, state conflicts409, oversize413 and allocation/queue pressure503.
An already accepted slow request may return202; its owner retains the job until
completion. Identity bodies and job storage are wiped before release.

The factory window opens for5minutes while uncommissioned. Holding BOOT3seconds
opens a maintenance window; holding it10seconds requests reset. Five power-on
boots, each interrupted within5seconds, also request reset. Software/OTA reset
does not increment that counter. Five seconds of stable operation clears it.
Reset clears user Wi-Fi, fabrics and light/scenes while preserving factory
identity, HTTP authorization and QR. Commission over BLE again after reset.

## OTA and tests

Use a strictly increasing numeric `--version` and the same signing key. Package
the already signed ESP application for a standard Matter OTA provider:

```powershell
python tools/package_ota.py --input build/release/matter_c_bulb.bin --output output/bulb-v2.ota --version 2 --vendor 0x1234 --product 1
```

The port stages the inactive partition, checks signed image/identity/version,
uses the SDK trial/rollback protocol and confirms after application recovery.
VersionApplied precedes the durable NotifyUpdateApplied claim; DNS/CASE setup
can retry before that claim. Ciphertext remains independently owned through
ACK/drain. Only a completed real installation/reboot counts as an OTA test.

Portable tests cover codecs, corruption, bounded allocation faults and atomic
light/scene storage. `tests/light_acceptance.mjs`, `ota_acceptance.mjs`,
`network_acceptance.mjs` and `admin.mjs` use an independent pinned Matter.js0.17.9
peer; its explicit Initiator-role diagnostic override is included in results.
`tests/quick_yaml.py` runs unchanged upstream OO/LVL/CC cases through unmodified
chip-tool, records source hashes, checks and skipped PICS steps, and does not
label those runs certification. `tools/lab_bridge.py` is a bounded test-only
SSH/UDP/DNS-SD alias for VM access, not production discovery infrastructure.
