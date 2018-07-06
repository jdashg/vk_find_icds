#include "vk_find_icds.h"

#ifdef __APPLE__
#include "CoreFoundation/CoreFoundation.h"
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include "Windows.h"
#endif

#ifdef _WIN32

class RegNode final
{
public:
   const HKEY handle_;
   const bool should_close_;

public:
   explicit RegNode(HKEY handle, bool should_close=false)
      : handle_(handle)
      , should_close_(false)
   { }

   ~RegNode() {
      if (should_close_) {
         RegCloseKey(handle_);
      }
   }

   unique_ptr<RegNode> Open(const wchar_t* const subkey) const {
      HKEY subhandle;
      const auto res = RegOpenKeyExW(handle_, subkey, 0, KEY_READ, &subhandle);
      if (res != ERROR_SUCCESS)
         return nullptr;
      return new RegNode(subhandle, true);
   }

   std::vector<std::wstring> EnumSubkeys() const {
      std::vector<std::wstring> ret;
      wchar_t subkey_buff[255 + 1]; // Max key size is 255.
      for (uint32_t i; true; i++) {
         const auto res = RegEnumKeyExW(handle_, i, subkey_buff, nullptr, nullptr,
                                        nullptr, nullptr, nullptr);
         if (res != ERROR_SUCCESS)
            break;
         ret.push_back(std::wstring(subkey_buff));
      }
      return ret;
   }

   std::vector<uint8_t> GetValueBytes(const wchar_t* const subkey,
                                      const wchar_t* const name) const
   {
      uint32_t size = 0;
      auto res = RegGetValueW(handle_, subkey, name, RRF_RT_ANY, nullptr, nullptr, &size);
      if (res != ERROR_SUCCESS)
         return {};

      const auto ret = std::vector<uint8_t>(size);
      auto res = RegGetValueW(handle_, subkey, name, RRF_RT_ANY, nullptr, ret.data(), &size);
      if (res != ERROR_SUCCESS)
         return {};

      return ret;
   }
};

static const auto kRegPath = L"System\\CurrentControlSet\\Control\\Class";
static const auto kGuidDisplay = L"{4d36e968-e325-11ce-bfc1-08002be10318}";
static const auto kGuidSoftwareComponent = L"{5c4c3332-344d-483c-8739-259e934c9cc8}";
static const auto kLegacyRegPath = L"SOFTWARE\\Khronos\\Vulkan\\Drivers";

static std::vector<std::wstring>
LoadFromWindowsRegistry()
{
   const auto local_machine = RegNode(HKEY_LOCAL_MACHINE);
   std::vector<std::wstring> paths_from_reg;

   [&]() {
      const auto class_node = local_machine->Open(kRegClassPath);
      if (!class_node)
         return;

      const auto fn_for_guid = [&](const wchar_t* const guid) {
         const auto guid_node = class_node->Open(guid);
         if (!guid_node)
            return;

         const auto subkeys = guid_node->EnumSubkeys();
         for (const auto& k : subkeys) {
            if (k.length() != 4)
               continue;

            const auto bytes = guid_node->GetValueBytes(k.c_str(), L"VulkanDriverName");
            if (!bytes.size())
               return;

            const auto begin = (const wchar_t*)bytes.data();
            const auto end = begin + bytes.size() / sizeof(wchar_t);
            auto pos = begin;
            for (auto itr = begin; itr != end; ++itr) {
               if (!*itr) {
                  paths_from_reg.push_back(std::wstring(pos, itr));
                  pos = itr + 1;
               }
            }
         }
      };
      fn_for_guid(kGuidDisplay);
      fn_for_guid(kGuidSoftwareComponent);
   }();

   [&]() {
      const auto drivers_node = local_machine->Open(kLegacyRegPath);
      if (!drivers_node)
         return;

      std::vector<wchar_t> val_name_buff(32 * 1024);
      uint32_t val_val = -1;

      for (uint32_t i = 0; true; i++) {
         const auto res = RegEnumValueW(drivers_node.handle_, i, val_name_buff.data(),
                                        val_name_buff.size(), nullptr, nullptr, &val_val,
                                        sizeof(val_val));
         if (res == ERROR_NO_MORE_ITEMS)
            break;
         if (res != ERROR_SUCCESS)
            continue;

         if (val_val == 0) {
            paths_from_reg.push_back(val_name_buff.data());
         }
      }
   }();

   return paths_from_reg;
}

#endif // _WIN32

static std::vector<std::string>
SplitString(const std::string& str, const char delim)
{
   std::vector<std::string> ret;
   const auto end = str.end();
   auto pos = str.begin();
   for (auto itr = str.begin(); itr != end; ++itr) {
      if (*itr == delim) {
         ret.push_back(std::string(pos, itr));
         pos = itr + 1;
      }
   }
   ret.push_back(std::string(pos, end));
   return ret;
}

std::vector<std::unique_ptr<VkIcdPath>>
FindVkIcds()
{
   std::vector<std::unique_ptr<VkIcdPath>> ret;

   const auto fn_add = [&](const std::string& path, const std::wstring& wpath) {
      ret.push_back(std::unique_ptr<VkIcdPath>(new VkIcdPath{path, wpath}));
   };

   // --

   [&]() {
      const auto env = getenv("VK_ICD_FILENAMES");
      if (!env)
         return;

      const auto paths = SplitString(env, ':');
      for (const auto& path : paths) {
         fn_add(path, {});
      }
   }();

   // --

#ifdef _WIN32
   const auto wpaths = LoadFromWindowsRegistry();
   for (const auto& wpath : wpaths) {
      fn_add({}, wpath);
   }
#endif // _WIN32

#ifdef __APPLE__
   /* <bundle>/Contents/Resources/vulkan/icd.d
    * /etc/vulkan/icd.d
    * /usr/local/share/vulkan/icd.d
    *
    * /usr/share/vulkan/icd.d
    * $HOME/.local/share/vulkan/icd.d
    */
   [&]() {
      const CFBundleRef main_bundle = CFBundleGetMainBundle();
      if (!main_bundle)
         return;

      const CFURLRef ref = CFBundleCopyResourcesDirectoryURL(main_bundle);
      if (!ref)
         return;

      std::vector<uint8_t> buff(1000);
      if (!CFURLGetFileSystemRepresentation(ref, true, buff.data(), buff.size()))
         return;

      const auto path = std::string((const char*)buff.data()) + "/Contents/Resources/vulkan/icd.d";
      fn_add(path, {});
   }();
   fn_add("/etc/vulkan/icd.d", {});
   fn_add("/usr/local/share/vulkan/icd.d", {});


#endif // __APPLE__

#ifdef __linux__
   /* /usr/local/etc/vulkan/icd.d
    * /usr/local/share/vulkan/icd.d
    * /etc/vulkan/icd.d
    *
    * /usr/share/vulkan/icd.d
    * $HOME/.local/share/vulkan/icd.d
    */
   fn_add("/usr/local/etc/vulkan/icd.d", {});
   fn_add("/usr/local/share/vulkan/icd.d", {});
   fn_add("/etc/vulkan/icd.d", {});
#endif // __linux

#ifndef _WIN32
   fn_add("/usr/share/vulkan/icd.d", {});

   [&]() {
      const auto env = getenv("HOME");
      if (!env)
         return;

      const auto path = std::string(env) + "/.local/share/vulkan/icd.d";
      fn_add(path, {});
   }();
#endif // !_WIN32

   return ret;
}
