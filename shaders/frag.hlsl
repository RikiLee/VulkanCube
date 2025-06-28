// 定义着色器模型版本
#pragma shader_model 6_0

// 输入结构体
struct PS_INPUT {
    float3 fragColor : COLOR0;  // 输入颜色
};

// 像素着色器主函数
float4 main(PS_INPUT input) : SV_TARGET {
    // 输出颜色，添加透明度
    return float4(input.fragColor, 1.0);
}
