# Cloud Files SDK: demo-only source preview

This is a distinct source preview with matching [SDK/](SDK/) client code. It does
not reproduce the current Microsoft-signed release packages. The repository's
top-level `SDK/`, application, installer and published signed binaries continue
to target the existing combined demo/full runtime.

Existing installers cannot install this preview. No matching signed package or
installer is supplied here. Existing signed SYS/CAT files must not be substituted
for these newly built images.

## Behavior and identities

The preview runtime is `TitanSdkDemoRuntime.sys`, service
`TitanSdkDemoRuntime`, with device/link identity `TitanSdkDemoRuntime1`.
It is separate from `TitanSdkRuntime` and does not inherit or migrate that
runtime's current-boot demo budget. All four preview SDKs share this one preview
runtime, with a separate 3,600-second budget per product and Windows boot.

First live use starts the product's kernel-owned deadline. Closing applications
or reloading a function driver does not renew it; the resident runtime remains
loaded until Windows restarts. There is no software-timer fallback when the
matching runtime is unavailable.

Full activation is physically absent from this source variant. The public
`Verification` signature remains for API compatibility and always returns an
unsupported error. Activation IOCTLs return `STATUS_NOT_SUPPORTED`. Reserved
challenge/token layouts and operation numbers remain declarations for ABI
compatibility; no code verifies or accepts activation grants.

Windows supplies the Cloud Files filter (`cldflt`). This preview's custom kernel component supplies the demo budget; the provider remains a user-mode SDK.

Use [SDK/](SDK/) from this folder for the preview. Do not mix it with the
top-level SDK or current release installers. Cloud Files and Remote Workstation
use public Windows APIs; modified user-mode code can bypass the supplied SDK
policy. This source does not claim resistance to arbitrary kernel control.

## Build

Use Windows x64, 64-bit Windows PowerShell, Visual Studio 2022 C++ x64 tools,
matching x64 Spectre-mitigated libraries, CMake/CTest, and Windows SDK/WDK
**10.0.26100.0** with Visual Studio integration. MSVC **14.32.31326** was
validated. The wrapper selects the newest installed x64 toolset with matching
Spectre libraries.

From the repository root:

```powershell
.\driver\Build.ps1
# Compile the same Release x64 configuration from scratch:
.\driver\Build.ps1 -Rebuild
```

[Build.ps1](Build.ps1) builds these unsigned kernel projects:

- [Runtime/TitanSdkDemoRuntime.vcxproj](Runtime/TitanSdkDemoRuntime.vcxproj)

It also compiles the matching SDK through [CMakeLists.txt](CMakeLists.txt),
links every SDK object, and runs [DemoAbiProbe.cpp](DemoAbiProbe.cpp). That
probe checks public ABI layouts and unconditional rejection of activation;
it does not open a runtime, contact an activation server or exercise devices.
Build outputs and intermediate files go to `obj/DemoSourceBuild/` at the
repository root, outside this source folder. Signing, test signing and Inf2Cat
are disabled. The wrapper does not install/load a driver or change Windows
security or network configuration.

## Validation boundary

Compilation, linking and the ABI rejection probe passed for this source layout.
These checks do not qualify driver loading, live demo expiry/reboot behavior,
device removal, hardware compatibility or production installation. New signing
and runtime qualification remain separate work. The current signed release's
validation results do not qualify this new variant.

[source-manifest.json](source-manifest.json) records source hashes, the
portable-layout change and validation boundaries. Existing application/SDK/
installer bytes are checked separately in
[source-boundaries.json](source-boundaries.json).

## Source licenses

[LICENSE.txt](LICENSE.txt) supplies separate evaluation-source terms only
for files identified as Titan evaluation source in
[LICENSE-MAP.txt](LICENSE-MAP.txt). It permits inspection, compilation,
internal evaluation and source-review contributions while retaining the
covered demo controls and separate production/OEM terms.

The file map also identifies complete retained MIT/MS-PL permissions.
Those permissions apply to the whole mapped files, including sample-derived
files with Titan changes; the evaluation restrictions do not narrow them.
The root application's/SDK's existing license and previously distributed
driver-binary terms remain unchanged. Full/commercial driver source and
private signing/activation material are outside this source package.
