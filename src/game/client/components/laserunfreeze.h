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
gleSending)
 false;
gle = m_FakeAngle;
 true;
}

bool m_Claimed = false;

private:
enum
{
ONE = 0,
ORMAL,

{
st vec2 *m_pTraj;
st int *m_pFreeze;
st vec2 *m_pOther;
st bool *m_pOn;
st int *m_pActive;
t m_NumActive;
t m_Len;
t m_FreezeIdx;
t m_MaxBounces;
t m_MaxViableSeg;
ceCost;
uantizeDir(vec2 Dir, int *pTx, int *pTy, vec2 *pUnit) const;
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
