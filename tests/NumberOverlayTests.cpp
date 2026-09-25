#include "NumberOverlayLayout.h"
#include "RPSUINumberOverlayApi.h"
#include <cassert>
#include <limits>

int main()
{
    using rpsui::number_overlay::layout;
    const auto zero=layout(0,1024,480);
    assert(zero.count==1 && zero.glyphs[0].u0==0 && zero.glyphs[0].u1==0.1f);
    const auto magazine=layout(30,1024,480);
    assert(magazine.count==2 && magazine.glyphs[0].u0==0.3f && magazine.glyphs[1].u0==0);
    const auto maximum=layout(std::numeric_limits<std::uint32_t>::max(),1024,480);
    assert(maximum.count==10);
    for (const auto* sample:{&zero,&magazine,&maximum}) for (std::size_t i=0;i<sample->count;++i) {
        const auto& g=sample->glyphs[i];
        assert(g.left>=0 && g.right<=1024 && g.top>=0 && g.bottom<=480);
        assert(g.right>g.left && g.bottom>g.top && g.u0>=0 && g.u1<=1);
        if (i) assert(g.left>=sample->glyphs[i-1].right-0.001f);
    }
    assert(!layout(12,0,480).count);
    assert(!layout(12,1024,std::numeric_limits<float>::quiet_NaN()).count);
    // New records do not append fields to, alias, or repurpose released V1 ABI.
    static_assert(sizeof(rpsui::sdk::NumberOverlayApiV1)==40);
    static_assert(offsetof(rpsui::sdk::NumberOverlayApiV1,registerOverlay)==16);
    static_assert(offsetof(rpsui::sdk::NumberOverlayPresentationV1,value)==72);
}
