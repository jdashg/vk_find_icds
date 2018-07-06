#ifndef VK_FIND_ICDS_H
#define VK_FIND_ICDS_H

#include <string>
#include <vector>

struct VkIcdPath final
{
   std::string path;
   std::wstring wpath;
};

std::vector<std::unique_ptr<VkIcdPath>>
FindVkIcds();

#endif // VK_FIND_ICDS_H
