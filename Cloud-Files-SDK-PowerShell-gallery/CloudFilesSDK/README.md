# Cloud-Files-SDK PowerShell SDK

PowerShell7.4+ Windows x64. `Install-Module CloudFilesSDK -Scope CurrentUser`.

```powershell
$client=Connect-CloudFilesSDKSDK
try { Invoke-CloudFilesSDKSDK -Client $client -Operation status } finally { Disconnect-CloudFilesSDKSDK -Client $client }
```

Use -Arguments with a hashtable matching [schema](docs/API.md). Native errors retain their Windows code through the inner exception. Matching driver installed separately.
