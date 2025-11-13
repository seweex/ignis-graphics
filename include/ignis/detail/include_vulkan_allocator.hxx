#ifdef VMA_VULKAN_VERSION
    #ifndef IGNIS_VMA_INCLUDED
        #error "You are already using the Vulkan Allocators library. The Ignis library must be the only user who uses that library."
    #endif
#else
    #define IGNIS_VMA_INCLUDED
    #define VMA_IMPLEMENTATION

    #include <vulkan/vulkan_raii.hpp>
    #include <vma/vk_mem_alloc.h>

    #include <vk_mem_alloc.hpp>
    #include <vk_mem_alloc_enums.hpp>
    #include <vk_mem_alloc_structs.hpp>
    #include <vk_mem_alloc_handles.hpp>
    #include <vk_mem_alloc_funcs.hpp>
#endif