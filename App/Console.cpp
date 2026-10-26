#include "../SDK/CloudFiles.h"
#include <iostream>
#include <string>
int wmain(int argc,wchar_t** argv){try{
 if(argc==2&&!wcscmp(argv[1],L"--preview")){std::cout<<"Cloud Files SDK | OFFLINE PREVIEW\nExpose an immutable local directory as on-demand Windows placeholders.\nNo sync root, runtime or driver is opened in preview mode.\n";return 0;}
 if(argc==3&&!wcscmp(argv[1],L"release")){auto error=CloudFiles::Provider::Release(argv[2]);if(!error.empty()){std::cerr<<error<<"\n";return 1;}std::cout<<"Registration released. Hydrated files remain local; dehydrated placeholders were removed by Windows.\n";return 0;}
 if(argc!=4||wcscmp(argv[1],L"serve")){std::cout<<"Cloud Files SDK | static read-only provider\n\nCloudFilesCLI.exe serve C:\\Backing C:\\NewCloudRoot\nCloudFilesCLI.exe release C:\\CloudRoot\n\nThe backing directory stays unchanged. Start requires the shared runtime and administrator rights.\n";return 2;}
 CloudFiles::Provider provider;auto backend=CloudFiles::LocalDirectory(argv[2]);auto error=provider.Start(argv[3],std::move(backend));if(!error.empty()){std::cerr<<error<<"\n";return 1;}
 std::cout<<"Connected. Read files inside the sync root to hydrate their contents.\nCommands: status, activate, stop. A blank activation key cancels.\n";
 for(std::string command;std::getline(std::cin,command);){
  if(command=="stop")break;
  if(command=="activate"){std::string key;std::cout<<"Product key (not saved): ";std::getline(std::cin,key);if(!key.empty()){error=provider.Verification(key);SecureZeroMemory(key.data(),key.size());std::cout<<(error.empty()?"Full access activated for this Windows boot.":error)<<"\n";}}
  else{auto status=provider.GetStatus();std::cout<<status.entries<<" entries | "<<status.fetches<<" fetches | "<<status.hydrated_bytes<<" bytes | "<<status.failures<<" failures\n"<<status.detail<<"\n";}
 }
 error=provider.Stop();if(!error.empty()){std::cerr<<error<<"\n";return 1;}std::cout<<"Disconnected. Cached data and registration remain; use release only when finished with this snapshot.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}
