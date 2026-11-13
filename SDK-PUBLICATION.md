# SDK publication status

Current source layout and registry coverage. A GitHub download is distinct from publication on a central registry.

| Target | Status | Package / documentation |
| --- | --- | --- |
| CPP/vcpkg | published_custom_registry | cloud-files-sdk |
| CPP/conan | published_custom_source_index | [cloud-files-sdk](https://github.com/TitanCSA/sdk-packages/tree/089bc73095aa8eda89bacbe10d0b9e58394b0207/conan-index) [Source](packaging/native/) |
| CPP/nuget | published_payload_verified | [Cloud-Files-SDK.Native](https://www.nuget.org/packages/Cloud-Files-SDK.Native/0.1.1) |
| NET/nuget | published_payload_verified | [CloudFilesSDK.SDK](https://www.nuget.org/packages/CloudFilesSDK.SDK/1.0.0) |
| Python/pypi | built_fixture_and_production_verified | cloud-files-sdk [Source](Cloud-Files-SDK-Python-pypi/) |
| Python/conda-forge | custom_channel_install_verified_requires_conda_forge_review | [cloud-files-sdk](https://raw.githubusercontent.com/TitanCSA/sdk-packages/089bc73095aa8eda89bacbe10d0b9e58394b0207/conda-channel) [Source](packaging/conda/) |
| Rust/cargo | published_install_verified | [cloud-files-sdk](https://crates.io/crates/cloud-files-sdk) [Source](Cloud-Files-SDK-Rust-cargo/) |
| JavaScript/npm | stopped_before_submission | cloud-files-sdk [Source](Cloud-Files-SDK-JavaScript-npm/) |
| Java/maven | published_install_verified | [cloud-files-sdk](https://central.sonatype.com/artifact/io.github.titancsa/cloud-files-sdk/1.0.0) [Source](Cloud-Files-SDK-Java-maven/) |
| Java/jitpack | published_install_verified | [cloud-files-sdk](https://jitpack.io/#titanfyre/Cloud-Files-SDK/java-sdk-v1.0.0) [Source](Cloud-Files-SDK-Java-maven/) |
| Go/modules | published_install_verified | [cloud-files-sdk](https://pkg.go.dev/github.com/titanfyre/Cloud-Files-SDK/Cloud-Files-SDK-Go-modules@v1.0.0) [Source](Cloud-Files-SDK-Go-modules/) |
| Go/pkg-go-dev | indexed_verified | [Documentation](https://pkg.go.dev/github.com/titanfyre/Cloud-Files-SDK/Cloud-Files-SDK-Go-modules@v1.0.0) |
| PowerShell/gallery | published_install_verified | [CloudFilesSDK](https://www.powershellgallery.com/packages/CloudFilesSDK/1.0.0) [Source](Cloud-Files-SDK-PowerShell-gallery/) |
| Dart/pub-dev | built_fixture_and_production_verified | cloud_files_sdk [Source](Cloud-Files-SDK-Dart-pub-dev/) |
| Ruby/rubygems | published_install_verified | [cloud-files-sdk](https://rubygems.org/gems/cloud-files-sdk) [Source](Cloud-Files-SDK-Ruby-rubygems/) |
| PHP/packagist | composer_source_and_tag_published | [titancsa/cloud-files-sdk](https://packagist.org/packages/titancsa/cloud-files-sdk) |

Native SDK source and CMake/Conan/vcpkg/NuGet recipes share [`packaging/native`](packaging/native/LAYOUT.md). Previous release archives and tags remain unchanged.

Application and driver prerequisites and original licensing remain those documented by each SDK. No driver installer is bundled with these source packages.
