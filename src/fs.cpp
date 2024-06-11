#include <fs.h>

namespace fsbridge {

FILE *fopen(const fs::path& p, const char *mode)
{
    return ::fopen(p.string().c_str(), mode);
}


} // fsbridge
