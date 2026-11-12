#pragma once
#include "turn_cue.hpp"
namespace roadnav {
// Use the exact scene view/projection, including camera roll and drunk shake.
// Script GET_CAM_ROT/FOV is the authored camera, not necessarily the rendered one.
struct RoadProjection {
    float view[4][4]{},projection[4][4]{};bool valid=false;float nearClip=0,farClip=0;
    bool usable()const{
        for(const auto* matrix:{view,projection})for(int i=0;i<4;++i)for(int j=0;j<4;++j)if(!std::isfinite(matrix[i][j]))return false;
        for(int axis=0;axis<3;++axis){float length=0;for(int i=0;i<3;++i)length+=view[i][axis]*view[i][axis];if(std::fabs(length-1)>.05f)return false;}
        return std::fabs(view[3][3]-1)<.001f&&std::fabs(projection[3][3])<.001f&&std::fabs(projection[2][3])>.9f&&std::fabs(projection[2][3])<1.1f&&projection[0][0]>.1f&&projection[1][1]>.1f;
    }
    bool matchesCamera(const CameraFrame& camera)const{
        if(!usable())return false;
        Vec3 eye{};float* axes[]={&eye.x,&eye.y,&eye.z};
        for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)*axes[i]-=view[3][j]*view[i][j];
        if(distance2D(eye,camera.position)>10||std::fabs(eye.z-camera.position.z)>10)return false;
        constexpr float radians=.01745329252f;const float pitch=camera.rotation.x*radians,yaw=camera.rotation.z*radians;
        const Vec3 forward{-std::sin(yaw)*std::cos(pitch),std::cos(yaw)*std::cos(pitch),std::sin(pitch)};
        const float sign=projection[2][3]>0?1.0f:-1.0f;
        return sign*(forward.x*view[0][2]+forward.y*view[1][2]+forward.z*view[2][2])>.75f;
    }
    bool clipPlanes(float& nearPlane,float& farPlane)const{
        if(!valid||!std::isfinite(nearClip)||!std::isfinite(farClip)||nearClip<.001f||nearClip>5||farClip<100||farClip>20000||farClip<=nearClip)return false;
        nearPlane=nearClip;farPlane=farClip;return true;
    }
    bool projectPoint(Vec3 p,float width,float height,ScreenPoint& screen,float& depth)const{
        float v[4]{},clip[4]{};const float world[]{p.x,p.y,p.z,1};
        for(int j=0;j<4;++j)for(int i=0;i<4;++i)v[j]+=world[i]*view[i][j];
        for(int j=0;j<4;++j)for(int i=0;i<4;++i)clip[j]+=v[i]*projection[i][j];
        depth=clip[3];if(!std::isfinite(depth)||depth<=.01f)return false;
        screen.x=(clip[0]/depth+1)*width*.5f;screen.y=(1-clip[1]/depth)*height*.5f;
        return std::isfinite(screen.x)&&std::isfinite(screen.y);
    }
};
}
