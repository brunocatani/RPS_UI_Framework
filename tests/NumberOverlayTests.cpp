#include "NumberOverlayLayout.h"
#include <RPS_UI_Framework/NumberOverlay.h>
#include <cassert>
#include <limits>
#include <utility>

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

    // HUD layout: loaded over reserve, both right aligned to the divider's
    // right end, every quad inside the panel, digits never overlapping.
    using rpsui::number_overlay::hudLayout;
    constexpr rpsui::number_overlay::HudAtlasMetrics atlas{0.2f,0.75f,0.45f};
    for (const bool bracketed:{false,true}) {
        for (const auto [top,bottom]:{std::pair<std::uint32_t,std::uint32_t>{0,0},{30,240},{8,4294967295u}}) {
            const auto hud=hudLayout(top,bottom,bracketed,atlas,1024,480);
            assert(hud.barCount==(bracketed ? 7u : 1u));
            assert(hud.glyphCount>=2);
            const auto& divider=hud.bars[0];
            float firstBottomTop=-1;
            for (std::size_t i=0;i<hud.glyphCount;++i) {
                const auto& g=hud.glyphs[i];
                assert(g.left>=0 && g.right<=1024 && g.top>=0 && g.bottom<=480);
                assert(g.right>g.left && g.bottom>g.top && g.u0>=0 && g.u1<=1 && g.v0==atlas.inkTop && g.v1==atlas.inkBottom);
                if (g.top>divider.bottom) {
                    if (firstBottomTop<0) firstBottomTop=g.top;
                } else {
                    assert(g.bottom<divider.top);
                }
                if (i && hud.glyphs[i-1].top==g.top) assert(g.left>=hud.glyphs[i-1].right-0.001f);
            }
            assert(firstBottomTop>divider.bottom);
            // Each row's last digit ends at the divider's right end.
            float topRight=0,bottomRight=0;
            for (std::size_t i=0;i<hud.glyphCount;++i) {
                auto& rowRight=hud.glyphs[i].top<divider.top ? topRight : bottomRight;
                rowRight=(std::max)(rowRight,hud.glyphs[i].right);
            }
            assert(std::abs(topRight-divider.right)<0.01f && std::abs(bottomRight-divider.right)<0.01f);
            for (std::size_t i=0;i<hud.barCount;++i) {
                const auto& b=hud.bars[i];
                assert(b.left>=0 && b.right<=1024 && b.top>=0 && b.bottom<=480 && b.right>b.left && b.bottom>b.top);
            }
        }
    }
    assert(!hudLayout(1,2,false,atlas,0,480).glyphCount);
    assert(!hudLayout(1,2,false,{0.5f,0.5f,0.45f},1024,480).glyphCount);

}
