#include "vk_find_icds.h"

#include <cstdio>
#include <fstream>

static std::vector<uint8_t>
ReadStream(std::istream* const in, std::string* const out_err)
{
   std::vector<uint8_t> ret(1000*1000);
   uint64_t pos = 0;
   while (true) {
      in->read((char*)ret.data() + pos, ret.size() - pos);
      pos += in->gcount();
      if (in->good()) {
         ret.resize(ret.size() * 2);
         continue;
      }

      if (in->eof()) {
         ret.resize(pos);
         return ret;
      }

      *out_err = std::string("rdstate: ") + std::to_string(in->rdstate());
      return {};
   }
}

int
main(const int argc, const char* const argv[])
{
   const auto icd_paths = FindVkIcds();
   for (const auto& path : icd_paths) {
      std::ifstream input;
      if (path->path.size()) {
         printf("%s:\n", path->path.c_str());
         input.open(path->path.c_str(), std::ios_base::in | std::ios_base::binary);
      } else if (path->wpath.size()) {
         printf("%ls\n", path->wpath.c_str());
#ifdef _WIN32
         input.open(path->wpath.c_str(), std::ios_base::in | std::ios_base::binary);
#endif
      }

      std::string err;
      auto bytes = ReadStream(&input, &err);
      if (!bytes.size())
         continue;
      printf("%s\n\n", (const char*)bytes.data());
   }
   return 0;
}
