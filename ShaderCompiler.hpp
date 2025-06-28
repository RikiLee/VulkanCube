#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>

#include <glslang/Public/ShaderLang.h>

class ShaderCompiler {
public:
    static ShaderCompiler& getInstance() {
        static ShaderCompiler s_shaderCompilerInstance;
        return s_shaderCompilerInstance;
    }

    std::vector<uint32_t> compileGLSL(const std::filesystem::path& file, EShLanguage stage) const;
    std::vector<uint32_t> compileGLSL(const std::string& source, EShLanguage stage) const;

    //std::vector<uint32_t> compile_hlsl(const std::filesystem::path& file, EShLanguage stage);
    //std::vector<uint32_t> compile_hlsl(const std::string& source, EShLanguage stage);
    static EShLanguage getShaderStage(const std::string& filename);
private:
    ShaderCompiler();
    ShaderCompiler(const ShaderCompiler&) = delete;
    ShaderCompiler& operator=(const ShaderCompiler&) = delete;
    ~ShaderCompiler();
    int m_vulkanAPIVersion = 130;
};

