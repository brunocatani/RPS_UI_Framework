#pragma once

#include "RPSUIInputApi.h"
#include <cmath>
#include <array>

namespace rpsui::input_policy {
inline bool validPointerAim(float pitchDegrees,float yawDegrees) noexcept {
 return std::isfinite(pitchDegrees) && std::isfinite(yawDegrees) && std::fabs(pitchDegrees)<=180 && std::fabs(yawDegrees)<=90;
}
// NiTransform's contiguous vectors are local axes in world space. FO4VR's
// compose helper (1401A8D60), called by NiAVObject world update (141C23740),
// weights those vectors by local X/Y/Z. VR conversion at 141BAB210 maps
// OpenVR -Z to native +Y; wand update 140C612E0 preserves that basis.
inline std::array<float,3> pointerAimDirection(float pitchDegrees,float yawDegrees) noexcept {
 constexpr float radians=3.14159265358979323846f/180.f;
 const float pitch=pitchDegrees*radians,yaw=yawDegrees*radians;
 return {std::sin(yaw)*std::cos(pitch),std::cos(yaw)*std::cos(pitch),std::sin(pitch)};
}
template<class Transform>
bool nativeWandPose(const Transform& transform, sdk::HandInputV1& hand,const std::array<float,3>& aim={0,1,0}) noexcept {
 if(!std::isfinite(transform.scale) || transform.scale<=0 || transform.scale>100)return false;
 for(unsigned axis=0;axis<3;++axis) {
  hand.position[axis]=transform.translate[axis];
  hand.forward[axis]=transform.rotate[0][axis]*aim[0]+transform.rotate[1][axis]*aim[1]+transform.rotate[2][axis]*aim[2];
  if(!std::isfinite(hand.position[axis]) || std::fabs(hand.position[axis])>1.e8f || !std::isfinite(hand.forward[axis]))return false;
 }
 const float length=std::hypot(hand.forward[0],hand.forward[1],hand.forward[2]);
 if(length<.01f || length>100)return false;
 for(float& component:hand.forward)component/=length;
 return true;
}
}
