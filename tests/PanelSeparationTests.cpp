#include "PanelSeparationPolicy.h"
#include <iostream>
#include <stdexcept>
void check(bool value){if(!value)throw std::runtime_error("Panel separation assertion failed");}
int main(){try {
 using namespace rpsui;
 sdk::PanelPoseV1 wheel,config;wheel.physicalWidth=95;wheel.physicalHeight=95;
 config.physicalWidth=96;config.physicalHeight=60;
 config.center[0]=99.5f;
 check(panel_separation::haveInPlaneGap(wheel,config)); // Four-unit edge gap, previously shifted again.
 config.center[0]=-99.5f;check(panel_separation::haveInPlaneGap(wheel,config));
 config.center[0]=0;check(!panel_separation::haveInPlaneGap(wheel,config));
 config.center[0]=90;check(!panel_separation::haveInPlaneGap(wheel,config));
 config.center[0]=0;config.center[2]=82;check(panel_separation::haveInPlaneGap(wheel,config));
 config.center[2]=0;config.physicalWidth=180;config.physicalHeight=112.5f;
 config.center[0]=141.5f;check(panel_separation::haveInPlaneGap(wheel,config));
 // Same side-by-side placement with the panels rotated in the game world.
 wheel.right[0]=config.right[0]=0;wheel.right[1]=config.right[1]=1;
 wheel.front[0]=config.front[0]=1;wheel.front[1]=config.front[1]=0;
 config.center[0]=0;config.center[1]=141.5f;check(panel_separation::haveInPlaneGap(wheel,config));
 std::cout<<"Sidecar gaps preserved; genuine overlap remains eligible for separation\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
