Vulkan Cube Example

How to build:
1.install vulkan SDK

2. using vcpkg install packages that we need: glslang glm glfw3 spdlog VMA

3. mkdir build
   
4. cd build
  
5. cmake .. -DCMAKE_TOOLCHIAN_FILE=[vcpkg install dir]/scripts/buildsystems/vcpkg.cmake
