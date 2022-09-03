#include "XBLCore.hpp"

#if RETRO_REV02
namespace RSDK::SKU
{
XBLCore *InitXBLCore()
{
    // Initalize API subsystems
    XBLCore *core = new XBLCore;

    if (achievements)
        delete achievements;
    achievements = new XBLAchievements;

    if (leaderboards)
        delete leaderboards;
    leaderboards = new XBLLeaderboards;

    if (richPresence)
        delete richPresence;
    richPresence = new XBLRichPresence;

    if (stats)
        delete stats;
    stats = new XBLStats;

    if (userStorage)
        delete userStorage;
    userStorage = new XBLUserStorage;

    return core;
}
} // namespace RSDK::SKU
#endif
