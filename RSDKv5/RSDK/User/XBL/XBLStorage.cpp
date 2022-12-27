
#include "XBLCore.hpp"

namespace RSDK::SKU
{
int32 XBLUserStorage::TryAuth()
{
    if (authStatus == STATUS_NONE) {
        authStatus = STATUS_CONTINUE;
    }

    return authStatus;
}
} // namespace RSDK::SKU