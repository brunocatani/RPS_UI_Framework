#include <DIAMOND/Configuration.h>
namespace config=diamond::configuration;
namespace {
    // Starts from DIAMOND's compiled defaults, as the real module does without a file.
    config::Snapshot defaults() {
        config::Snapshot result;result.count=static_cast<std::uint32_t>(config::kSettingCount);result.revision=1;
        for(const auto& setting:config::kSettings)result.values[static_cast<std::size_t>(setting.id)]=setting.defaultValue;
        return result;
    }
    config::Snapshot state=defaults();
    config::Status snapshot(config::Snapshot* result) noexcept {if(!result)return config::Status::InvalidArgument;*result=state;return config::Status::Ok;}
    config::Status set(config::Setting id,float value,char*,std::uint32_t) noexcept {
        if(static_cast<std::size_t>(id)>=config::kSettingCount)return config::Status::InvalidArgument;
        const auto& metadata=config::metadata(id);
        if(value<metadata.minimum||value>metadata.maximum)return config::Status::InvalidArgument;
        state.values[static_cast<std::size_t>(id)]=value;++state.revision;return config::Status::Ok;
    }
    const config::Api api{sizeof(config::Api),config::kMajor,config::kMinor,0,snapshot,set};
}
extern "C" __declspec(dllexport) const config::Api* DIAMOND_GetConfiguration(std::uint32_t major,std::uint32_t minor,std::uint32_t size) noexcept {
    return major==config::kMajor&&minor<=config::kMinor&&size>=sizeof(config::Api)?&api:nullptr;
}
