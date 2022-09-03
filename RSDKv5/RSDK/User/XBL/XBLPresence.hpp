#pragma once

#include <RSDK/User/Core/UserPresence.hpp>

#if RETRO_REV02

namespace RSDK::SKU
{
struct XBLRichPresence : UserRichPresence {
    void SetPresence(int32 id, String *message)
    {
        // set EGS rich presence
    }
};
} // namespace RSDK::SKU
#endif
