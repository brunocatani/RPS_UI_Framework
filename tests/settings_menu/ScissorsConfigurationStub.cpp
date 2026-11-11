#include <SCISSORS/Configuration.h>
namespace config=scissors::configuration;
namespace {
    // Starts from SCISSORS' compiled defaults, as the real module does without INIs.
    constexpr std::uint32_t defaults() {std::uint32_t bits=0;for(const auto& s:config::kSettings)if(s.defaultValue)bits|=config::bit(s.id);return bits;}
    config::Snapshot state{sizeof(config::Snapshot),defaults(),1};
    config::Status snapshot(config::Snapshot* result) noexcept {if(!result)return config::Status::InvalidArgument;*result=state;return config::Status::Ok;}
    config::Status set(config::Setting id,std::uint32_t value,char*,std::uint32_t) noexcept {
        if(static_cast<std::uint32_t>(id)>=config::kSettings.size()||value>1)return config::Status::InvalidArgument;
        state.values=value?state.values|config::bit(id):state.values&~config::bit(id);++state.revision;return config::Status::Ok;
    }
    config::NumberSnapshot defaultNumbers() {
        config::NumberSnapshot result;result.count=static_cast<std::uint32_t>(config::kNumbers.size());result.revision=1;
        for(const auto& n:config::kNumbers)result.values[static_cast<std::size_t>(n.id)]=n.defaultValue;
        return result;
    }
    config::NumberSnapshot numberState=defaultNumbers();
    config::Status numbers(config::NumberSnapshot* result) noexcept {
        if(!result)return config::Status::InvalidArgument;
        *result=numberState;result->revision=state.revision;return config::Status::Ok;
    }
    config::Status setNumber(config::Number id,float value,char*,std::uint32_t) noexcept {
        if(static_cast<std::uint32_t>(id)>=config::kNumbers.size())return config::Status::InvalidArgument;
        numberState.values[static_cast<std::size_t>(id)]=value;++state.revision;return config::Status::Ok;
    }
    const config::Api api{sizeof(config::Api),config::kMajor,config::kMinor,0,snapshot,set,numbers,setNumber};
}
extern "C" __declspec(dllexport) const config::Api* SCISSORS_GetConfiguration(std::uint32_t major,std::uint32_t minor,std::uint32_t size) noexcept {
    return major==config::kMajor&&minor<=config::kMinor&&size>=sizeof(config::Api)?&api:nullptr;
}
