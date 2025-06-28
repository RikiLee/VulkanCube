#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <array>
#include <optional>
#include <set>
#include <unordered_map>
#include <queue>

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() const noexcept {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

struct UniformBufferObject {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 projView;
};

class VulkanCube {
private:
    struct Animation {
        std::vector<uint32_t> faceIds;
        glm::vec3 rotateAxis = glm::vec3(0.f);
        glm::vec3 rotateCenter = glm::vec3(0.f);
        bool clockWise = false;
        bool interactive = false;

        static constexpr glm::vec3 xAxis = glm::vec3(1.f, 0.f, 0.f);
        static constexpr glm::vec3 yAxis = glm::vec3(0.f, 1.f, 0.f);
        static constexpr glm::vec3 zAxis = glm::vec3(0.f, 0.f, 1.f);
    };

    enum class Direction {
        Left = 0,
        Right = 1,
        Top = 2,
        Bottom = 3,
        Front = 4,
        Back = 5
    };

    struct FaceInfo {
        size_t left = 6;
        size_t right = 6;
        size_t top = 6;
        size_t bottom = 6;
        std::array<std::array<size_t, 2>, 4> edges{};
    };

public:
    explicit VulkanCube();
    VulkanCube(const VulkanCube&) = delete;
    VulkanCube& operator=(const VulkanCube&) = delete;
    ~VulkanCube();

    void run();

private:
    UniformBufferObject ubo{};
    std::chrono::high_resolution_clock::time_point startTime;
    std::chrono::high_resolution_clock::time_point rotateStartTime;
    std::chrono::high_resolution_clock::time_point currentTime;
    size_t periodTimeMS = 0;

    static const size_t s_msCount = 3000;
    std::queue<Animation> animationQueues;
    bool moving = false;
    bool is2D = true;
    bool rotating = false;
    bool isPaused = false;
    bool previousWindowMinimizedStatus = false;

    bool interactive = false;
    float scale = 1.f;
    size_t clickTime = 0;
    std::array<size_t, 2> selectedFace;

    void processAnimation();

    enum class TranslateType {
        Reset = 0,
        Flod = 1,
        Open = 2,
    };

    void resetFaceToCenter(TranslateType mask);

    bool readyToAddAnimation() const noexcept { return animationQueues.empty(); }
    void addCubeAnimation();
    std::array<FaceInfo, 6> getAdjacentInfo() const noexcept;
    std::array<std::array<size_t, 2>, 4> getEdges(size_t faceId) const noexcept;

    struct AdjacentInfo {
        size_t faceId;
        size_t AdjacentId;
        Direction direction;
    };

    std::vector<AdjacentInfo> getAllAdjacentFaces(const std::array<FaceInfo, 6>& faceInfos, size_t faceID, Direction direction) const noexcept;
    std::array<uint32_t, 6> getDirectionFaceIds() const noexcept;

    bool getFaceID(double x, double y, size_t& id) const noexcept;
    bool addRotateAnimation();
    std::vector<uint32_t> getFaceInHalfPlane(float v, bool greater, bool isX) const noexcept;
    bool connectAlongAxis(const std::array<FaceInfo, 6>& faceInfos, size_t firstId, Direction direction) const noexcept;

private:
    GLFWwindow* window;

    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkSurfaceKHR surface;

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    VkDevice device;

    VkQueue graphicsQueue;
    VkQueue presentQueue;

    VkSwapchainKHR swapChain;
    std::vector<VkImage> swapChainImages;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    std::vector<VkImageView> swapChainImageViews;
    std::vector<VkFramebuffer> swapChainFramebuffers;

    VkRenderPass renderPass;
    VkDescriptorSetLayout descriptorSetLayout;
    VkPipelineLayout pipelineLayout;
    VkPipeline trianglePipeline;
    VkPipeline linePipeline;

    VkCommandPool commandPool;

    VkImage colorImage;
    VkDeviceMemory colorImageMemory;
    VkImageView colorImageView;

    VkImage depthImage;
    VkDeviceMemory depthImageMemory;
    VkImageView depthImageView;

    //uint32_t mipLevels;
    //VkImage textureImage;
    //VkDeviceMemory textureImageMemory;
    //VkImageView textureImageView;
    //VkSampler textureSampler;

    std::vector<glm::vec3> vertices;
    std::vector<glm::vec3> moved_vertices;
    std::vector<glm::vec3> color;
    std::vector<uint16_t> indices;
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    void* vertexBufferMappedPtr = nullptr;
    VkBuffer colorBuffer;
    VkDeviceMemory colorBufferMemory;
    VkBuffer indexBuffer;
    VkDeviceMemory indexBufferMemory;
    void* indexBufferMapperPtr = nullptr;
    static const size_t indexMaxCount = 64;

    std::vector<VkBuffer> uniformBuffers;
    std::vector<VkDeviceMemory> uniformBuffersMemory;
    std::vector<void*> uniformBuffersMapped;

    VkDescriptorPool descriptorPool;
    std::vector<VkDescriptorSet> descriptorSets;

    std::vector<VkCommandBuffer> commandBuffers;

    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    uint32_t currentFrame = 0;

    bool framebufferResized = false;

    void initWindow();

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);

    void initVulkan() {
        createInstance();
        setupDebugMessenger();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapChain();
        createImageViews();
        createRenderPass();
        createDescriptorSetLayout();
        createGraphicsPipeline();
        createCommandPool();
        createColorResources();
        createDepthResources();
        createFramebuffers();
        //createTextureImage();
        //createTextureImageView();
        //createTextureSampler();
        //loadModel();
        createVertexBuffer();
        createIndexBuffer();
        createUniformBuffers();
        initUBO();
        addExampleAnimation();
        createDescriptorPool();
        createDescriptorSets();
        createCommandBuffers();
        createSyncObjects();
    }

    void initUBO() {
        ubo.model = glm::mat4(1.0f);
        auto view = glm::lookAt(glm::vec3(0.0f, 0.0f, 20.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

        // 计算视场角对应的高度
        constexpr float fov = glm::radians(45.0f);
        float halfHeight = glm::tan(fov / 2.0f) * 20.0f; // 20.0f 是相机到原点的距离
        scale = 1.f / halfHeight;
        float aspect = static_cast<float>(swapChainExtent.width) / static_cast<float>(swapChainExtent.height);
        float halfWidth = halfHeight * aspect;

        ubo.projView = glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, 0.1f, 100.0f) * view;
        ubo.projView[1][1] *= -1.f;

        for (auto& uboMapped : uniformBuffersMapped) {
            memcpy(uboMapped, &ubo, sizeof(ubo));
        }
    }

    void addExampleAnimation() {
        // add some animations
        Animation animation;
        animation.faceIds = { 4 };
        animation.rotateAxis = Animation::zAxis;
        animation.rotateCenter = vertices[4ull * 4];
        animation.clockWise = false;
        animationQueues.push(animation);

        animation.faceIds = { 3, 5 };
        animation.rotateAxis = Animation::zAxis;
        animation.rotateCenter = vertices[3ull * 4];
        animation.clockWise = true;
        animationQueues.push(animation);
    }

    void cleanupSwapChain();

    void cleanup();

    void recreateSwapChain();

    void createInstance();

    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

    void setupDebugMessenger();

    void createSurface();

    void pickPhysicalDevice();

    void createLogicalDevice();

    void createSwapChain();

    void createImageViews();

    void createRenderPass();

    void createDescriptorSetLayout();

    void createGraphicsPipeline();

    void createFramebuffers();

    void createCommandPool();

    void createColorResources();

    void createDepthResources();

    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);

    VkFormat findDepthFormat();

    bool hasStencilComponent(VkFormat format) {
        return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
    }

    //void createTextureImage();

    //void generateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels);

    VkSampleCountFlagBits getMaxUsableSampleCount();

    /*
    void createTextureImageView() {
        textureImageView = createImageView(textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);
    }
    */

    //void createTextureSampler();

    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels);

    void createImage(uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);

    void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels);

    //void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);

    //void loadModel();

    void createVertexBuffer();

    void createIndexBuffer();

    void createUniformBuffers();

    void createDescriptorPool();

    void createDescriptorSets();

    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);

    VkCommandBuffer beginSingleTimeCommands();

    void endSingleTimeCommands(VkCommandBuffer commandBuffer);

    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    void createCommandBuffers();

    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);

    void createSyncObjects();

    void updateUniformBuffer(uint32_t currentImage);

    void drawFrame();

    VkShaderModule createShaderModule(const std::vector<uint32_t>& code);

    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);

    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);

    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);

    bool isDeviceSuitable(VkPhysicalDevice device);

    bool checkDeviceExtensionSupport(VkPhysicalDevice device);

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);

    std::vector<const char*> getRequiredExtensions();

    bool checkValidationLayerSupport();

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, 
        VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);
};

