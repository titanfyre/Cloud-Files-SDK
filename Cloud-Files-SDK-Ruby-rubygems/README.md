# Cloud-Files-SDK Ruby SDK

`gem install cloud-files-sdk`

Windows x64 Ruby3.1+.

```ruby
require "cloud_files_sdk"
CloudFilesSDKSDK::Client.open { |client| p client.call("status") }
```

[API and operation schema](docs/API.md). Matching driver installed separately.
