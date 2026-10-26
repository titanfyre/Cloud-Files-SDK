# Validation and boundaries

Cloud Files SDK 0.1.0 targets Windows 10 version 2004/build 19041+ x64 and Windows 11 x64 using documented
CFAPI and built-in cldflt. It does not contain a custom Cloud Files filter.

## Offline checks

Normal and AddressSanitizer builds pass 1,069 assertions, including 1,024 random
file reads compared with independent bytes, catalog/path/range/parent checks,
cancellation, changed-source refusal and hardlink refusal. These use ordinary
owned temporary files, register no host cloud root and install no host driver.
Studio, CLI, example and both setups compile W4/WX, static CRT, CFG, ASLR and NX.
The labelled preview is synthetic and is not live-hydration evidence.

## Completed guest campaigns

The frozen candidate completed both a normal campaign and a targeted Driver
Verifier campaign in the owned Hyper-V Windows build 19045
(10.0.19045) guest. Each completed installation, its explicit runtime
fixtures, package removal and required normal restarts. The guest was restored
to its original checkpoint and finished Off; host boot/security policy was not
changed. These results cover that candidate and fixture, not every Windows PC.

New kernel candidates used guest-only test signing. Independent fixture drivers
retained their verified Microsoft signatures. This is functional qualification,
not a Microsoft attestation/HLK/WHQL claim. Returned Microsoft packages and the
final embedded setup still require their own trust, membership and installation
checks before installable binaries are distributed.

## Demo and activation qualification

The production runtime is configured for one hour per product per Windows boot.
The production functional campaigns **did not wait a full hour**. A separately
pinned private build of the same runtime code used an explicit twelve-second
duration to exercise real expiry, per-product independence, no reset by closing
and reopening handles or repeated Begin, full-mode activation after expiry, and
full-mode persistence beyond another twelve seconds. Products 8 and 10 expired
on their own deadlines and were not unlocked by product 7's activation. Product
9 was already full after its own USB
test and is not described as an unused slot in the lifecycle probe.

The USB short-runtime fixture independently kept a read pending across the real
cutoff, observed completion after expiry and before its independent physical-read
timeout budget, and refused a new export after expiry. Genuine full-mode transfer
recovery followed. Known-IOCTL malformed buffers, restricted callers, limited
handle access, wrong-handle/cross-product proofs, modified signatures, replaced
and consumed nonces, and accepted-proof replay were refused. Five original
broker grants were independently verified against the public P256 key. These
tests are not proof of unbreakable licensing or immunity to kernel compromise.

Normal and AddressSanitizer offline builds also passed the strict shared runtime
installer's 115 identity/trust-refusal checks. License-server HTTP and signature
verification run during activation under a monotonic whole-operation budget.
Data paths retain their local entitlement checks and normal processing costs;
encrypted transports still perform their own TLS work.

## Cloud Files fixture scope

Both guest campaigns passed real registration and placeholders, partial/full and
four-thread hydration, separate-consumer rename/delete refusal, source
preservation, disconnect/cache behavior, uncached offline refusal, unchanged
snapshot resume, exact-root release, genuine full activation and exact fixture
cleanup. Namespace refusal was exercised outside the connected provider process;
CFAPI permits the sync engine's own maintenance. Counters and assertion totals
are functional evidence, not storage/network throughput or latency benchmarks.

The frozen SDK implementation SHA256 is 7c32ec47318ad6eac94e3c98221b632af6c42704bc27663179f381ed894c914d.
The shared production runtime input SHA256 is 5ee4e64afeffe527a02b74fc50bdcc7eed4808e6e3ec9a3db028039057c6bf80.

Windows 11, Secure Boot/HVCI, physical storage variation, antivirus/filter
coexistence, suspend/resume, abrupt power loss, low resources and long workloads
remain separate qualification work. This provider is a read-only static snapshot
backend, not a bidirectional synchronizer or an application-consistent snapshot.

## Data and cancellation behavior

This is an initial read-only, static-namespace provider. The local backend checks
opened file identity, size, link count and modification time against enumeration;
its metadata snapshot is not a cryptographic or VSS snapshot. It rejects source
changes it observes and refuses write-sharing while reading a chunk. Applications
must keep backing data stable or provide their own immutable revisioned reader.

Each local read uses bounded overlapped I/O and cancellation. The SDK honors a
full fetch cancellation and checks permission/cancellation before publishing a
chunk. Partial cancellation may allow previously requested extra bytes to finish.
Custom callbacks must cooperate and return promptly. Stop waits for callbacks to
drain; neither the SDK nor C++ can safely kill an arbitrary blocked callback.
Exceptional failure of Windows disconnection retains context until process exit
to avoid freeing callback storage still reachable by Windows.

Unregistering a root is explicit. Windows removes dehydrated placeholders and
turns hydrated files into ordinary files; the SDK does not recursively delete
user cache or backend data. Cached content can remain readable after disconnect
or license expiry. Root identity checks prevent this SDK from unregistering a
different provider's root or treating a descendant as the root.

The runtime's signed activation controls the supplied implementation. Public
Cloud Files APIs and public provider source can be used by other user-mode code;
this does not create a new security boundary around Microsoft's built-in filter.

## Primary references

- [Register a sync root](https://learn.microsoft.com/en-us/windows/win32/api/cfapi/nf-cfapi-cfregistersyncroot)
- [Connect callbacks](https://learn.microsoft.com/en-us/windows/win32/api/cfapi/nf-cfapi-cfconnectsyncroot)
- [Publish transfer data](https://learn.microsoft.com/en-us/windows/win32/api/cfapi/nf-cfapi-cfexecute)
- [Callback ranges, alignment and cancellation](https://learn.microsoft.com/en-us/windows/win32/api/cfapi/ns-cfapi-cf_callback_parameters)
- [Consumer rename and deletion callbacks](https://learn.microsoft.com/en-us/windows/win32/api/cfapi/ne-cfapi-cf_callback_type)
- [Disconnect behavior](https://learn.microsoft.com/en-us/windows/win32/api/cfapi/nf-cfapi-cfdisconnectsyncroot)
- [Unregister/cache behavior](https://learn.microsoft.com/en-us/windows/win32/api/cfapi/nf-cfapi-cfunregistersyncroot)
