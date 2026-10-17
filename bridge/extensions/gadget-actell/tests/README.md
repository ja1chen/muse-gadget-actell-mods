# Public SLAN test materials

`slan-dummy-request.json` and `slan-dummy-claim.json` are public synthetic inputs
from the frozen SLAN/1 handoff, without trailing newlines. IDs, names, claim tokens
and formal keys in the C harnesses are synthetic. They must never be used for pairing.

`slan_pairing_test.c` executes the actual pairing worker and strict codec under
ASan/UBSan, using a fake clock, HTTP peer and atomic storage model. It checks approval,
rejection, expiry, wrong token, substituted pin, storage failures, lost ACK, pending
reboot recovery, cancellation/deletion and a deterministic malformed-input corpus.

`slan_nvs_test.c` executes the actual NVS record adapter with fake atomic NVS and
synthetic certificate/PSA adapters. It proves record handling, not flash durability
or TLS verification. Stubs are host-test include paths only and never staged as
part of the firmware component.

`slan_leaf_test.c` runs the actual leaf verifier against a native build of the same
Mbed TLS source. `tools/check-native-leaf.py` generates fresh test certificates,
checks key/SAN/time/pin/self-signature rejection, then makes loopback TLS handshakes
using the same required-verification callback pattern. Temporary private keys have
0600 permissions and are deleted. It requires Python `cryptography`, native Mbed TLS
and a local C compiler; it is separate from the dependency-free default regression.
No test establishes iPhone interoperability or physical-device acceptance.
