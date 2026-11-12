import os
from pathlib import Path

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import copy, get, rmdir

required_conan_version = ">=2.0.9"


class CloudFilesSdkConan(ConanFile):
    name = "cloud-files-sdk"
    description = "Windows x64 C++ SDK for read-only Cloud Files placeholder providers"
    license = "MIT"
    url = "https://github.com/conan-io/conan-center-index"
    homepage = "https://github.com/titanfyre/Cloud-Files-SDK"
    topics = ("windows", "cloud-files", "filesystem", "placeholders")
    package_type = "static-library"
    settings = "os", "arch", "compiler", "build_type"

    def layout(self):
        cmake_layout(self, src_folder="src")

    def validate(self):
        if self.settings.os != "Windows" or self.settings.arch != "x86_64":
            raise ConanInvalidConfiguration("Upstream supports Windows x64 only")
        if self.settings.compiler != "msvc":
            raise ConanInvalidConfiguration("This Windows SDK is validated with MSVC")
        check_min_cppstd(self, 17)

    def build_requirements(self):
        self.tool_requires("cmake/[>=3.24 <4]")

    def source(self):
        get(self, **self.conan_data["sources"][str(self.version)])

    def generate(self):
        CMakeToolchain(self).generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        CMake(self).install()
        copy(self, "LICENSE", src=self.source_folder,
             dst=os.path.join(self.package_folder, "licenses"))
        # Conan's CMakeDeps supplies the same upstream package/target names.
        package_root = Path(self.package_folder).resolve()
        for relative in ("lib/cmake", "share"):
            target = package_root / relative
            if package_root not in target.resolve().parents:
                raise ConanInvalidConfiguration("Unexpected package directory")
            rmdir(self, str(target))

    def package_info(self):
        self.cpp_info.libs = ["TitanCloudFilesSDK"]
        self.cpp_info.includedirs = ["include", "include/SDK"]
        self.cpp_info.system_libs = ["advapi32", "bcrypt", "cldapi", "comctl32",
                                     "crypt32", "gdi32", "newdev", "ole32", "setupapi",
                                     "shell32", "user32", "winhttp", "wintrust"]
        self.cpp_info.set_property("cmake_file_name", "CloudFilesSDK")
        self.cpp_info.set_property("cmake_target_name", "CloudFilesSDK::sdk")
