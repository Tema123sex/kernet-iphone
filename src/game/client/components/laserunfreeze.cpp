#include "laserunfreeze.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/collision.h>
#include <game/mapitems.h>

#include <game/client/components/controls.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/gameworld.h>

#include <cstdio>
#include <cstring>

static void LU_Log(const char *pMsg)
{
FILE *f = fopen("/storage/emulated/0/Download/lu.log", "a");
if(!f)
f = fopen("/sdcard/lu.log", "a");
if(!f)
return;
fprintf(f, "%s\n", pMsg);
fflush(f);
fclose(f);
}


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
		return false;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return false;
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0)
		return false;
	if(!GameClient()->m_Snap.m_pLocalCharacter)
		return false;
	if(!GameClient()->m_Snap.m_aCharacters[LocalId].m_Active)
		return false;

	if(!GameClient()->m_PredictedChar.m_aWeapons[WEAPON_LASER].m_Got)
		return false;

	if(GameClient()->m_PredictedChar.m_LaserHitDisabled)
		return false;
	if(g_Config.m_ClDummy < 0 || g_Config.m_ClDummy >= NUM_DUMMIES)
		return false;
	if(!Collision())
		return false;
	if(Collision()->GetWidth() <= 0)
		return false;
	return true;
}

int CLaserUnfreeze::FreezeKindAt(vec2 Pos) const
{
	const CCollision *pCol = Collision();
	if(!pCol || pCol->GetWidth() <= 0 || pCol->GetHeight() <= 0)
		return LU_FREEZE_NONE;
	const int Nx = std::clamp((int)Pos.x / 32, 0, pCol->GetWidth() - 1);
	const int Ny = std::clamp((int)Pos.y / 32, 0, pCol->GetHeight() - 1);
	const int Idx = Ny * pCol->GetWidth() + Nx;
	const int G = pCol->GetTileIndex(Idx);
	const int F = pCol->GetFrontTileIndex(Idx);
	const int S = pCol->SwitchLayer() ? pCol->GetSwitchType(Idx) : 0;

	if(G == TILE_DFREEZE || F == TILE_DFREEZE || S == TILE_DFREEZE)
		return LU_FREEZE_DEEP;
	const auto IsNorm = [](int T) { return T == TILE_FREEZE || T == TILE_LFREEZE; };
	if(IsNorm(G) || IsNorm(F) || IsNorm(S))
		return LU_FREEZE_NORMAL;
	return LU_FREEZE_NONE;
}

int CLaserUnfreeze::SweepFreeze(vec2 Prev, vec2 Cur) const
{
	const float d = distance(Prev, Cur);
	const int Steps = (d == 0.0f) ? 1 : (int)(d + 1.0f);
	int Worst = LU_FREEZE_NONE;
	for(int i = 0; i < Steps; i++)
	{
		const vec2 P = mix(Prev, Cur, (d == 0.0f) ? 0.0f : (float)i / d);
		const int K = FreezeKindAt(P);
		if(K == LU_FREEZE_DEEP)
			return LU_FREEZE_DEEP;
		if(K == LU_FREEZE_NORMAL)
			Worst = LU_FREEZE_NORMAL;
	}
	return Worst;
}

int CLaserUnfreeze::PredictWorld(vec2 *pSelf, int *pFreeze, vec2 *pOther, bool *pOn, int Max, int Window, int OtherSim, bool *pDeep) const
{
	*pDeep = false;
	LU_Log("PredictWorld: entry");
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0 || LocalId >= MAX_CLIENTS)
	{
		LU_Log("PredictWorld: LocalId out of range");
		return -1;
	}
	if(!GameClient()->m_PredictedWorld.GetCharacterById(LocalId))
	{
		LU_Log("PredictWorld: no char in PredictedWorld");
		return -1;
	}

	CGameWorld SimWorld;
	SimWorld.CopyWorld(&GameClient()->m_PredictedWorld);
	LU_Log("PredictWorld: CopyWorld ok");
	CCharacter *pSelfChar = SimWorld.GetCharacterById(LocalId);
	if(!pSelfChar)
	{
		LU_Log("PredictWorld: no char in SimWorld");
		return -1;
	}

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		pOn[i] = false;
		if(i == LocalId)
			continue;
		if(!GameClient()->m_Snap.m_aCharacters[i].m_Active)
			continue;
		if(GameClient()->IsOtherTeam(i))
			continue;
		CCharacter *pOtherChar = SimWorld.GetCharacterById(i);
		if(!pOtherChar)
			continue;
		pOn[i] = true;
		pOther[i * Max] = pOtherChar->GetPos();
	}

	pSelf[0] = pSelfChar->GetPos();
	pFreeze[0] = LU_FREEZE_NONE;
	CNetObj_PlayerInput In = GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy];
	In.m_Fire = 0;

	vec2 Prev = pSelfChar->GetPos();
	int FreezeIdx = -1;
	for(int k = 1; k < Max; k++)
	{
		pSelfChar->OnDirectInput(&In);
		SimWorld.m_GameTick++;
		pSelfChar->OnPredictedInput(&In);
		LU_Log("PredictWorld: before Tick");
		SimWorld.Tick();
		LU_Log("PredictWorld: after Tick");

		pSelfChar = SimWorld.GetCharacterById(LocalId);
		if(!pSelfChar)
		{
			*pDeep = true;
			return -1;
		}

		const vec2 Cur = pSelfChar->GetPos();
		pSelf[k] = Cur;
		pFreeze[k] = SweepFreeze(Prev, Cur);

		if(FreezeIdx < 0 && pFreeze[k] != LU_FREEZE_NONE)
		{
			if(pFreeze[k] == LU_FREEZE_DEEP)
			{
				*pDeep = true;
				return -1;
			}
			FreezeIdx = k;
		}

		if(k < OtherSim)
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(!pOn[i])
					continue;
				CCharacter *pOtherChar = SimWorld.GetCharacterById(i);
				if(pOtherChar)
					pOther[i * Max + k] = pOtherChar->GetPos();
				else
					pOther[i * Max + k] = pOther[i * Max + k - 1];
			}
		}

		if(FreezeIdx < 0 && k >= Window)
			return -1;
		Prev = Cur;
	}
	return FreezeIdx;
}

int CLaserUnfreeze::FirstTeeOnSegment(vec2 From, vec2 To, const SPlan &Plan, int Idx) const
{
	if(!Plan.m_HasOthers)
		return -1;
	const int At = std::clamp(Idx, 0, Plan.m_Len - 1);

	const float Margin = std::min(LU_TEE_DRIFT * (float)At, LU_TEE_DRIFT_MAX);
	int Closest = -1;
	float ClosestLen = distance(From, To) * 100.0f;
	for(int n = 0; n < Plan.m_NumActive; n++)
	{
		const int i = Plan.m_pActive[n];
		const vec2 Pos = Plan.m_pOther[i * Plan.m_Len + At];
		vec2 Hit;
		if(!closest_point_on_line(From, To, Pos, Hit))
			continue;
		if(distance(Pos, Hit) >= LU_TEE_RADIUS + Margin)
			continue;
		const float Len = distance(From, Hit);
		if(Len < ClosestLen)
		{
			ClosestLen = Len;
			Closest = i;
		}
	}
	return Closest;
}

bool CLaserUnfreeze::Sticks(const int *pFreeze, int TrajLen, int From) const
{
	const int End = std::min(From + LU_HOLD_TICKS, TrajLen - 1);
	for(int k = From; k <= End; k++)
	{
		if(pFreeze[k] != LU_FREEZE_NONE)
			return false;
	}
	return true;
}

bool CLaserUnfreeze::TraceSelfShot(vec2 Dir, const SPlan &Plan, int *pOutHitIdx, float *pOutMiss, int MaxUsefulIdx) const
{
	LU_Log("TraceSelfShot: entry");
	if(length(Dir) < 0.001f)
		return false;
	if(!Collision() || Collision()->GetWidth() <= 0)
		return false;
	Dir = normalize(Dir);

	const float BounceCost = Plan.m_BounceCost;
	const float Reach = Plan.m_Reach;

	vec2 Pos = Plan.m_pTraj[0];
	float Energy = Reach;
	for(int Seg = 0; Seg <= Plan.m_MaxViableSeg; Seg++)
	{
		if(Energy <= 0.0f)
			return false;

		const int Idx = Seg * LU_BOUNCE_TICKS - 1;
		if(Idx > MaxUsefulIdx)
			return false;

		vec2 To = Pos + Dir * Energy;
		vec2 Coltile = To;
		vec2 Before = To;
		int z = 0;
		const int Res = Collision()->IntersectLineTeleWeapon(Pos, To, &Coltile, &Before, &z);
		if(Res)
			To = Before;

		const int Other = FirstTeeOnSegment(Pos, To, Plan, Idx);
		float OtherLen = 1e18f;

		if(Seg >= 1 && Other >= 0)
		{
			vec2 Hit;
			const int At = std::clamp(Idx, 0, Plan.m_Len - 1);
			const vec2 P = Plan.m_pOther[Other * Plan.m_Len + At];
			if(closest_point_on_line(Pos, To, P, Hit))
				OtherLen = distance(Pos, Hit);
		}

		if(Seg >= 1)
		{
			const int Lo = Idx - LU_JITTER;
			const int Hi = Idx + LU_JITTER;
			if(Lo < 1 || Hi >= Plan.m_Len)
				return false;

			int NumHit = 0;
			float Worst = 0.0f;
			for(int k = Lo; k <= Hi; k++)
			{
				vec2 Hit;
				if(!closest_point_on_line(Pos, To, Plan.m_pTraj[k], Hit))
					continue;
				const float d = distance(Plan.m_pTraj[k], Hit);
				if(d >= LU_TEE_RADIUS)
					continue;
				if(distance(Pos, Hit) > OtherLen)
					continue;
				NumHit++;
				Worst = std::max(Worst, d);
			}

			if(NumHit > 0)
			{
				if(NumHit != Hi - Lo + 1)
					return false;
				if(Worst > LU_SAFE_MISS)
					return false;
				if(Plan.m_FreezeIdx < 0 || Plan.m_FreezeIdx > Lo - 1)
					return false;
				if(!Sticks(Plan.m_pFreeze, Plan.m_Len, Lo))
					return false;
				*pOutHitIdx = Idx;
				*pOutMiss = Worst;
				return true;
			}
		}

		if(Other >= 0)
			return false;

		if(!Res)
			return false;

		if(Res == TILE_TELEINWEAPON)
			return false;

		vec2 TempPos = Before;
		vec2 TempDir = Dir * 4.0f;
		Collision()->MovePoint(&TempPos, &TempDir, 1.0f, nullptr);
		Energy -= distance(Pos, TempPos) + BounceCost;
		Pos = TempPos;
		Dir = normalize(TempDir);
	}
	return false;
}

bool CLaserUnfreeze::QuantizeDir(vec2 Dir, int *pTx, int *pTy, vec2 *pUnit) const
{
	const int Tx = (int)(Dir.x * LU_TARGET_LEN);
	const int Ty = (int)(Dir.y * LU_TARGET_LEN);
	if(Tx == 0 && Ty == 0)
		return false;
	*pTx = Tx;
	*pTy = Ty;
	*pUnit = normalize(vec2((float)Tx, (float)Ty));
	return true;
}

void CLaserUnfreeze::Fire(CNetObj_PlayerInput *pOut, CNetObj_PlayerInput *pPersistent, int Tx, int Ty, int PredTick)
{
	LU_Log("Fire: entry");
	if(Tx == 0 && Ty == 0)
		return;

	pOut->m_WantedWeapon = WEAPON_LASER + 1;
	pOut->m_TargetX = Tx;
	pOut->m_TargetY = Ty;
	m_Claimed = true;

	if(GameClient()->m_PredictedChar.m_ActiveWeapon != WEAPON_LASER)
		return;

	m_FireExtra += 2;
	if(pPersistent)
		pOut->m_Fire = (pPersistent->m_Fire + m_FireExtra) & INPUT_STATE_MASK;
	else
		pOut->m_Fire = (pOut->m_Fire + 2) & INPUT_STATE_MASK;
	m_LastFireTick = PredTick;
	m_LastFireTx = Tx;
	m_LastFireTy = Ty;
	m_LastFireDir = normalize(vec2((float)Tx, (float)Ty));

	m_Claimed = true;
	if(!g_Config.m_ClZzAutoUnfreezeSilent)
	{
		m_FakeAngle = angle(vec2((float)Tx, (float)Ty));
		m_FakeAngleSending = true;
	}
}

bool CLaserUnfreeze::HoldTurn(CNetObj_PlayerInput *pOut, int PredTick)
{
	if(g_Config.m_ClZzAutoUnfreezeSilent)
		return false;
	if(m_LastFireTick < 0 || (m_LastFireTx == 0 && m_LastFireTy == 0))
		return false;

	if(PredTick - m_LastFireTick > LU_TURN_TICKS)
		return false;

	if(pOut->m_Hook != 0 && GameClient()->m_PredictedChar.m_HookState == HOOK_IDLE)
		return false;

	pOut->m_TargetX = m_LastFireTx;
	pOut->m_TargetY = m_LastFireTy;
	m_Claimed = true;
	m_FakeAngle = angle(vec2((float)m_LastFireTx, (float)m_LastFireTy));
	m_FakeAngleSending = true;
	return true;
}

void CLaserUnfreeze::OnRender()
{
	LU_Log("OnRender: entry");
	if(g_Config.m_ClZzAutoUnfreezeSilent)
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(!m_FakeAngleSending || length(m_LastFireDir) < 0.001f)
		return;
	const int Dummy = g_Config.m_ClDummy;
	GameClient()->m_Controls.m_aTargetPos[Dummy] = GameClient()->m_LocalCharacterPos + m_LastFireDir * 150.0f;
}

void CLaserUnfreeze::OnPlayerInput(CNetObj_PlayerInput *pOut, CNetObj_PlayerInput *pPersistent)
{
	m_Claimed = false;
	m_FakeAngleSending = false;
	if(!IsActive())
		return;

	const int PredTick = Client()->PredGameTick(g_Config.m_ClDummy);

	if(HoldTurn(pOut, PredTick))
		return;

	int LastShot = m_LastFireTick;
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(GameClient()->m_Snap.m_aCharacters[LocalId].m_Cur.m_Weapon == WEAPON_LASER)
		LastShot = std::max(LastShot, GameClient()->m_Snap.m_aCharacters[LocalId].m_Cur.m_AttackTick);
	if(LastShot >= 0 && PredTick - LastShot < LU_RELOAD_TICKS)
		return;

	if(pOut->m_Hook != 0 && GameClient()->m_PredictedChar.m_HookState == HOOK_IDLE)
		return;

	if(GameClient()->m_PredictedChar.m_DeepFrozen || GameClient()->m_PredictedChar.m_FreezeEnd > PredTick)
		return;

	const int Look = std::clamp(g_Config.m_ClZzAutoUnfreezeTicks, 8, 120);
	const int Window = std::clamp(g_Config.m_ClZzAutoUnfreezeDirTicks, 1, 40);
	const int MaxBounces = std::clamp(g_Config.m_ClZzAutoUnfreezeMaxPoints, 1, 10);
	const int Len = Look + 1;

	static std::vector<vec2> s_vTraj;
	static std::vector<int> s_vFreeze;
	static std::vector<vec2> s_vOther;
	if(s_vTraj.size() < (size_t)Len)
		s_vTraj.resize((size_t)Len);
	if(s_vFreeze.size() < (size_t)Len)
		s_vFreeze.resize((size_t)Len);
	if(s_vOther.size() < (size_t)MAX_CLIENTS * (size_t)Len)
		s_vOther.resize((size_t)MAX_CLIENTS * (size_t)Len);
	std::vector<vec2> &vTraj = s_vTraj;
	std::vector<int> &vFreeze = s_vFreeze;
	std::vector<vec2> &vOther = s_vOther;

	const int OtherSim = std::min(LU_BOUNCE_TICKS * MaxBounces, Len);
	bool Deep = false;
	bool aOn[MAX_CLIENTS];
	const int FreezeIdx = PredictWorld(vTraj.data(), vFreeze.data(), vOther.data(), aOn, Len, Window, OtherSim, &Deep);
	if(Deep || FreezeIdx <= 0 || FreezeIdx > Window)
		return;

	int aActiveOthers[MAX_CLIENTS];
	int NumActiveOthers = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(aOn[i])
			aActiveOthers[NumActiveOthers++] = i;
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
		const int Idx = Seg * LU_BOUNCE_TICKS - 1;
		const int Lo = Idx - LU_JITTER;
		const int Hi = Idx + LU_JITTER;
		if(Lo < 1 || Hi >= Plan.m_Len)
			continue;
		if(Plan.m_FreezeIdx < 0 || Plan.m_FreezeIdx > Lo - 1)
			continue;
		if(!Sticks(Plan.m_pFreeze, Plan.m_Len, Lo))
			continue;
		Plan.m_MaxViableSeg = Seg;
	}
	if(Plan.m_MaxViableSeg < 0)
		return;

	vec2 Aim;
	if(pPersistent)
		Aim = vec2((float)pPersistent->m_TargetX, (float)pPersistent->m_TargetY);
	else
		Aim = vec2((float)pOut->m_TargetX, (float)pOut->m_TargetY);
	if(length(Aim) < 0.001f)
		Aim = vec2(0.0f, -1.0f);
	Aim = normalize(Aim);
	const bool FullCircle = g_Config.m_ClZzAutoUnfreezeFov >= 360;

	const float HalfFovCos = FullCircle ? 0.0f : std::cos(((float)g_Config.m_ClZzAutoUnfreezeFov * 0.5f) * (pi / 180.0f));

	const bool MostBounces = g_Config.m_ClZzAutoUnfreezeMostBounces != 0;
	const int CurDirTicks = std::clamp(g_Config.m_ClZzAutoUnfreezeCurDirTicks, 1, 40);

	int BestIdx = MostBounces ? -1 : (1 << 30);
	float BestMiss = 1e18f;
	vec2 BestDir = vec2(0.0f, 0.0f);
	int BestTx = 0;
	int BestTy = 0;
	bool HaveBest = false;

	const auto Consider = [&](vec2 CosSin) {
		int Tx, Ty;
		vec2 Dir;
		if(!QuantizeDir(CosSin, &Tx, &Ty, &Dir))
			return;
		if(!FullCircle && dot(Aim, Dir) < HalfFovCos)
			return;
		int HitIdx = 0;
		float Miss = 0.0f;
		int MaxUseful = (1 << 30);
		if(!MostBounces)
			MaxUseful = BestIdx;
		if(!TraceSelfShot(Dir, Plan, &HitIdx, &Miss, MaxUseful))
			return;

		bool Better = false;
		if(!HaveBest)
			Better = true;
		else if(MostBounces)
			Better = HitIdx > BestIdx;
		else
			Better = HitIdx < BestIdx;
		if(!Better && HaveBest && HitIdx == BestIdx && Miss < BestMiss)
			Better = true;
		if(Better)
		{
			HaveBest = true;
			BestIdx = HitIdx;
			BestMiss = Miss;
			BestDir = Dir;
			BestTx = Tx;
			BestTy = Ty;
		}
	};

	{
		int Tx, Ty;
		vec2 Dir;
		int HitIdx = 0;
		float Miss = 0.0f;
		bool Quantized = QuantizeDir(Aim, &Tx, &Ty, &Dir);
		bool Traced = false;
		if(Quantized)
			Traced = TraceSelfShot(Dir, Plan, &HitIdx, &Miss, CurDirTicks);
		if(Quantized && Traced && HitIdx <= CurDirTicks)
		{
			HaveBest = true;
			BestIdx = HitIdx;
			BestMiss = Miss;
			BestDir = Dir;
			BestTx = Tx;
			BestTy = Ty;
		}
	}

	if(!HaveBest)
	{
		const int Coarse = 512;
		const float Step = 2.0f * pi / (float)Coarse;

		static vec2 s_aCoarseDir[512];
		static bool s_CoarseReady = false;
		if(!s_CoarseReady)
		{
			for(int i = 0; i < Coarse; i++)
				s_aCoarseDir[i] = vec2(std::cos((float)i * Step), std::sin((float)i * Step));
			s_CoarseReady = true;
		}
		for(int i = 0; i < Coarse; i++)
			Consider(s_aCoarseDir[i]);
		if(!HaveBest)
			return;

		const float Base = std::atan2(BestDir.y, BestDir.x);
		for(int i = -16; i <= 16; i++)
		{
			const float Ang = Base + (float)i * (Step / 16.0f);
			Consider(vec2(std::cos(Ang), std::sin(Ang)));
		}
	}

	const vec2 SendDir = normalize(vec2((float)BestTx, (float)BestTy));

	vec2 PrevDir = vec2(0.0f, 0.0f);
	if(m_PlanTx || m_PlanTy)
		PrevDir = normalize(vec2((float)m_PlanTx, (float)m_PlanTy));
	bool SamePlan = false;
	if(m_PlanTick == PredTick - 1 && length(PrevDir) > 0.001f)
	{
		const float Dot = dot(SendDir, PrevDir);
		const float CosThreshold = std::cos(2.0f * (pi / 180.0f));
		if(Dot > CosThreshold)
		{
			const int Diff = FreezeIdx - m_PlanFreezeIdx;
			const int AbsDiff = Diff < 0 ? -Diff : Diff;
			if(AbsDiff <= 1)
				SamePlan = true;
		}
	}
	if(SamePlan)
		m_PlanStable = m_PlanStable + 1;
	else
		m_PlanStable = 1;
	m_PlanTx = BestTx;
	m_PlanTy = BestTy;
	m_PlanFreezeIdx = FreezeIdx;
	m_PlanTick = PredTick;

	if(m_PlanStable < LU_CONFIRM_TICKS && FreezeIdx > LU_COMMIT_NOW)
		return;

	Fire(pOut, pPersistent, BestTx, BestTy, PredTick);
}
