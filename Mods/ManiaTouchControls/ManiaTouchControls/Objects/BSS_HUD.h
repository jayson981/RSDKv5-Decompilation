#ifndef OBJ_BSS_HUD_H
#define OBJ_BSS_HUD_H

#include "GameAPI/Game.h"

// Object Class
typedef struct {
    RSDK_OBJECT
    uint16 aniFrames;
    uint8 debugEnabled;
} ObjectBSS_HUD;

// Modded Object Class
typedef struct {
    uint16 dpadFrames;
    Animator dpadAnimator;
    Animator dpadTouchAnimator;
    Vector2 dpadPos;
    int32 dpadAlpha;
    Vector2 actionPos;
    Vector2 pausePos;
    int32 pauseAlpha;
} ModObjectBSS_HUD;

// Entity Class
typedef struct {
    RSDK_ENTITY
    Animator sphereAnimator;
    Animator ringAnimator;
    Animator numbersAnimator;
} EntityBSS_HUD;

// Object Struct
extern ObjectBSS_HUD *BSS_HUD;
extern ModObjectBSS_HUD *Mod_BSS_HUD;

// Standard Entity Events
void BSS_HUD_Draw(void);
void BSS_HUD_StageLoad(void);

// Extra Entity Functions
void BSS_HUD_DrawTouchControls(void);

#endif //! OBJ_BSS_HUD_H
