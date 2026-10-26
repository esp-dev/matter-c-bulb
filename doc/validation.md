# C6 bulb validation, 2026-10-25

This is an engineering prototype with individual **TEST FFF1/8000** identity,
not a certification result. The numeric receipt, source pins, test-file hashes
and delivered artifact SHA256 values are in [validation.json](validation.json).
No device key, PIN, bearer token or network password is included there.

## Implemented scope

The product repository owns GPIO8 WS2812 output, joint atomic light/scene NVS,
factory identity/network REST provisioning, physical reset, partition layout,
signed firmware packaging and develop/release configuration. Matter clusters,
group receive, shared Wi-Fi/multicast ports and protocol repairs are committed
in the **main Matter repository's `work/bulb-integration` branch**, pinned by the
submodule. The separate main checkout's ongoing CSA75 work was preserved.

The shared implementation adds Extended Color Light revision5, Groups, Scenes
Management and root Groupcast Listener/PerGroup. It also fixes premature OTA
notification claims, unsupported-country Location persistence, transient
OFFLINE/IPv6-DAD MRP handling and the initial regulatory default. Limits and
storage formats of the existing stack are retained.

## Executed checks

| Check | Actual result |
|---|---|
| Product CMake/CTest | 2/2, codec and atomic light/scene storage, including both publication OOM positions |
| Bridge metadata parser | 2/2, target isolation and malformed DNS |
| QR/manual payload | Independent Matter.js decoder passes; generated PNG visually inspected |
| Main library Linux | 117 native definitions; 119/119 total CTest normally and separately ASan/UBSan/LSan |
| Memcheck | 14 focused fixtures and full existing two-runtime lifecycle pass |
| Separate sanitizers | Full existing two-runtime lifecycle passes |
| Faulted wire | Six cases on earlier profile snapshots; final after-case on MRP correction passes Memcheck and separate sanitizers |
| C6 lighting | Commissioning, mandatory server inventory, OnOff, Level, HS/XY/CT transitions, Groups/Scenes/global scene, Identify |
| C6 subscription | Three paths, 18 reports, zero unexpected closures in the lighting run |
| Factory REST | 7 rejection/state checks while commissioned; 8 after reset and 8 on release5 |
| Wi-Fi loss | Targeted 2s and20s bans; short-loss reports resume, long-loss fresh CASE/control/resubscribe pass; two fabrics retained |
| External Wi-Fi provisioning | Stop/drain, durable write, restart and both fabrics retained |
| Five rapid resets | Five EN pulses at3s intervals reset user state; factory identity and original onboarding still commission successfully |
| Real OTA | Develop4→release5 and release5→release6 install, reboot, retain fabric, emit VersionApplied/Notify and allow control |
| Regulatory default | Final6 reads RegulatoryConfig=LocationCapability=2 after real OTA/reboot |
| Stock chip-tool | Full physical release5 commissioning with attestation, CASE, Read/Invoke and own-fabric removal |
| Original YAML | Nine cases, **449 checks**,237 steps executed,34 PICS skips,345.24s total |
| Release silence | Final6: no application/SDK/bootloader logs at115200 or1000000baud; network control separately verified |

The original short YAML cases are TC-OO-2.1/2.2, TC-LVL-2.1/2.2 and
TC-CC-3.1/4.1/5.1/6.1/8.1. Files were checked against SDK Git HEAD, without
modifying expected results. Unsupported features are explicitly represented by
PICS; skipped steps are not counted as executed checks. The runner emitted its
existing XML annotation-parser notices; this is not a full data-model conformance
campaign. Those nine lighting cases ran on release5; final6 changes only the
initial general-commissioning regulatory default and adds fresh native/two-node,
real OTA/post-boot attribute/control and UART results.

Stock chip-tool ran on the VM through a byte-preserving SSH/UDP bridge with a
local operational DNS-SD alias. This does not prove native VM-to-WLAN discovery
or routing. Windows Matter.js tests explicitly record their pinned0.17.9
Initiator-role diagnostic override. Their DCL/PAA/CD trust checks are skipped by
that peer; the separate stock chip-tool commissioning performs attestation with
the unit's public test PAA and official development CD.

## Failures retained in the evidence

An initial short-loss test exposed premature termination of MRP on OFFLINE and
the ESP port treating an IPv6 source awaiting DAD as a permanent error. Both are
fixed in the main library and physically retested. Limits/retry count remain
unchanged; an outage can still legitimately exhaust MRP and require resubscribe.

One intermediate Linux pair run missed a subscription report; its isolated
rerun and final complete runs passed. An earlier full Windows batch had three
UDP receive timeouts; the same three passed serially outside the sandbox.
Original failed receipts were retained.

One unforced OTA6 transfer stopped at873472bytes/70% with a provider peer
timeout. The device did not reboot, retained version5/fabric, returned Idle and
emitted DownloadError. Retrying the unchanged image/code completed installation
and notification. The timeout's cause is **not isolated**; this remains an
intermittent lab observation rather than a confirmed new native defect.

## Memory and completion boundaries

SDK `MALLOC_CAP_8BIT` counters cover total dynamic system heap, including Wi-Fi,
AP, HTTP and task-stack allocations. On develop4, the two-fabric reconnect
workload observed a minimum178440B free; OTA4→5 observed180912B minimum free,
167936B largest block at the final sample and8824B minimum free of the12288B main
task stack. Largest-block readings are samples, not a continuous minimum. These
are bounded one/two-fabric workloads; they are not a proof for every simultaneous
Matter limit or a guarantee that an arbitrary MQTT/HTTP application fits.

COM23 is left with signed **release6**, factory identity/QR retained, zero test
fabrics and lab Wi-Fi/IPv6 ready. COM24 was not flashed or reconfigured. Created
Windows profiles/connections and temporary bridge processes are cleaned up;
IPv6 is left enabled. Firmware output contains no per-device factory secrets.

Manual Apple Home/Google Home acceptance, physical multicast group traffic,
full lighting certification, production identity and a trusted-time/LKG policy
remain separate gates. The application's current clock starts from build UTC.
Five EN resets were tested; actual removal of power was not. The inherited
native OTA port has separate wrong-signature/interruption/rollback evidence;
that entire negative campaign was not repeated for this product.

The ROM still emits230bytes at115200baud on reset. Application configuration
does not suppress immutable ROM output; **no eFuse was changed**. Release6's
bootloader is byte-identical to the physically tested silent release5 bootloader.

## Reproduction entry points

Use the README build/provisioning instructions and private local device/controller
directories. Executed entry points include:

```text
ctest --test-dir build/host --output-on-failure
python tests/test_lab_bridge.py
node tests/check_pairing.mjs <private-manifest> <pinned-node-modules>
node tests/light_acceptance.mjs ... commission Wi-Fi PL
node tests/network_acceptance.mjs ... <private-router-config>
python tests/factory_acceptance.py --device <private-unit> --output <private-result>
python tests/reset_acceptance.py --port COM23 --output <private-result>
node tests/ota_acceptance.mjs ... <signed-image.ota> 6 Wi-Fi
python tests/quick_yaml.py --sdk <upstream> --chip-tool <binary> --storage <private-state> --pics config/bulb.pics --output <private-result>
python tests/release_silence.py --port COM23 --output <private-result>
```

Library checks used CTest plus `valgrind --leak-check=full --error-exitcode=99`,
then a separate `MATTER_SANITIZE=ON` build with ASan/UBSan/LSan. Runs used isolated
VM directories and four compilation jobs. Repositories and commits are local;
no push or remote bulb repository creation was performed. Publish the Matter
commit before distributing a fresh clone that resolves the canonical submodule.
