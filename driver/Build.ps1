param([switch]$Rebuild)
$ErrorActionPreference='Stop'
if(![Environment]::Is64BitProcess){throw 'Use 64-bit Windows PowerShell.'}
$source=$PSScriptRoot
$repository=Split-Path $source -Parent
$buildRoot=Join-Path $repository 'obj\DemoSourceBuild'
$projects=@(Get-ChildItem -LiteralPath $source -Recurse -Filter '*.vcxproj' -File | Sort-Object FullName)
if(!$projects.Count){throw 'No demo-only driver source projects were found.'}
$programFilesX86=[Environment]::GetFolderPath('ProgramFilesX86')
$vswhere=Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
if(!(Test-Path -LiteralPath $vswhere -PathType Leaf)){throw 'Install Visual Studio 2022 C++ tools and WDK 10.0.26100.0.'}
$vs=& $vswhere -latest -products * -version '[17.0,18.0)' -requires Microsoft.Component.MSBuild Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'Visual Studio 2022 with C++ x64 tools and MSBuild is required.'}
$msbuild=Join-Path $vs 'MSBuild\Current\Bin\amd64\MSBuild.exe'
if(!(Test-Path -LiteralPath $msbuild -PathType Leaf)){throw 'Visual Studio x64 MSBuild was not found.'}
$toolset=Get-ChildItem -LiteralPath (Join-Path $vs 'VC\Tools\MSVC') -Directory |
    Where-Object {
        (Test-Path -LiteralPath (Join-Path $_.FullName 'bin\Hostx64\x64\cl.exe') -PathType Leaf) -and
        (Test-Path -LiteralPath (Join-Path $_.FullName 'lib\spectre\x64\libcmt.lib') -PathType Leaf)
    } |
    Sort-Object {[version]$_.Name} -Descending |
    Select-Object -First 1
if(!$toolset){throw 'Install matching x64 MSVC tools and x64 Spectre-mitigated libraries.'}
$kit=Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots' -Name KitsRoot10 -ErrorAction SilentlyContinue
$kitRoot=if($kit -and $kit.KitsRoot10){$kit.KitsRoot10}else{Join-Path $programFilesX86 'Windows Kits\10'}
if(!(Test-Path -LiteralPath (Join-Path $kitRoot 'build\10.0.26100.0\WindowsDriver.common.targets') -PathType Leaf)){throw 'Install SDK and WDK 10.0.26100.0 with Visual Studio integration.'}
$cmakeCommand=Get-Command cmake.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
$cmake=if($cmakeCommand){$cmakeCommand.Source}else{Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'}
$ctest=Join-Path (Split-Path $cmake -Parent) 'ctest.exe'
if(!(Test-Path -LiteralPath $cmake -PathType Leaf) -or !(Test-Path -LiteralPath $ctest -PathType Leaf)){throw 'Install CMake and CTest, or Visual Studio CMake tools.'}
$target=if($Rebuild){'Rebuild'}else{'Build'}
Write-Host ('Compiling unsigned demo source with MSVC '+$toolset.Name+'. Output: '+$buildRoot)
foreach($project in $projects){
    $relative=$project.FullName.Substring($source.Length+1)
    $projectKey=[IO.Path]::ChangeExtension($relative,$null).TrimEnd('.')
    $projectBuild=Join-Path $buildRoot $projectKey
    $native=Join-Path $projectBuild 'bin'
    $intermediate=Join-Path $projectBuild 'obj'
    New-Item -ItemType Directory -Path $native,$intermediate -Force | Out-Null
    $arguments=@(
        $project.FullName,'/nologo','/m','/v:minimal',('/t:'+$target),
        '/p:Configuration=Release','/p:Platform=x64',('/p:VCToolsVersion='+$toolset.Name),
        '/p:SignMode=Off','/p:EnableTestSign=false','/p:EnableInf2cat=false',
        ('/p:OutDir='+$native+'/'),('/p:IntDir='+$intermediate+'/')
    )
    & $msbuild @arguments
    if($LASTEXITCODE){throw ('Demo-only driver compilation failed: '+$relative)}
}
$sdkBuild=Join-Path $buildRoot 'SDK'
& $cmake -S $source -B $sdkBuild -G 'Visual Studio 17 2022' -A x64 -T ('v143,version='+$toolset.Name) '-DCMAKE_SYSTEM_VERSION=10.0.26100.0'
if($LASTEXITCODE){throw 'Demo-only SDK configuration failed.'}
$sdkArguments=@('--build',$sdkBuild,'--config','Release','--parallel')
if($Rebuild){$sdkArguments+='--clean-first'}
& $cmake @sdkArguments
if($LASTEXITCODE){throw 'Demo-only SDK compilation/link failed.'}
& $ctest --test-dir $sdkBuild -C Release --output-on-failure
if($LASTEXITCODE){throw 'Demo-only SDK ABI/activation-rejection test failed.'}
Write-Host 'Unsigned demo-only source compilation and ABI rejection tests passed.'
Write-Host 'No driver was signed, installed or loaded, and no Windows security or network configuration was changed.'

