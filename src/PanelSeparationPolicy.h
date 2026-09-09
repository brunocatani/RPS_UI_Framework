#pragma once
#include "RPSUIFrameworkApi.h"
#include <cmath>

namespace rpsui::panel_separation {
// A separating projection proves the rectangles do not overlap. Center-distance
// padding alone incorrectly moves an explicitly positioned sidecar a second time.
inline bool haveInPlaneGap(const sdk::PanelPoseV1& a,const sdk::PanelPoseV1& b) noexcept {
 const auto dot=[](const float* x,const float* y){return x[0]*y[0]+x[1]*y[1]+x[2]*y[2];};
 const float delta[]{a.center[0]-b.center[0],a.center[1]-b.center[1],a.center[2]-b.center[2]};
 const float* axes[]{a.right,a.up,b.right,b.up};
 for(const auto* axis:axes) {
  const float extentA=(std::fabs(dot(a.right,axis))*a.physicalWidth+std::fabs(dot(a.up,axis))*a.physicalHeight)*0.5f;
  const float extentB=(std::fabs(dot(b.right,axis))*b.physicalWidth+std::fabs(dot(b.up,axis))*b.physicalHeight)*0.5f;
  if(std::fabs(dot(delta,axis))>extentA+extentB)return true;
 }
 return false;
}
}
