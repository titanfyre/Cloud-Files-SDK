# Cloud-Files-SDK Python SDK

`pip install cloud-files-sdk`

Windows x64 Python3.10+. No driver is installed by this package.

```python
from cloud_files_sdk import Client
with Client() as client:
    info = client.call("status")
```

[Operation schema and API](docs/API.md). Check errors and the product licensing state before device creation.
