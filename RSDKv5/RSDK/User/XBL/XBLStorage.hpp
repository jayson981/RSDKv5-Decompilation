#pragma once

#include <RSDK/User/Core/UserStorage.hpp>

#if RETRO_REV02
namespace RSDK::SKU
{
struct XBLUserStorage : UserStorage {
    int32 TryAuth();
    int32 TryInitStorage()
    {
        // init EGS storage
        storageStatus = STATUS_OK;
        return storageStatus;
    }
    bool32 GetUsername(String *userName)
    {
        // get EGS username
        return false;
    }
    bool32 TryLoadUserFile(const char *filename, void *buffer, uint32 size, void (*callback)(int32 status))
    {
        // load file from EGS cloud
        return false;
    }
    bool32 TrySaveUserFile(const char *filename, void *buffer, uint32 size, void (*callback)(int32 status), bool32 compressed)
    {
        // save file to EGS cloud
        return false;
    }
    bool32 TryDeleteUserFile(const char *filename, void (*callback)(int32 status))
    {
        // delete file from EGS cloud
        return false;
    }
    void ClearPrerollErrors() {}
};
} // namespace RSDK::SKU
#endif