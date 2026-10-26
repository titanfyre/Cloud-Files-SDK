#include "../SDK/CloudFiles.h"
#include <iostream>
int wmain(int argc,wchar_t** argv){
 std::cout<<"Expose a local folder through Windows on-demand files, then disconnect. The source is read only.\n";
 if(argc!=3){std::cout<<"Usage: CloudFilesExample.exe C:\\Backing C:\\NewCloudRoot\n";return 2;}
 try{
  CloudFiles::Provider cloud;
  auto error=cloud.Start(argv[2],CloudFiles::LocalDirectory(argv[1]));
  if(!error.empty()){std::cerr<<error<<"\n";return 1;}
  std::cout<<"Open a file in the new cloud folder. Press Enter to disconnect.\n";
  std::cin.get();error=cloud.Stop();if(!error.empty()){std::cerr<<error<<"\n";return 1;}
  // Optional paid activation: cloud.Verification("YOUR-PRODUCT-KEY").
  // Release(root) is explicit: it removes dehydrated placeholders but keeps cache.
  return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
