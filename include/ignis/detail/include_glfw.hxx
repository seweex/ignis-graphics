#ifdef GLFWAPI
    #ifndef IGNIS_GLFW_INCLUDED
        #error "You are already using the GLFW library. The Ignis library must be the only user who uses that library."
    #endif
#else
    #define IGNIS_GLFW_INCLUDED
    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>
#endif