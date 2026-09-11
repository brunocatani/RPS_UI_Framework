#pragma once

#include "RPSUIInputApi.h"
#include <cmath>

namespace rpsui::input_policy {
// NiTransform's contiguous vectors are local axes in world space. FO4VR's
// compose helper (1401A8D60), called by NiAVObject world update (141C23740),
// weights those vectors by local X/Y/Z. The wand points along local +Y.
template<class Transform>
bool nativeWandPose(const Transform& transform, sdk::HandInputV1& hand) noexcept {
 if(!std::isfinite(transform.scale) || transform.scale<=0 || transform.scale>100)return false;
 for(unsigned axis=0;axis<3;++axis) {
  hand.position[axis]=transform.translate[axis];
  hand.forward[axis]=transform.rotate[1][axis];
  if(!std::isfinite(hand.position[axis]) || std::fabs(hand.position[axis])>1.e8f || !std::isfinite(hand.forward[axis]))return false;
 }
 const float length=std::hypot(hand.forward[0],hand.forward[1],hand.forward[2]);
 if(length<.01f || length>100)return false;
 for(float& component:hand.forward)component/=length;
 return true;
}
}
