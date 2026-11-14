#pragma once
#include <cmath>
#include <cstdint>
namespace qolcamera {
struct Matrix {float v[16]{};};
struct RenderRequest {
    std::uint32_t at=0;unsigned cm=0,vehicle=0,child=0;
    bool driving=false,allowed=false;float player[3]{};
};
struct MatrixSample {Matrix before{},after{};RenderRequest request{};std::uint32_t at=0;unsigned side=0;bool written=false;std::uintptr_t destination=0;};
inline bool usableMatrix(const Matrix& m){
    for(float f:m.v)if(!std::isfinite(f))return false;
    for(unsigned axis=0;axis<3;++axis){const auto i=axis*4;const float n=m.v[i]*m.v[i]+m.v[i+1]*m.v[i+1]+m.v[i+2]*m.v[i+2];if(std::fabs(n-1)>.1f)return false;}
    return std::fabs(m.v[12])<20000&&std::fabs(m.v[13])<20000&&std::fabs(m.v[14])<5000;
}
inline bool prepareHeight(Matrix& matrix,const RenderRequest& request,std::uint32_t now){
    if(!request.driving||!request.allowed||now-request.at>200||!request.cm||request.cm>120||!usableMatrix(matrix))return false;
    for(float f:request.player)if(!std::isfinite(f))return false;
    const float x=matrix.v[12]-request.player[0],y=matrix.v[13]-request.player[1],z=matrix.v[14]-request.player[2];
    if(x*x+y*y>625||std::fabs(z)>20)return false;
    matrix.v[14]+=static_cast<float>(request.cm)*.01f;return true;
}
inline Matrix copyBaseline(Matrix current,const MatrixSample& previous,std::uintptr_t source){
    if(source!=previous.destination||!source)return current;
    bool same=true;for(unsigned i=0;i<16;++i)if(std::fabs(current.v[i]-previous.after.v[i])>.00001f)same=false;
    if(same)current.v[14]-=previous.after.v[14]-previous.before.v[14];
    return current;
}
struct RenderEvidence {float requested=0,copyDelta=0,renderDelta=0,error=0;bool paired=false,verified=false;};
inline RenderEvidence renderedEvidence(const MatrixSample& sample,const float* eye,std::uint32_t now,bool valid){
    RenderEvidence e;e.requested=sample.request.cm*.01f;
    e.copyDelta=sample.after.v[14]-sample.before.v[14];
    if(!valid||!sample.request.driving||now-sample.at>100||!usableMatrix(sample.before)||!usableMatrix(sample.after))return e;
    for(unsigned i=0;i<3;++i)if(!std::isfinite(eye[i]))return e;
    const float x=eye[0]-sample.after.v[12],y=eye[1]-sample.after.v[13];e.error=eye[2]-sample.after.v[14];
    e.paired=x*x+y*y<.0625f&&std::fabs(e.error)<.1f;
    e.renderDelta=eye[2]-sample.before.v[14];
    e.verified=e.paired&&std::fabs(e.copyDelta-e.requested)<.01f&&std::fabs(e.renderDelta-e.requested)<.1f;
    return e;
}
}
