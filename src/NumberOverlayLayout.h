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
}
