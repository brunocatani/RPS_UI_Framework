#pragma once
#include <imgui.h>
#include <cfloat>

namespace rpsui::settings_menu {
// Size at which wrapped text fits its box, stepping down from `largest` so a long
// name stays readable in full. Text still too tall at `smallest` is left to the
// caller's clip rect.
inline float fitWrappedSize(ImFont* font,const char* text,float wrap,float height,float largest,float smallest) {
 float size=largest;
 while(size>smallest && font->CalcTextSizeA(size,FLT_MAX,wrap,text).y>height)size-=1.f;
 return size<smallest?smallest:size;
}
}
