#include "render/NativeSceneTargets.h"
#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string_view>

namespace native = rpsui::render::native_scene_targets;
namespace {
    constexpr std::uintptr_t base=0x140000000, renderer=0x20000000;
    constexpr std::uintptr_t depth=0x30000100, color=0x40000100;
    struct Memory {
        std::map<std::uintptr_t,std::array<std::byte,8>> cells;
        std::uintptr_t denied{};
        template<class T> void put(std::uintptr_t address,T value) {
            std::memcpy(cells[address].data(),&value,sizeof(value));
        }
        bool operator()(std::uintptr_t address,void* out,std::size_t size) const noexcept {
            const auto it=cells.find(address);
            if(address==denied || it==cells.end() || size>it->second.size())return false;
            std::memcpy(out,it->second.data(),size);return true;
        }
    };
    void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
    Memory fixture() {
        Memory memory;
        memory.put(base+native::kRendererDataRva,renderer);
        memory.put(base+native::kSceneSelectorRva,std::uint8_t{0});
        memory.put(base+native::kManagerRva+native::kDepthMapOffset+4,std::uint32_t{2});
        memory.put(renderer+native::kDepthArrayOffset+2*native::kDepthStride,depth);
        memory.put(base+native::kManagerRva+native::kColorMapOffset+16,std::uint32_t{3});
        memory.put(renderer+native::kColorArrayOffset+3*native::kColorStride,color);
        return memory;
    }
}
int main() {
    try {
        auto memory=fixture();
        const auto read=[&](auto address,auto out,auto size) noexcept {return memory(address,out,size);};
        native::Snapshot scene;
        require(native::readDepth(base,read,scene) && scene.logicalDepth==1 && scene.depthTexture==depth,"Scene depth role did not resolve");
        require(native::readColor(base,read,scene) && scene.colorTexture==color,"Final color role did not resolve independently");
        require(scene.depthTexture!=scene.colorTexture,"Separate color and depth resources were conflated");

        // Depth is captured before the final postprocessed color target is allocated.
        memory.cells.erase(base+native::kManagerRva+native::kColorMapOffset+16);
        require(native::readDepth(base,read,scene),"Depth capture incorrectly depends on final color allocation");
        require(!native::readColor(base,read,scene) && std::string_view(scene.stage)=="color-slot","Missing final color did not fail closed");

        memory=fixture();
        memory.put(base+native::kSceneSelectorRva,std::uint8_t{1});
        memory.put(base+native::kManagerRva+native::kDepthMapOffset+12*4,std::uint32_t{5});
        memory.put(renderer+native::kDepthArrayOffset+5*native::kDepthStride,depth+0x100);
        require(native::readDepth(base,read,scene) && scene.logicalDepth==12 && scene.depthTexture==depth+0x100,"Alternate scene depth role was ignored");

        // Each failed hop must report its own stage without reading past it.
        const std::array<std::pair<std::uintptr_t,const char*>,4> failures{{
            {base+native::kRendererDataRva,"renderer-root"}, {base+native::kSceneSelectorRva,"scene-selector"},
            {base+native::kManagerRva+native::kDepthMapOffset+4,"depth-slot"},
            {renderer+native::kDepthArrayOffset+2*native::kDepthStride,"depth-texture"}}};
        for(const auto& [address,stage]:failures) {
            memory=fixture();memory.denied=address;
            require(!native::readDepth(base,read,scene) && std::string_view(scene.stage)==stage,"Incorrect failed-hop diagnostic");
        }
        memory=fixture();memory.put(base+native::kRendererDataRva,std::uintptr_t{0x123});
        require(!native::readDepth(base,read,scene),"Invalid renderer pointer accepted");
        memory=fixture();memory.put(base+native::kSceneSelectorRva,std::uint8_t{2});
        require(!native::readDepth(base,read,scene),"Invalid scene selector accepted");
        for(auto index:{native::kDepthCapacity,0xFFFFFFFFu}) {
            memory=fixture();memory.put(base+native::kManagerRva+native::kDepthMapOffset+4,index);
            require(!native::readDepth(base,read,scene),"Out-of-range depth slot accepted");
        }
        memory=fixture();memory.put(renderer+native::kDepthArrayOffset+2*native::kDepthStride,std::uintptr_t{0});
        require(!native::readDepth(base,read,scene),"Missing native depth texture accepted");
        memory=fixture();require(native::readDepth(base,read,scene),"Fixture did not resolve");
        memory.put(base+native::kManagerRva+native::kColorMapOffset+16,native::kColorCapacity);
        require(!native::readColor(base,read,scene),"Out-of-range color slot accepted");
        std::cout<<"Native scene target roles, missing allocations, and failed pointer hops passed\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
