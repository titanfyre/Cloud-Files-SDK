param([switch]$Asan,[string]$RuntimePackageDirectory)
$ErrorActionPreference='Stop'
$build=Join-Path $PSScriptRoot $(if($Asan){'obj/Asan'}else{'obj/Build'})
$runtime=@('-DRUNTIME_PACKAGE_DIRECTORY=')
if($RuntimePackageDirectory){$runtime=@("-DRUNTIME_PACKAGE_DIRECTORY=$([IO.Path]::GetFullPath($RuntimePackageDirectory))")}
cmake -S $PSScriptRoot -B $build -G 'Visual Studio 17 2022' -A x64 "-DTRACE_ASAN=$([int]$Asan.IsPresent)" @runtime
if($LASTEXITCODE){throw 'Configuration failed'}
$options=@()
if($Asan){
 $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
 $installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
 $tools=Get-ChildItem -LiteralPath (Join-Path $installation 'VC\Tools\MSVC') -Directory|Sort-Object {[version]$_.Name} -Descending|Select-Object -First 1
 if(!$tools){throw 'AddressSanitizer toolset unavailable'}
 $options=@('--',"/p:VCToolsVersion=$($tools.Name)")
 $env:PATH=(Join-Path $tools.FullName 'bin\Hostx64\x64')+';'+$env:PATH
}
cmake --build $build --config Release @options
if($LASTEXITCODE){throw 'Build failed'}
ctest --test-dir $build -C Release --output-on-failure
if($LASTEXITCODE){throw 'Tests failed'}
