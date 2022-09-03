#pragma once

#include <RSDK/User/Core/UserAchievements.hpp>

#if RETRO_REV02
namespace RSDK::SKU
{
struct XBLAchievements : UserAchievements {
    bool32 CheckAchievementsEnabled() { return true; }
    void SetAchievementNames(String **names, int32 count)
    {
        // set achievement names
    }
    String *GetAchievementString(String *string)
    {
        // get achievement string
        return NULL;
    }
    String *GetAchievementName(String *name, uint32 id)
    {
        // get achievement name
        return NULL;
    }
    bool32 CheckAchievementPopupEnabled() { return true; }
    int32 GetNextAchievementID()
    {
        // get next achievement from the list
        return 0;
    }
    void RemoveLastAchievementID()
    {
        // remove achievement from the list
    }
    void TryUnlockAchievement(AchievementID *id)
    {
        // unload EGS achievement
    }
};
} // namespace RSDK::SKU
#endif
