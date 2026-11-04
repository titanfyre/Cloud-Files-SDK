#include "License.h"
#include <iostream>
int main(){
 static_assert(sizeof(TS_STATUS)==32);
 static_assert(sizeof(TS_CHALLENGE)==48);
 static_assert(sizeof(TS_TOKEN)==112);
 for(unsigned product=7;product<=10;++product){
  const auto error=TitanSdk::Verification(product,"ignored proposal input");
  if(error!="Full activation is unavailable in this demo-only variant.")return 1;
 }
 if(TitanSdk::Verification(6,{}).empty())return 2;
 std::cout<<"Public ABI layouts retained; all product Verification calls reject activation without runtime or network access.\n";
 return 0;
}