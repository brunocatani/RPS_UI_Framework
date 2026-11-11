#include "IniSettingsStore.h"
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

int main() {
 const auto path=std::filesystem::temp_directory_path()/
  ("SettingsMenuPreviewIsolation-"+std::to_string(GetCurrentProcessId())+".ini");
 struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code error;std::filesystem::remove(path,error);}} cleanup{path};
 try {
  const std::string original="[Preview]\nbEnabled=true\nfValue=1.0\n";
  {std::ofstream file(path);file<<original;}
  rpsui::settings_menu::IniSettingsStore store(path);
  if(!store.load() || store.settings().empty())throw std::runtime_error("Preview snapshot failed");
  const auto result=store.setBooleanByIndex(0,false);
  if(!result.changed || !result.saved || store.settings()[0].value!="false")throw std::runtime_error("Preview edit did not update its model");
  const auto numeric=store.setNumericByIndex(1,2.0);
  if(!numeric.changed || !numeric.saved || store.settings()[1].numericValue!=2.0)throw std::runtime_error("Preview numeric model was stale");
  std::ifstream file(path);const std::string after{std::istreambuf_iterator<char>(file),{}};
  if(after!=original)throw std::runtime_error("Preview modified its source INI");
  std::cout<<"Preview edits update memory without changing the source INI\n";
  return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
