// 定义着色器模型版本
#pragma shader_model 6_0

// 输入结构体
struct VS_INPUT {
    float3 inPosition : POSITION0;  // 顶点位置
    float3 inColor : COLOR0;        // 顶点颜色
};

// 输出结构体
struct VS_OUTPUT {
    float4 gl_Position : SV_POSITION;  // 裁剪空间位置
    float3 fragColor : COLOR0;         // 输出颜色
};

// 统一缓冲区对象
cbuffer UniformBufferObject : register(b0) {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};

// 顶点着色器主函数
VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    // 计算裁剪空间位置
    output.gl_Position = mul(mul(proj, view), mul(model, float4(input.inPosition, 1.0)));
    // 传递顶点颜色
    output.fragColor = input.inColor;
    return output;
}
