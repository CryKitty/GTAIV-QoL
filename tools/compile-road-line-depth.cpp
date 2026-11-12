#include <windows.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
int main(){
    const char* source=R"(
sampler2D sceneDepth : register(s0);
float4 camera : register(c0); // near, far, 1/width, 1/height
float4 main(float4 color:COLOR0,float2 route:TEXCOORD0,float2 pixel:VPOS):COLOR0 {
    float logDepth=tex2D(sceneDepth,(pixel+.5)*camera.zw).r;
    float visibleDepth=pow(camera.y/camera.x,saturate(logDepth))*camera.x;
    float bias=.08+route.x*.0004;
    float feather=.12+route.x*.0006;
    float coverage=saturate((visibleDepth+bias-route.x)/feather);
    clip(coverage-.001);
    return float4(color.rgb,color.a*coverage);
})";
    ID3DBlob* shader=nullptr;ID3DBlob* errors=nullptr;
    const HRESULT result=D3DCompile(source,std::strlen(source),"road line depth",nullptr,nullptr,"main","ps_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&shader,&errors);
    if(FAILED(result)){if(errors){std::fwrite(errors->GetBufferPointer(),1,errors->GetBufferSize(),stderr);errors->Release();}return 1;}
    FILE* file=nullptr;if(fopen_s(&file,"vendor/roadnav/src/road_line_depth_shader.hpp","w")||!file){shader->Release();return 2;}
    std::fprintf(file,"#pragma once\n// Generated at build time; no runtime shader compiler.\nconstexpr DWORD roadLineDepthShader[]={\n");
    const auto words=static_cast<const DWORD*>(shader->GetBufferPointer());
    for(SIZE_T i=0;i<shader->GetBufferSize()/sizeof(DWORD);++i)std::fprintf(file,"0x%08lXu,%s",words[i],i%8==7?"\n":"");
    std::fprintf(file,"\n};\n");std::fclose(file);shader->Release();if(errors)errors->Release();return 0;
}
