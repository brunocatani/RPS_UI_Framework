#pragma once
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>

namespace rpsui::number_overlay
{
    inline constexpr unsigned kGlyphWidth=128, kGlyphHeight=192, kGlyphCount=10;
    struct Glyph { float left{},top{},right{},bottom{},u0{},u1{}; };
    struct Layout { std::array<Glyph,10> glyphs{}; std::size_t count{}; };
    inline Layout layout(std::uint32_t value,float width,float height) noexcept
    {
        Layout result;
        if (!std::isfinite(width) || !std::isfinite(height) || width<=0 || height<=0) return result;
        std::array<char,10> digits{};
        const auto converted=std::to_chars(digits.data(),digits.data()+digits.size(),value);
        if (converted.ec!=std::errc{}) return result;
        result.count=static_cast<std::size_t>(converted.ptr-digits.data());
        const float glyphHeight=(std::min)(height*0.85f,width*0.9f/static_cast<float>(result.count)*kGlyphHeight/kGlyphWidth);
        const float glyphWidth=glyphHeight*kGlyphWidth/kGlyphHeight;
        const float left=(width-glyphWidth*static_cast<float>(result.count))*0.5f, top=(height-glyphHeight)*0.5f;
        for (std::size_t i=0;i<result.count;++i) {
            const float digit=static_cast<float>(digits[i]-'0');
            const float x=left+static_cast<float>(i)*glyphWidth;
            result.glyphs[i]={x,top,x+glyphWidth,top+glyphHeight,digit/kGlyphCount,(digit+1)/kGlyphCount};
        }
        return result;
    }

    /*
     * The chamber cartridge: an upright round four times as tall as it is wide,
     * a pointed body over a separate base. Solid when chambered; a thin dim
     * outline when not. Drawn as quads sampling a solid cell, in panel pixels.
     */
    struct Rect { float left{},top{},right{},bottom{}; };
    struct Corner { float x{},y{}; };
    struct CartridgeQuad { std::array<Corner,4> corners{}; float alpha{1.0f}; };
    struct CartridgeLayout { std::array<CartridgeQuad,15> quads{}; std::size_t count{}; };
    inline constexpr float kCartridgeAspect=121.0f/30.0f;
    inline constexpr float kCartridgeFillAlpha=180.0f/255.0f, kCartridgeEmptyAlpha=0.38f;

    inline CartridgeLayout cartridgeLayout(const Rect& rect,bool chambered) noexcept
    {
        CartridgeLayout result;
        const float width=rect.right-rect.left, height=rect.bottom-rect.top;
        if (!std::isfinite(width) || !std::isfinite(height) || width<=0 || height<=0) return result;
        const auto at=[&](float x,float y) { return Corner{rect.left+x*width,rect.top+y*height}; };
        const auto add=[&](std::array<Corner,4> corners,float alpha) { result.quads[result.count++]={corners,alpha}; };
        // The body's outline from the base up over the tip, in fractions of the rect.
        constexpr std::array<std::array<float,2>,7> kBody{{{0.1f,0.909f},{0.1f,0.2975f},{0.2f,0.1405f},{0.5f,0.0f},
            {0.8f,0.1405f},{0.9f,0.2975f},{0.9f,0.909f}}};
        constexpr float kBaseTop=0.926f;
        const float stroke=width*(2.2f/30.0f), half=stroke*0.5f;
        const float outlineAlpha=chambered ? 1.0f : kCartridgeEmptyAlpha;
        const auto edge=[&](Corner from,Corner to) {
            const float dx=to.x-from.x, dy=to.y-from.y, length=std::sqrt(dx*dx+dy*dy);
            if (length<=0) return;
            const float nx=-dy/length*half, ny=dx/length*half;
            add({Corner{from.x+nx,from.y+ny},Corner{to.x+nx,to.y+ny},Corner{to.x-nx,to.y-ny},Corner{from.x-nx,from.y-ny}},outlineAlpha);
        };
        if (chambered) {
            add({at(0.1f,0.2975f),at(0.9f,0.2975f),at(0.9f,0.909f),at(0.1f,0.909f)},kCartridgeFillAlpha);
            add({at(0.1f,0.2975f),at(0.2f,0.1405f),at(0.8f,0.1405f),at(0.9f,0.2975f)},kCartridgeFillAlpha);
            add({at(0.2f,0.1405f),at(0.5f,0.0f),at(0.5f,0.0f),at(0.8f,0.1405f)},kCartridgeFillAlpha);
            add({at(0.0f,kBaseTop),at(1.0f,kBaseTop),at(1.0f,1.0f),at(0.0f,1.0f)},outlineAlpha);
        } else {
            edge(at(0.0f,kBaseTop),at(1.0f,kBaseTop));
            edge(at(1.0f,kBaseTop),at(1.0f,1.0f));
            edge(at(1.0f,1.0f),at(0.0f,1.0f));
            edge(at(0.0f,1.0f),at(0.0f,kBaseTop));
        }
        for (std::size_t i=0;i<kBody.size();++i) {
            const auto& from=kBody[i];
            const auto& to=kBody[(i+1)%kBody.size()];
            edge(at(from[0],from[1]),at(to[0],to[1]));
        }
        return result;
    }

    // The plain style beside a cartridge: the digits right aligned to a fixed
    // edge, so they keep their distance from it at any count.
    inline constexpr float kPlainChamberDigitEdge=0.70f, kPlainChamberLeft=0.75f, kPlainChamberTop=0.22f, kPlainChamberHeight=0.505f;
    struct PlainChamberLayout { Layout digits{}; Rect cartridge{}; };
    inline PlainChamberLayout plainChamberLayout(std::uint32_t value,float width,float height) noexcept
    {
        PlainChamberLayout result;
        result.digits=layout(value,width*kPlainChamberDigitEdge,height);
        if (!result.digits.count) return result;
        const float shift=width*kPlainChamberDigitEdge-result.digits.glyphs[result.digits.count-1].right;
        for (std::size_t i=0;i<result.digits.count;++i) {
            result.digits.glyphs[i].left+=shift;
            result.digits.glyphs[i].right+=shift;
        }
        const float cartridgeHeight=height*kPlainChamberHeight;
        const float left=width*kPlainChamberLeft, top=height*kPlainChamberTop;
        result.cartridge={left,top,left+cartridgeHeight/kCartridgeAspect,top+cartridgeHeight};
        return result;
    }

    /*
     * The Fallout HUD ammo counter (HUDMenu.swf, AmmoCount_mc): the loaded
     * count over a divider over the reserve count, both right aligned to the
     * divider's right end, in one condensed bold face. Proportions come from
     * that clip in units of the digit height h: the digits sit 0.3h from the
     * divider, the divider is 0.09h thick and 1.9h long. The bracketed variant
     * adds the HUD's bracket pair (BracketPairFadeHolder): a bar above with end
     * ticks pointing down and a bar below with end ticks pointing up.
     *
     * The atlas holds ten digit cells of kHudGlyphWidth x kHudGlyphHeight and
     * an eleventh solid cell that bars sample. Each glyph's quad covers only
     * the digit's ink rows [inkTop, inkBottom] and its advance, centered in its
     * cell, so h is the real digit height.
     */
    inline constexpr unsigned kHudGlyphWidth=128, kHudGlyphHeight=192, kHudCellCount=11;
    inline constexpr float kHudSolidU=(kHudCellCount-0.5f)/kHudCellCount;
    struct HudAtlasMetrics
    {
        float inkTop{};     // first ink row, 0..1 of the cell height
        float inkBottom{};  // one past the last ink row, 0..1
        float advance{};    // digit advance, 0..1 of the cell width
    };
    struct HudGlyph { float left{},top{},right{},bottom{},u0{},u1{},v0{},v1{}; };
    struct Bar { float left{},top{},right{},bottom{}; };
    struct HudLayout
    {
        std::array<HudGlyph,20> glyphs{};
        std::size_t glyphCount{};
        std::array<Bar,7> bars{};
        std::size_t barCount{};
        Rect cartridge{}; // Set only with chamber; the height of the two rows.
    };
    inline constexpr float kHudDigitGap=0.30f, kHudDividerThickness=0.09f, kHudDividerLength=1.9f;
    inline constexpr float kHudBracketPadX=0.35f, kHudBracketPadY=0.28f, kHudBracketTick=0.32f;
    inline constexpr float kHudFill=0.92f;
    inline constexpr float kHudChamberGap=0.35f;

    inline HudLayout hudLayout(std::uint32_t top,std::uint32_t bottom,bool bracketed,
        const HudAtlasMetrics& atlas,float width,float height,bool chamber=false) noexcept
    {
        HudLayout result;
        const float inkHeight=atlas.inkBottom-atlas.inkTop;
        if (!std::isfinite(width) || !std::isfinite(height) || width<=0 || height<=0 ||
            !std::isfinite(inkHeight) || inkHeight<=0 || atlas.inkTop<0 || atlas.inkBottom>1 ||
            !std::isfinite(atlas.advance) || atlas.advance<=0 || atlas.advance>1) return result;
        std::array<char,10> topDigits{}, bottomDigits{};
        const auto topEnd=std::to_chars(topDigits.data(),topDigits.data()+topDigits.size(),top);
        const auto bottomEnd=std::to_chars(bottomDigits.data(),bottomDigits.data()+bottomDigits.size(),bottom);
        if (topEnd.ec!=std::errc{} || bottomEnd.ec!=std::errc{}) return result;
        const auto topCount=static_cast<std::size_t>(topEnd.ptr-topDigits.data());
        const auto bottomCount=static_cast<std::size_t>(bottomEnd.ptr-bottomDigits.data());

        // Everything below is in units of the digit height until scaled.
        const float advance=atlas.advance*kHudGlyphWidth/(inkHeight*kHudGlyphHeight);
        const float blockWidth=(std::max)({advance*static_cast<float>(topCount),advance*static_cast<float>(bottomCount),kHudDividerLength});
        const float blockHeight=2.0f+2.0f*kHudDigitGap;
        const float padX=bracketed ? kHudBracketPadX : 0.0f;
        const float padY=bracketed ? kHudBracketPadY+kHudDividerThickness : 0.0f;
        const float cartridgeWidth=chamber ? blockHeight/kCartridgeAspect : 0.0f;
        const float chamberBlock=chamber ? kHudChamberGap+cartridgeWidth : 0.0f;
        const float contentWidth=blockWidth+chamberBlock+2.0f*padX, contentHeight=blockHeight+2.0f*padY;
        const float h=(std::min)(height*kHudFill/contentHeight,width*kHudFill/contentWidth);
        const float originX=(width-contentWidth*h)*0.5f, originY=(height-contentHeight*h)*0.5f;
        const float right=originX+(padX+blockWidth)*h;
        const float blockTop=originY+padY*h;

        const float u=(1.0f-atlas.advance)*0.5f;
        const auto row=[&](const std::array<char,10>& digits,std::size_t count,float y) {
            for (std::size_t i=0;i<count;++i) {
                const float digit=static_cast<float>(digits[i]-'0');
                const float x=right-static_cast<float>(count-i)*advance*h;
                result.glyphs[result.glyphCount++]={x,y,x+advance*h,y+h,
                    (digit+u)/kHudCellCount,(digit+1.0f-u)/kHudCellCount,atlas.inkTop,atlas.inkBottom};
            }
        };
        row(topDigits,topCount,blockTop);
        row(bottomDigits,bottomCount,blockTop+(1.0f+2.0f*kHudDigitGap)*h);
        const float dividerCenter=blockTop+(1.0f+kHudDigitGap)*h;
        result.bars[result.barCount++]={right-kHudDividerLength*h,dividerCenter-kHudDividerThickness*h*0.5f,
            right,dividerCenter+kHudDividerThickness*h*0.5f};
        if (chamber) {
            const float left=right+kHudChamberGap*h;
            result.cartridge={left,blockTop,left+cartridgeWidth*h,blockTop+blockHeight*h};
        }
        if (bracketed) {
            const float left=originX, outerRight=originX+contentWidth*h;
            const float upper=originY, lower=originY+contentHeight*h;
            const float stroke=kHudDividerThickness*h, tick=kHudBracketTick*h;
            result.bars[result.barCount++]={left,upper,outerRight,upper+stroke};
            result.bars[result.barCount++]={left,upper,left+stroke,upper+tick};
            result.bars[result.barCount++]={outerRight-stroke,upper,outerRight,upper+tick};
            result.bars[result.barCount++]={left,lower-stroke,outerRight,lower};
            result.bars[result.barCount++]={left,lower-tick,left+stroke,lower};
            result.bars[result.barCount++]={outerRight-stroke,lower-tick,outerRight,lower};
        }
        return result;
    }
}
