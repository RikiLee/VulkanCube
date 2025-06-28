#include "ShaderCompiler.hpp"

#include <spdlog/spdlog.h>
#include <fstream>
#include <stdexcept>

// ---- glslang includes ----
#include <glslang/SPIRV/GlslangToSpv.h>
#include <glslang/Public/ResourceLimits.h>

// 辅助函数：从文件读取内容
static std::string readFile(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filepath);
    }
    size_t fileSize = (size_t)file.tellg();
    std::string buffer(fileSize, ' ');
    file.seekg(0);
    file.read(&buffer[0], fileSize);
    return buffer;
}

static std::vector<uint32_t> readSPVFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("failed to open file!");
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);

    file.close();

    return buffer;
}

// 辅助函数：将 SPIR-V 字节码写入文件
static void writeFile(const std::string& filepath, const std::vector<uint32_t>& spirv) {
    spdlog::debug("Writing SPIR-V to {}", filepath);
    std::ofstream file(filepath, std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file for writing: " + filepath);
    }
    file.write(reinterpret_cast<const char*>(spirv.data()), spirv.size() * sizeof(uint32_t));
    file.close();
}

// 将 glslang 的着色器类型转换为 EShLanguage
EShLanguage ShaderCompiler::getShaderStage(const std::string& filename) {
    if (filename.find(".vert") != std::string::npos) return EShLangVertex;
    if (filename.find(".frag") != std::string::npos) return EShLangFragment;
    if (filename.find(".comp") != std::string::npos) return EShLangCompute;
    if (filename.find(".geom") != std::string::npos) return EShLangGeometry;
    // ... 添加其他着色器类型
    throw std::runtime_error("Unsupported shader type: " + filename);
}

std::vector<uint32_t> ShaderCompiler::compileGLSL(const std::filesystem::path& file, EShLanguage stage) const
{
    std::string saveFile = file.string() + ".spv";
    if (std::filesystem::exists(saveFile)) {
        spdlog::debug("{} already compiled to {}", file.string(), saveFile);
        return readSPVFile(saveFile);
    }

    spdlog::debug("Compiling GLSL: {}...", file.string());
    std::string source = readFile(file.string());
    const char* sourceCStr = source.c_str();

    glslang::TShader shader(stage);
    shader.setStrings(&sourceCStr, 1);

    glslang::EShTargetClientVersion vulkanClientVersion = glslang::EShTargetVulkan_1_3;
    glslang::EShTargetLanguageVersion targetVersion = glslang::EShTargetSpv_1_0;

    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, m_vulkanAPIVersion);
    shader.setEnvClient(glslang::EShClientVulkan, vulkanClientVersion);
    shader.setEnvTarget(glslang::EShTargetSpv, targetVersion);

    TBuiltInResource resources = *GetDefaultResources();
    EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);

    if (!shader.parse(&resources, 100, false, messages)) {
        spdlog::error("GLSL parsing failed for {}:", file.string());
        spdlog::error("{}", shader.getInfoLog());
        spdlog::error("{}", shader.getInfoDebugLog());
        throw std::runtime_error("GLSL compilation error.");
    }

    glslang::TProgram program;
    program.addShader(&shader);

    if (!program.link(messages)) {
        spdlog::error("GLSL linking failed for {}:", file.string());
        spdlog::error("{}", program.getInfoLog());
        spdlog::error("{}", program.getInfoDebugLog());
        throw std::runtime_error("GLSL linking error.");
    }

    std::vector<uint32_t> spirv;
    glslang::GlslangToSpv(*program.getIntermediate(stage), spirv);

    writeFile(saveFile, spirv);
    return spirv;
}

std::vector<uint32_t> ShaderCompiler::compileGLSL(const std::string& source, EShLanguage stage) const
{
    spdlog::debug("Compiling GLSL: {}", source);
    const char* sourceCStr = source.c_str();

    glslang::TShader shader(stage);
    shader.setStrings(&sourceCStr, 1);

    glslang::EShTargetClientVersion vulkanClientVersion = glslang::EShTargetVulkan_1_3;
    glslang::EShTargetLanguageVersion targetVersion = glslang::EShTargetSpv_1_5;

    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, m_vulkanAPIVersion);
    shader.setEnvClient(glslang::EShClientVulkan, vulkanClientVersion);
    shader.setEnvTarget(glslang::EShTargetSpv, targetVersion);

    TBuiltInResource resources = *GetDefaultResources();
    EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);

    if (!shader.parse(&resources, 100, false, messages)) {
        spdlog::error("GLSL parsing failed:");
        spdlog::error("{}", shader.getInfoLog());
        spdlog::error("{}", shader.getInfoDebugLog());
        throw std::runtime_error("GLSL compilation error.");
    }

    glslang::TProgram program;
    program.addShader(&shader);

    if (!program.link(messages)) {
        spdlog::error("GLSL linking failed:");
        spdlog::error("{}", program.getInfoLog());
        spdlog::error("{}", program.getInfoDebugLog());
        throw std::runtime_error("GLSL linking error.");
    }

    std::vector<uint32_t> spirv;
    glslang::GlslangToSpv(*program.getIntermediate(stage), spirv);
    return spirv;
}

ShaderCompiler::ShaderCompiler()
{
    // 必须在使用 glslang API 前初始化
    glslang::InitializeProcess();
}

ShaderCompiler::~ShaderCompiler()
{
    // 在程序结束时清理
    glslang::FinalizeProcess();
}

/*
// ---- DXC includes ----
#include <minwindef.h>
#include <directx-dxc/dxcapi.h>
#include <wrl/client.h> // For ComPtr

// 使用 DXC 编译 HLSL
std::vector<uint32_t> compileHLSL(const std::string& filepath) {
    spdlog::debug("Compiling HLSL: {}...", filepath);

    using Microsoft::WRL::ComPtr;

    ComPtr<IDxcUtils> pUtils;
    ComPtr<IDxcCompiler3> pCompiler;
    DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&pUtils));
    DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&pCompiler));

    std::string source = readFile(filepath);

    // 从源码创建 Blob
    ComPtr<IDxcBlobEncoding> pSource;
    pUtils->CreateBlob(source.c_str(), source.length(), CP_UTF8, &pSource);

    // 编译参数
    std::vector<LPCWSTR> arguments;
    arguments.push_back(L"-E"); arguments.push_back(L"main"); // 入口点
    arguments.push_back(L"-T"); arguments.push_back(L"ps_6_0"); // 目标: Pixel Shader 6.0
    arguments.push_back(L"-spirv"); // 输出 SPIR-V
    arguments.push_back(L"-fspv-target-env=vulkan1.2"); // 目标环境 Vulkan 1.2
    arguments.push_back(L"-O3"); // 优化级别

    DxcBuffer sourceBuffer;
    sourceBuffer.Ptr = pSource->GetBufferPointer();
    sourceBuffer.Size = pSource->GetBufferSize();
    sourceBuffer.Encoding = DXC_CP_ACP;

    ComPtr<IDxcResult> pResults;
    pCompiler->Compile(&sourceBuffer, arguments.data(), arguments.size(), nullptr, IID_PPV_ARGS(&pResults));

    ComPtr<IDxcBlobUtf8> pErrors = nullptr;
    pResults->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&pErrors), nullptr);
    if (pErrors != nullptr && pErrors->GetStringLength() != 0) {
        spdlog::error("HLSL compilation warnings/errors for {}:", filepath);
        spdlog::error("{}", pErrors->GetStringPointer());
    }

    HRESULT hrStatus;
    pResults->GetStatus(&hrStatus);
    if (FAILED(hrStatus)) {
        throw std::runtime_error("HLSL compilation failed.");
    }

    ComPtr<IDxcBlob> pSpirv;
    pResults->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&pSpirv), nullptr);

    const uint32_t* spirv_data = reinterpret_cast<const uint32_t*>(pSpirv->GetBufferPointer());
    size_t spirv_size = pSpirv->GetBufferSize() / sizeof(uint32_t);
    return std::vector<uint32_t>(spirv_data, spirv_data + spirv_size);
}
*/