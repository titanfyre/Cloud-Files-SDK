# Cloud-Files-SDK .NET8 SDK

Package ID `CloudFilesSDK.SDK`. ```csharp
using var client=new TitanCSA.Products.CloudFilesSDK.Client();
string statusJson=client.Call("status", "{}");
```

Client implements IDisposable; Call takes raw argument JSON object and returns result JSON. NativeException exposes Code. UInt64 fields remain decimal strings. [API](docs/API.md). Windows x64 and separately installed matching driver required.
