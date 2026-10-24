#ifndef GAME_CLIENT_COMPONENTS_LASERUNFREEZE_H
#define GAME_CLIENT_COMPONENTS_LASERUNFREEZE_H

#include <base/vmath.h>

#include <generated/protocol.h>

#include <game/gamecore.h>

#include <game/client/component.h>

class CLaserUnfreeze : public CComponent
{
public:
int Sizeof() const override { return sizeof(*this); }

void OnReset() override;

void OnRender() override;
void OnPlayerInput(CNetObj_PlayerInput *pOut, CNetObj_PlayerInput *pPersistent);

bool FakeAimAngle(float *pAngle) const
{
if(!m_FakeAngleSending)
return false;
*pAngle = m_FakeAngle;
return true;
}

bool m_Claimed = false;

private:
enum
{
LU_FREEZE_NONE = 0,
LU_FREEZE_NORMAL,
LU_FREEZE_DEEP,
};

struct SPlan
{
const vec2 *m_pTraj;
const int *m_pFreeze;
const vec2 *m_pOther;
const bool *m_pOn;
const int *m_pActive;
int m_NumActive;
bool m_HasOthers;
int m_Len;
int m_FreezeIdx;
int m_MaxBounces;
int m_MaxViableSeg;
float m_BounceCost;
float m_Reach;
};

bool QuantizeDir(vec2 Dir, int *pTx, int *pTy, vec2 *pUnit) const;
int FreezeKindAt(vec2 Pos) const;

int SweepFreeze(vec2 Prev, vec2 Cur) const;

int PredictWorld(vec2 *pSelf, int *pFreeze, vec2 *pOther, bool *pOn, int Max, int Window, int OtherSim, bool *pDeep) const;

bool Sticks(const int *pFreeze, int TrajLen, int From) const;

bool TraceSelfShot(vec2 Dir, const SPlan &Plan, int *pOutHitIdx, float *pOutMiss, int MaxUsefulIdx = (1 << 30)) const;
int FirstTeeOnSegment(vec2 From, vec2 To, const SPlan &Plan, int Idx) const;
bool IsActive() const;
void Fire(CNetObj_PlayerInput *pOut, CNetObj_PlayerInput *pPersistent, int Tx, int Ty, int PredTick);

bool HoldTurn(CNetObj_PlayerInput *pOut, int PredTick);

int m_LastFireTick = -1;
vec2 m_LastFireDir = vec2(0.0f, 0.0f);
int m_LastFireTx = 0;
int m_LastFireTy = 0;
int m_FireExtra = 0;

bool m_FakeAngleSending = false;
float m_FakeAngle = 0.0f;

int m_PlanTx = 0;
int m_PlanTy = 0;
int m_PlanFreezeIdx = -1;
int m_PlanTick = -1;
int m_PlanStable = 0;
};

#endif
