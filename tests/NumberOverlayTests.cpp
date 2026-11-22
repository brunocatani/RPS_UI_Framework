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

    // The chamber cartridge stays inside the panel and clear of the digits, at
    // any count, in all three styles.
    using rpsui::number_overlay::cartridgeLayout;
    using rpsui::number_overlay::plainChamberLayout;
    const auto inPanel=[](const rpsui::number_overlay::Rect& r) {
        assert(r.left>=0 && r.right<=1024 && r.top>=0 && r.bottom<=480 && r.right>r.left && r.bottom>r.top);
    };
    for (const std::uint32_t value:{0u,7u,30u,240u}) {
        const auto plain=plainChamberLayout(value,1024,480);
        assert(plain.digits.count>0);
        inPanel(plain.cartridge);
        for (std::size_t i=0;i<plain.digits.count;++i) {
            assert(plain.digits.glyphs[i].left>=0 && plain.digits.glyphs[i].right<=plain.cartridge.left);
        }
        for (const bool bracketed:{false,true}) {
            const auto hud=hudLayout(value,value,bracketed,atlas,1024,480,true);
            inPanel(hud.cartridge);
            for (std::size_t i=0;i<hud.glyphCount;++i) assert(hud.glyphs[i].right<=hud.cartridge.left);
            for (std::size_t i=0;i<hud.barCount;++i) {
                const auto& b=hud.bars[i];
                assert(b.left>=0 && b.right<=1024 && b.top>=0 && b.bottom<=480);
                // The brackets surround it; the divider stops short of it.
                if (i==0) assert(b.right<=hud.cartridge.left);
            }
            const auto without=hudLayout(value,value,bracketed,atlas,1024,480);
            assert(without.cartridge.right==0);
        }
    }
    for (const bool chambered:{false,true}) {
        const rpsui::number_overlay::Rect rect{100,100,130,221};
        const auto cartridge=cartridgeLayout(rect,chambered);
        assert(cartridge.count>0 && cartridge.count<=cartridge.quads.size());
        for (std::size_t i=0;i<cartridge.count;++i) {
            for (const auto& c:cartridge.quads[i].corners) {
                assert(c.x>=rect.left-2 && c.x<=rect.right+2 && c.y>=rect.top-2 && c.y<=rect.bottom+2);
            }
            assert(chambered || cartridge.quads[i].alpha==rpsui::number_overlay::kCartridgeEmptyAlpha);
        }
    }
    assert(!cartridgeLayout({0,0,0,0},true).count);

    // A label shares the chamber column: the cartridge beside the first row,
    // the label beside the second on its baseline. The numbers keep the size
    // they have without a label, and a long label shrinks alone to fit.
    using rpsui::number_overlay::LabelMetrics;
    const auto textEnd=[](const rpsui::number_overlay::Rect& r,const LabelMetrics& m) {
        const float scale=(r.right-r.left)/m.width;
        return r.left+(m.left+m.advance)*scale;
    };
    const auto textStart=[](const rpsui::number_overlay::Rect& r,const LabelMetrics& m) {
        return r.left+m.left*(r.right-r.left)/m.width;
    };
    const auto baseline=[](const rpsui::number_overlay::Rect& r,const LabelMetrics& m) {
        return r.top+m.digitBottom*(r.bottom-r.top)/m.height;
    };
    for (const float advance:{300.0f,812.0f,6000.0f}) {
        const LabelMetrics label{advance+88,214,44,advance,40,166};
        for (const bool bracketed:{false,true}) {
            for (const bool chamber:{false,true}) {
                const auto plain=hudLayout(30,240,bracketed,atlas,1024,480,chamber);
                const auto hud=hudLayout(30,240,bracketed,atlas,1024,480,chamber,&label);
                assert(hud.glyphCount==plain.glyphCount);
                const float digit=hud.glyphs[0].bottom-hud.glyphs[0].top;
                assert(std::abs(digit-(plain.glyphs[0].bottom-plain.glyphs[0].top))<0.01f);
                const auto& divider=hud.bars[0];
                const auto& last=hud.glyphs[hud.glyphCount-1];
                assert(hud.label.right>hud.label.left && hud.label.bottom>hud.label.top);
                assert(textStart(hud.label,label)>=divider.right && textEnd(hud.label,label)<=1024.01f);
                assert(std::abs(baseline(hud.label,label)-last.bottom)<0.01f);
                const float labelDigit=(label.digitBottom-label.digitTop)*(hud.label.bottom-hud.label.top)/label.height;
                assert(labelDigit<=digit*rpsui::api::number_overlay::kLabelScale+0.01f);
                if (advance<500) assert(std::abs(labelDigit-digit*rpsui::api::number_overlay::kLabelScale)<0.01f);
                if (chamber) {
                    inPanel(hud.cartridge);
                    assert(hud.cartridge.bottom<=divider.top && hud.cartridge.left>=divider.right);
                }
                if (bracketed) assert(hud.bars[3].right>=textEnd(hud.label,label)-0.01f);
            }
        }
        using rpsui::number_overlay::plainLabelLayout;
        constexpr rpsui::number_overlay::InkRows ink{0.27f,0.81f};
        for (const bool chamber:{false,true}) {
            const auto plain=plainLabelLayout(120,chamber,ink,label,1024,480);
            assert(plain.digits.count==3);
            const auto& last=plain.digits.glyphs[2];
            assert(std::abs((last.bottom-last.top)-480*rpsui::api::number_overlay::kPlainLabelNumberFill)<0.01f);
            const float after=chamber ? plain.cartridge.right : last.right;
            if (chamber) inPanel(plain.cartridge), assert(plain.cartridge.left>=last.right);
            assert(textStart(plain.label,label)>=after && textEnd(plain.label,label)<=1024.01f);
            assert(std::abs(baseline(plain.label,label)-(last.top+ink.bottom*(last.bottom-last.top)))<0.01f);
        }
    }
    assert(!rpsui::number_overlay::plainLabelLayout(1,false,{0.27f,0.81f},{},1024,480).digits.count);
}
