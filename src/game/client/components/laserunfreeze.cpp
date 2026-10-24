#include "laserunfreeze.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <engine/shared/config.h>

#include <game/collision.h>
#include <game/mapitems.h>

#include <game/client/components/controls.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/gameworld.h>

static const float LU_TEE_RADIUS = 28.0f;

static const int LU_RELOAD_TICKS = 40;

static const int LU_BOUNCE_TICKS = 8;

static const float LU_TARGET_LEN = 500.0f;

static const int LU_JITTER = 1;

static const int LU_HOLD_TICKS = 5;

static const float LU_SAFE_MISS = 16.0f;

static const int LU_CONFIRM_TICKS = 3;

static const int LU_COMMIT_NOW = 4;

static const int LU_TURN_TICKS = 16;

static const float LU_TEE_DRIFT = 6.0f;
static const float LU_TEE_DRIFT_MAX = 96.0f;

void CLaserUnfreeze::OnReset()
{
m_LastFireTick = -1;
m_LastFireTx = 0;
m_LastFireTy = 0;
m_FireExtra = 0;
m_Claimed = false;
m_FakeAngleSending = false;
m_PlanTx = 0;
m_PlanTy = 0;
m_PlanFreezeIdx = -1;
m_PlanTick = -1;
m_PlanStable = 0;
}

bool CLaserUnfreeze::IsActive() const
{
if(!g_Config.m_ClZzAutoUnfreeze)
 false;
if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
 false;
const int LocalId = GameClient()->m_Snap.m_LocalClientId;
if(LocalId < 0)
 false;
if(!GameClient()->m_Snap.m_pLocalCharacter)
 false;
if(!GameClient()->m_Snap.m_aCharacters[LocalId].m_Active)
 false;

if(!GameClient()->m_PredictedChar.m_aWeapons[WEAPON_LASER].m_Got)
 false;

if(GameClient()->m_PredictedChar.m_LaserHitDisabled)
 false;
return true;
}

int CLaserUnfreeze::FreezeKindAt(vec2 Pos) const
{
const CCollision *pCol = Collision();
const int Nx = std::clamp((int)Pos.x / 32, 0, pCol->GetWidth() - 1);
const int Ny = std::clamp((int)Pos.y / 32, 0, pCol->GetHeight() - 1);
const int Idx = Ny * pCol->GetWidth() + Nx;
const int G = pCol->GetTileIndex(Idx);
const int F = pCol->GetFrontTileIndex(Idx);
const int S = pCol->SwitchLayer() ? pCol->GetSwitchType(Idx) : 0;

if(G == TILE_DFREEZE || F == TILE_DFREEZE || S == TILE_DFREEZE)
 LU_FREEZE_DEEP;
const auto IsNorm = [](int T) { return T == TILE_FREEZE || T == TILE_LFREEZE; };
if(IsNorm(G) || IsNorm(F) || IsNorm(S))
 LU_FREEZE_NORMAL;
return LU_FREEZE_NONE;
}

int CLaserUnfreeze::SweepFreeze(vec2 Prev, vec2 Cur) const
{
const float d = distance(Prev, Cur);
const int Steps = (d == 0.0f) ? 1 : (int)(d + 1.0f);
int Worst = LU_FREEZE_NONE;
for(int i = 0; i < Steps; i++)
{
st vec2 P = mix(Prev, Cur, (d == 0.0f) ? 0.0f : (float)i / d);
st int K = FreezeKindAt(P);
 LU_FREEZE_DEEP;
ORMAL)
ORMAL;
}
return Worst;
}

int CLaserUnfreeze::PredictWorld(vec2 *pSelf, int *pFreeze, vec2 *pOther, bool *pOn, int Max, int Window, int OtherSim, bool *pDeep) const
{
*pDeep = false;
const int LocalId = GameClient()->m_Snap.m_LocalClientId;

CGameWorld SimWorld;
SimWorld.CopyWorld(&GameClient()->m_PredictedWorld);
CCharacter *pSelfChar = SimWorld.GetCharacterById(LocalId);
if(!pSelfChar)
 -1;

for(int i = 0; i < MAX_CLIENTS; i++)
{
[i] = false;
!GameClient()->m_Snap.m_aCharacters[i].m_Active || GameClient()->IsOtherTeam(i))
tinue;
Id(i);
tinue;
[i] = true;
= pOtherChar->GetPos();
}

pSelf[0] = pSelfChar->GetPos();
pFreeze[0] = LU_FREEZE_NONE;
CNetObj_PlayerInput In = GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy];
In.m_Fire = 0;

vec2 Prev = pSelfChar->GetPos();
int FreezeIdx = -1;
for(int k = 1; k < Max; k++)
{
DirectInput(&In);
PredictedInput(&In);
SimWorld.GetCharacterById(LocalId);
true;
 -1;
st vec2 Cur = pSelfChar->GetPos();
Cur;
SweepFreeze(Prev, Cur);

0 && pFreeze[k] != LU_FREEZE_NONE)
LU_FREEZE_DEEP)
true;
 -1;
k;
OtherSim)
t i = 0; i < MAX_CLIENTS; i++)
[i])
tinue;
= SimWorld.GetCharacterById(i);
Max + k] = pOtherChar ? pOtherChar->GetPos() : pOther[i * Max + k - 1];
0 && k >= Window)
 -1;
Cur;
}
return FreezeIdx;
}

int CLaserUnfreeze::FirstTeeOnSegment(vec2 From, vec2 To, const SPlan &Plan, int Idx) const
{
if(!Plan.m_HasOthers)
 -1;
const int At = std::clamp(Idx, 0, Plan.m_Len - 1);

const float Margin = std::min(LU_TEE_DRIFT * (float)At, LU_TEE_DRIFT_MAX);
int Closest = -1;
float ClosestLen = distance(From, To) * 100.0f;
for(int n = 0; n < Plan.m_NumActive; n++)
{
st int i = Plan.m_pActive[n];
st vec2 Pos = Plan.m_pOther[i * Plan.m_Len + At];
t_on_line(From, To, Pos, Hit))
tinue;
ce(Pos, Hit) >= LU_TEE_RADIUS + Margin)
tinue;
st float Len = distance(From, Hit);
 < ClosestLen)
 = Len;
i;
 Closest;
}

bool CLaserUnfreeze::Sticks(const int *pFreeze, int TrajLen, int From) const
{
const int End = std::min(From + LU_HOLD_TICKS, TrajLen - 1);
for(int k = From; k <= End; k++)
{
LU_FREEZE_NONE)
 false;
}
return true;
}

bool CLaserUnfreeze::TraceSelfShot(vec2 Dir, const SPlan &Plan, int *pOutHitIdx, float *pOutMiss, int MaxUsefulIdx) const
{
if(length(Dir) < 0.001f)
 false;
Dir = normalize(Dir);

const float BounceCost = Plan.m_BounceCost;
const float Reach = Plan.m_Reach;

vec2 Pos = Plan.m_pTraj[0];
float Energy = Reach;
for(int Seg = 0; Seg <= Plan.m_MaxViableSeg; Seg++)
{
ergy <= 0.0f)
 false;

st int Idx = Seg * LU_BOUNCE_TICKS - 1;
MaxUsefulIdx)
 false;

= Pos + Dir * Energy;
= To, Before = To;
t z = 0;
st int Res = Collision()->IntersectLineTeleWeapon(Pos, To, &Coltile, &Before, &z);
Before;

st int Other = FirstTeeOnSegment(Pos, To, Plan, Idx);
 = 1e18f;

1 && Other >= 0)
st vec2 P = Plan.m_pOther[Other * Plan.m_Len + std::clamp(Idx, 0, Plan.m_Len - 1)];
t_on_line(Pos, To, P, Hit))
 = distance(Pos, Hit);
1)
st int Lo = Idx - LU_JITTER;
st int Hi = Idx + LU_JITTER;
1 || Hi >= Plan.m_Len)
 false;

t NumHit = 0;
= 0.0f;
t k = Lo; k <= Hi; k++)
t_on_line(Pos, To, Plan.m_pTraj[k], Hit))
tinue;
st float d = distance(Plan.m_pTraj[k], Hit);
LU_TEE_RADIUS || distance(Pos, Hit) > OtherLen)
tinue;
umHit++;
std::max(Worst, d);
umHit > 0)
umHit != Hi - Lo + 1)
 false;

LU_SAFE_MISS)
 false;

.m_FreezeIdx < 0 || Plan.m_FreezeIdx > Lo - 1)
 false;
.m_pFreeze, Plan.m_Len, Lo))
 false;
Idx;
Worst;
 true;
0)
 false;

 false;

TILE_TELEINWEAPON)
 false;

= Before;
= Dir * 4.0f;
()->MovePoint(&TempPos, &TempDir, 1.0f, nullptr);
ergy -= distance(Pos, TempPos) + BounceCost;
TempPos;
normalize(TempDir);
}
return false;
}

bool CLaserUnfreeze::QuantizeDir(vec2 Dir, int *pTx, int *pTy, vec2 *pUnit) const
{
const int Tx = (int)(Dir.x * LU_TARGET_LEN);
const int Ty = (int)(Dir.y * LU_TARGET_LEN);
if(Tx == 0 && Ty == 0)
 false;
*pTx = Tx;
*pTy = Ty;
*pUnit = normalize(vec2((float)Tx, (float)Ty));
return true;
}

void CLaserUnfreeze::Fire(CNetObj_PlayerInput *pOut, CNetObj_PlayerInput *pPersistent, int Tx, int Ty, int PredTick)
{
if(Tx == 0 && Ty == 0)
;

pOut->m_WantedWeapon = WEAPON_LASER + 1;
pOut->m_TargetX = Tx;
pOut->m_TargetY = Ty;
m_Claimed = true;

if(GameClient()->m_PredictedChar.m_ActiveWeapon != WEAPON_LASER)
;

m_FireExtra += 2;
if(pPersistent)
t->m_Fire + m_FireExtra) & INPUT_STATE_MASK;
else
2) & INPUT_STATE_MASK;
m_LastFireTick = PredTick;
m_LastFireTx = Tx;
m_LastFireTy = Ty;
m_LastFireDir = normalize(vec2((float)Tx, (float)Ty));

m_Claimed = true;
if(!g_Config.m_ClZzAutoUnfreezeSilent)
{

gle = angle(vec2((float)Tx, (float)Ty));
gleSending = true;
}
}

bool CLaserUnfreeze::HoldTurn(CNetObj_PlayerInput *pOut, int PredTick)
{
if(g_Config.m_ClZzAutoUnfreezeSilent)
 false;
if(m_LastFireTick < 0 || (m_LastFireTx == 0 && m_LastFireTy == 0))
 false;

if(PredTick - m_LastFireTick > LU_TURN_TICKS)
 false;

if(pOut->m_Hook != 0 && GameClient()->m_PredictedChar.m_HookState == HOOK_IDLE)
 false;

pOut->m_TargetX = m_LastFireTx;
pOut->m_TargetY = m_LastFireTy;
m_Claimed = true;
m_FakeAngle = angle(vec2((float)m_LastFireTx, (float)m_LastFireTy));
m_FakeAngleSending = true;
return true;
}

void CLaserUnfreeze::OnRender()
{
if(g_Config.m_ClZzAutoUnfreezeSilent)
;
if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
;

if(!m_FakeAngleSending || length(m_LastFireDir) < 0.001f)
;
const int Dummy = g_Config.m_ClDummy;
GameClient()->m_Controls.m_aTargetPos[Dummy] = GameClient()->m_LocalCharacterPos + m_LastFireDir * 150.0f;
}

void CLaserUnfreeze::OnPlayerInput(CNetObj_PlayerInput *pOut, CNetObj_PlayerInput *pPersistent)
{
m_Claimed = false;
m_FakeAngleSending = false;
if(!IsActive())
;

const int PredTick = Client()->PredGameTick(g_Config.m_ClDummy);

if(HoldTurn(pOut, PredTick))
;

int LastShot = m_LastFireTick;
const int LocalId = GameClient()->m_Snap.m_LocalClientId;
if(GameClient()->m_Snap.m_aCharacters[LocalId].m_Cur.m_Weapon == WEAPON_LASER)
t()->m_Snap.m_aCharacters[LocalId].m_Cur.m_AttackTick);
if(LastShot >= 0 && PredTick - LastShot < LU_RELOAD_TICKS)
;

if(pOut->m_Hook != 0 && GameClient()->m_PredictedChar.m_HookState == HOOK_IDLE)
;

if(GameClient()->m_PredictedChar.m_DeepFrozen || GameClient()->m_PredictedChar.m_FreezeEnd > PredTick)
;

const int Look = std::clamp(g_Config.m_ClZzAutoUnfreezeTicks, 8, 120);
const int Window = std::clamp(g_Config.m_ClZzAutoUnfreezeDirTicks, 1, 40);
const int MaxBounces = std::clamp(g_Config.m_ClZzAutoUnfreezeMaxPoints, 1, 10);
const int Len = Look + 1;

static std::vector<vec2> s_vTraj;
static std::vector<int> s_vFreeze;
static std::vector<vec2> s_vOther;
if(s_vTraj.size() < (size_t)Len)
);
if(s_vFreeze.size() < (size_t)Len)
);
if(s_vOther.size() < (size_t)MAX_CLIENTS * (size_t)Len)
TS * (size_t)Len);
std::vector<vec2> &vTraj = s_vTraj;
std::vector<int> &vFreeze = s_vFreeze;
std::vector<vec2> &vOther = s_vOther;

const int OtherSim = std::min(LU_BOUNCE_TICKS * MaxBounces, Len);
bool Deep = false;
bool aOn[MAX_CLIENTS];
const int FreezeIdx = PredictWorld(vTraj.data(), vFreeze.data(), vOther.data(), aOn, Len, Window, OtherSim, &Deep);
if(Deep || FreezeIdx <= 0 || FreezeIdx > Window)
;

int aActiveOthers[MAX_CLIENTS];
int NumActiveOthers = 0;
for(int i = 0; i < MAX_CLIENTS; i++)
{
[i])
umActiveOthers++] = i;
}
const bool HasOthers = NumActiveOthers > 0;

SPlan Plan;
Plan.m_pTraj = vTraj.data();
Plan.m_pFreeze = vFreeze.data();
Plan.m_pOther = vOther.data();
Plan.m_pOn = aOn;
Plan.m_pActive = aActiveOthers;
Plan.m_NumActive = NumActiveOthers;
Plan.m_HasOthers = HasOthers;
Plan.m_Len = Len;
Plan.m_FreezeIdx = FreezeIdx;
Plan.m_MaxBounces = MaxBounces;

Plan.m_BounceCost = (float)GameClient()->m_aTuning[g_Config.m_ClDummy].m_LaserBounceCost;
Plan.m_Reach = (float)GameClient()->m_aTuning[g_Config.m_ClDummy].m_LaserReach;

Plan.m_MaxViableSeg = -1;
for(int Seg = 1; Seg <= Plan.m_MaxBounces; Seg++)
{
st int Idx = Seg * LU_BOUNCE_TICKS - 1;
st int Lo = Idx - LU_JITTER;
st int Hi = Idx + LU_JITTER;
Hi >= Plan.m_Len)
tinue;
.m_FreezeIdx < 0 || Plan.m_FreezeIdx > Lo - 1)
tinue;
.m_pFreeze, Plan.m_Len, Lo))
tinue;
.m_MaxViableSeg = Seg;
}
if(Plan.m_MaxViableSeg < 0)
;

vec2 Aim = pPersistent ? vec2((float)pPersistent->m_TargetX, (float)pPersistent->m_TargetY) : vec2((float)pOut->m_TargetX, (float)pOut->m_TargetY);
if(length(Aim) < 0.001f)
= normalize(Aim);
const bool FullCircle = g_Config.m_ClZzAutoUnfreezeFov >= 360;

const float HalfFovCos = FullCircle ? 0.0f : std::cos(((float)g_Config.m_ClZzAutoUnfreezeFov * 0.5f) * (pi / 180.0f));

const bool MostBounces = g_Config.m_ClZzAutoUnfreezeMostBounces != 0;
const int CurDirTicks = std::clamp(g_Config.m_ClZzAutoUnfreezeCurDirTicks, 1, 40);

int BestIdx = MostBounces ? -1 : (1 << 30);
float BestMiss = 1e18f;
vec2 BestDir = vec2(0.0f, 0.0f);
int BestTx = 0, BestTy = 0;
bool HaveBest = false;

const auto Consider = [&](vec2 CosSin) {
t Tx, Ty;
uantizeDir(CosSin, &Tx, &Ty, &Dir))
;
< HalfFovCos)
;
t HitIdx = 0;
Plan, &HitIdx, &Miss, MostBounces ? (1 << 30) : BestIdx))
;

st bool Better = !HaveBest || (MostBounces ? HitIdx > BestIdx : HitIdx < BestIdx) || (HitIdx == BestIdx && Miss < BestMiss);
HitIdx;
Dir;
= Ty;
t Tx, Ty;
t HitIdx = 0;
uantizeDir(Aim, &Tx, &Ty, &Dir) && TraceSelfShot(Dir, Plan, &HitIdx, &Miss, CurDirTicks) && HitIdx <= CurDirTicks)
HitIdx;
Dir;
= Ty;
st int Coarse = 512;
st float Step = 2.0f * pi / (float)Coarse;

s_CoarseReady = false;
t i = 0; i < Coarse; i++)
Step), std::sin((float)i * Step));
t i = 0; i < Coarse; i++)
sider(s_aCoarseDir[i]);
;

st float Base = std::atan2(BestDir.y, BestDir.x);
t i = -16; i <= 16; i++)
st float Ang = Base + (float)i * (Step / 16.0f);
sider(vec2(std::cos(Ang), std::sin(Ang)));
st vec2 SendDir = normalize(vec2((float)BestTx, (float)BestTy));

const vec2 PrevDir = (m_PlanTx || m_PlanTy) ? normalize(vec2((float)m_PlanTx, (float)m_PlanTy)) : vec2(0.0f, 0.0f);
const bool SamePlan =
Tick == PredTick - 1 &&
gth(PrevDir) > 0.001f &&
dDir, PrevDir) > std::cos(2.0f * (pi / 180.0f)) &&
FreezeIdx) <= 1;
m_PlanStable = SamePlan ? m_PlanStable + 1 : 1;
m_PlanTx = BestTx;
m_PlanTy = BestTy;
m_PlanFreezeIdx = FreezeIdx;
m_PlanTick = PredTick;

if(m_PlanStable < LU_CONFIRM_TICKS && FreezeIdx > LU_COMMIT_NOW)
;

Fire(pOut, pPersistent, BestTx, BestTy, PredTick);
}
