# Google Home commissioning and certificate time

Date:2026-10-26. Target:ESP32-C6-DevKitM-1, COM23, develop UART1000000.
This is an interoperability investigation with a development identity, not CSA
certification. Release and COM24 are outside this test.

## Reproduced failure

The user repeatedly reported Google Home stopping at saving certificates,
before choosing Wi-Fi, with ‚ÄúSomething went wrong, Make sure you're connected
to your network‚Äù. Develop13 identified AddNOC (`0x3e/6`) failing operational
certificate validation, followed by successful fail-safe cleanup and zero
fabrics. Develop14 retained the original decision and separately probed validity
dates without publishing its output key. The original verification failed;
the diagnostic verification passed with only date checks disabled. The bulb
reported2026-10-25T22:56:43 while the attempt occurred on2026-10-26.
Thus the failure preceded network provisioning and was caused by using stale
build time as trusted current UTC.

The diagnostic bypass was removed from the fix. It never authorized a fabric.
Logs contain command identifiers/results, not certificates, keys or passwords.

## Repair

Core3.5.6/3.5.6.1 distinguish trusted UTC from LKG and permit a validity-only
fallback policy. The shared Matter library adds explicit LAST_KNOWN_GOOD time
mode, retaining strict SUPPLIED UTC by default. LKG validates at one common
instant:the maximum of the durable floor and all path NotBefore values. Every
NotAfter, malformed period, overlap, signature, profile and trust check remains.
Attestation's specified leaf-NotBefore policy is unchanged. Verification never
modifies or persists the candidate time.

The application has no trusted RTC/SNTP. Its certificate callback returns the
durable LKG floor. NVS `app/cert_lkg` starts at build time and never moves
backwards. Once recovery finishes and fail-safe is idle, the application extracts
NotBefore only from its verified, committed own fabric records. Pending roots,
AddNOC/UpdateNOC candidates and rollback cannot advance it. The entire candidate
is evaluated before an atomic NVS write; RAM publication/cache follows success.
A failed write is retried. Recovery rebuilds the floor from committed fabrics,
covering power loss between fabric publication and the separate LKG write.
An unchanged fabric revision avoids repeated parsing/writes.
The original LKG shared-library commit is `79cdbb5`; develop17 pins its
successor `a4f7d34` on `matter-c-public/main`, adding non-secret Wi-Fi traces.

## Validation

Develop15 deliberately retains develop12's build epoch1792968614, so a fresh
timestamp cannot mask the original error. It compiled with ESP-IDF6.1 and was
flashed only to the current application slot at0xc0000, with hash verification.
The1642496-byte signed image SHA256 is
`53bd112abbf9b96cc09386007da9b5d7ebd24606630d57cf7bf1d2025dd60493`.
Bootloader, partition table, OTA selection, factory identity/QR and device state
were preserved. A private backup of application/Matter NVS preceded diagnostics.

Windows focused CTest passed4/4:CASE, credentials, operational certificate and
live runtime time policy. Product host tests passed3/3:factory codec, atomic
light storage and LKG publication. Coverage includes a full CASE handshake at
an older LKG, future certificate acceptance, expiry rejection, changed signature,
clock failure, unchanged attestation/default policy, OOM cleanup, failed clock
store/retry, malformed extraction, nested tags, restart, rollback and removal.

Compiler stack frames on C6:NotBefore extraction128bytes, clock update48bytes,
NVS clock store32bytes, UTC callback64bytes, app_main656bytes. These are individual
frames, not complete task call-chain/interrupt bounds. No large automatic buffer
or new packet allocation was introduced.

Linux CTest passed120/120 normally and separately under ASan/UBSan/LSan. Full
two-runtime stock-chip-tool scenarios passed under Memcheck and, separately,
ASan/UBSan/LSan. Focused Memcheck runtime, live-time and credentials/OOM tests
passed. Product tests passed3/3 normally and with sanitizers; its LKG test also
passed Memcheck. Evidence is in the isolated VM run `ble1/google-home04`.

Executed commands include `cmake --build ... -j4`,
`ctest --test-dir ... -j2 -E '^runtime_wire_' --output-on-failure`, focused
`valgrind --leak-check=full --errors-for-leak-kinds=definite,indirect,possible
--error-exitcode=99 ...`, and the existing full pair harness. Sanitizer builds
and nested installed consumers use `-fno-pie/-no-pie` and leak detection.
The initial diagnostic iteration exposed a null test-only runtime logger and
a missing nested-consumer sanitizer link environment; both were corrected
before the complete passing run. The final code has no validity-bypass probe.

IDF reports existing component include/Kconfig notices and the4% OTA-slot margin
warning. Develop15 booted with zero fabrics/offline; at idle it reported
167404bytes free,167220minimum,151552largest block and9272task-stack bytes free.
These pre-commissioning samples are not a pairing peak measurement.

The first manual develop15 retry displayed ìyou need a hubî. UART shows
attestation/CSR followed by ArmFailSafe0 cleanup, without AddNOC or operational
certificate verification. The user reports an existing Google Nest on their
home network; subsequent app control and network availability were confirmed. This first attempt did not exercise the repaired certificate path. The later
retry after console configuration did; see the successful develop17 result below. OTA install/reboot and DHCP fault scenarios were
not repeated in this investigation; earlier passes retain their original pins.
See the [machine-readable receipt](google-home-validation.json).

## Follow-up: development integration identity

The user confirmed that the Nest responds to app volume control, the phone is
on the same Wi-Fi and the earlier PC simulator could be added. A bounded TCP
connection to the Nest's Cast port also succeeded. Its displayed Fuchsia build
was31.20260429.103.8713700; exact hardware identification is not required for
this Wi-Fi test because both Nest Hub generations and Max support Matter/Wi-Fi.

The PC simulator declares VID0xFFF1/PID0x8001. The bulb's provisioned manifest and
factory generator declare VID0xFFF1/PID0x8000. The user confirmed that the latter
integration did not exist in Google Home Developer Console and has now added it.
Google's [development pairing rules](https://developers.home.google.com/matter/integration/pair#pairing_restrictions)
require the corresponding VID/PID integration and authorized account; its
[codelab](https://developers.home.google.com/codelabs/matter-device) instructs
rebooting the hub after integration configuration changes.

No firmware identity, certificates or QR were changed to match the simulator.
CSR construction/publishing code is identical to develop14, which previously
reached AddNOC. After creating that integration and restarting the hub, develop15 accepted
Google AddNOC using LKG and reached ConnectNetwork. This combined retest does
not isolate the effect of integration creation from the hub restart.
After the initial BLE window expired, a single ordinary EN restart reopened it;
UART capture continues with device state preserved. The subsequent result is recorded below.

## Channel13 and successful develop17 commissioning

The target AP advertises2.4GHz/channel13 and WPA3-Personal. Its beacon Country IE
is CH, channels1ñ13. Google supplied US in SetRegulatoryConfig even though the
user confirmed physical testing in Poland and a correct Google Home address.
Develop15/16 explicitly disabled802.11d, forcing a manual1ñ11 scan domain.
Develop15's two20-second ConnectNetwork attempts failed, then fail-safe removed
the candidate fabric. DHCP and operational discovery had not started.

ESP-IDF6.1's Wi-Fi Country Code documentation specifies that AUTO actively scans
1ñ11 and passively scans12ñ14, adopting the connected AP's Country IE. Develop17
therefore enables802.11d AUTO from initial world-safe startup and when applying
published Matter Location. It does not hardcode PL or rewrite Google's Location.
A separate small application policy maps supported countries/XX/unknown values,
caches SDK success and retries failure. It runs after durable model publication,
including restored state; it has no independent persistence journal. The common
ESP port adds owner-thread disconnect reason and association channel/auth traces.
No credentials or packet payloads are added to those traces.

Develop17, with the same deliberately old build epoch and factory QR, passed the
following physical sequence:

- Google attestation/CSR, trusted root, AddNOC and LKG verification success.
- WPA3-SAE Hunt-and-Peck association on channel13 with PMF, IPv6 and DHCPv4
  ready. The AP advertises WPA2/WPA3 transition mode (AP auth enum7); the
  negotiated authentication is SAE (driver dot11_authmode0x6).
- Operational CASE, CommissioningComplete and durable LKG publication.
- User confirmed the bulb appears in Google Home and app control works. The
  individual OnOff/brightness/color cases were not separately reported.
- Native direct HTTP status returned200/version17/fabrics1/Wi-Fi ready.
- One ordinary EN restart preserved the fabric and automatically reconnected
  to channel13/WPA3 in about16.3seconds from the reset request. SDK disconnect
  reasons2 and15 occurred before successful association, exercising retries.
  Direct HTTP status again returned200/version17/fabrics1/Wi-Fi ready.
  The older build floor did not cause a new LKG reconstruction write after boot.

The complete pairing boot observed124432bytes minimum free8-bit heap. A settled
sample had178360bytes free and131072largest contiguous block. These SDK-wide
counters include Wi-Fi/BT and task allocations; periodic samples are not a bound
on every instantaneous largest-block or stack excursion. Before the ordinary
restart, the owner task reported9256bytes stack free. app_main's compiler frame
remains656bytes; country synchronization80bytes, mapping48bytes and SDK wrapper
32bytes. Heap counters reset on reboot; the receipt retains both boot sequences.

Windows application CTest passed4/4, including country cache/error/retry,
unknown/XX fallback, restored Location and reboot. Linux application CTest passed
4/4 normally and separately under ASan/UBSan/LSan; clock and country cases passed
Memcheck. Linux core CTest passed120/120 normally and separately with sanitizers;
full two-runtime stock-chip-tool regressions passed Memcheck and separate
ASan/UBSan/LSan. These host tests do not emulate the ESP RF driver; channel13/WPA3
is established by the physical log. ESP-IDF compiled develop16 and17 and the
application-only writes were hash-verified. Existing4% slot margin remains.
The signed develop17 SHA256 is
`d3eec978e69ba5e015085c0f40d3ad508f1acc912cae6e618880513c0d957d2f`.

No factory reset or fabric deletion was performed. The user's Google fabric,
Wi-Fi settings and factory QR remain on COM23. No new release, Apple Home test,
CSA campaign, DHCP fault injection or OTA installation was run in this follow-up.
Long-duration app stability, hidden SSIDs on passive channels and app control
following the final ordinary restart remain unverified. Next: a bounded Google
Home reconnect/Subscribe soak, retaining the user fabric.
