#include "plugin.h"
#include "IniReader/IniReader.h"
#include "../injector/assembly.hpp"
#include "CWorld.h"
#include "CGeneral.h"
#include "CTimer.h"
#include "CGame.h"
#include "CCutsceneMgr.h"
#include "CRunningScript.h"

using namespace plugin;
using namespace injector;
using namespace std;

// Generic

constexpr float normalizer = 50.0f / 30.0f;
constexpr float normalizerMult = 0.6f; // 1.6666... to 1.0

void asm_fmul(float f) { _asm {fmul dword ptr[f]} }
void asm_fdiv(float f) { _asm {fdiv dword ptr[f]} }
void asm_fld(float f) { _asm {fld  dword ptr[f]} }
void asm_fadd(float f) { _asm {fadd dword ptr[f]} }
void asm_fsub(float f) { _asm {fsub dword ptr[f]} }
void asm_fld_st1() { _asm {fld st(1)} }
void asm_fld_st2() { _asm {fld st(2)} }

struct MagicTimeStepFMUL
{
	void operator()(reg_pack& regs)
	{
		asm_fmul(CTimer::ms_fTimeStep / normalizer);
	}
};

struct MagicTimeStepFLD
{
	void operator()(reg_pack& regs)
	{
		asm_fld(CTimer::ms_fTimeStep / normalizer);
	}
};

// Swing comp add velocity fix

void _declspec(naked) asm_SwingCompAdd() {
	__asm {
		fld     dword ptr[esi + 14h] //m_fAngVel
		fmul    dword ptr ds : [0xB7CB5C] //CTimer::ms_fTimeStep
		fdiv    dword ptr[normalizer]
		mov     ecx, ebx //original

		push    0x6F4427
		ret
	}
}

// Swing comp damping fix

static const float fLossA = 0.08f; // 1.0f - 0.92f (firela ladder)
static const float fLossB = 0.03f; // 1.0f - 0.97f (other)
static const float fLossC = 0.02f; // 1.0f - 0.98f (boat)
static const float fOne = 1.0f;

void __declspec(naked) asm_SwingCompDampingA() {
	__asm {
		fld     dword ptr ds : [0xB7CB5C] //ms_fTimeStep
		fmul    dword ptr[normalizerMult]
		fmul    dword ptr[fLossA]
		fsubr   dword ptr[fOne]

		push    0x6F43A7
		ret
	}
}

void __declspec(naked) asm_SwingCompDampingB() {
	__asm {
		fld     dword ptr ds : [0xB7CB5C] //ms_fTimeStep
		fmul    dword ptr[normalizerMult]
		fmul    dword ptr[fLossB]
		fsubr   dword ptr[fOne]

		push    0x6F43D0
		ret
	}
}

void __declspec(naked) asm_SwingCompDampingC() {
	__asm {
		fld     dword ptr ds : [0xB7CB5C] //ms_fTimeStep
		fmul    dword ptr[normalizerMult]
		fmul    dword ptr[fLossC]
		fsubr   dword ptr[fOne]

		push    0x6F43D8
		ret
	}
}

void __declspec(naked) asm_SwingCompDampingD() {
	__asm {
		fld     dword ptr ds : [0xB7CB5C] //ms_fTimeStep
		fmul    dword ptr[normalizerMult]
		fmul    dword ptr[fLossB]
		fsubr   dword ptr[fOne]

		push    0x6F43D8
		ret
	}
}

// Per-frame effect emission fix
// Returns true with chance ms_fTimeStep / normalizer, so something emitted once per frame keeps the 30 fps rate on average.
// Random instead of accumulator because the same call site is shared by all peds. Own RNG to not change the game's rand() sequence.

static uint32_t emissionRngState = 0x9E3779B9;

static bool EmitThisFrame()
{
	float chance = CTimer::ms_fTimeStep / normalizer;
	if (chance >= 1.0f) return true;
	emissionRngState ^= emissionRngState << 13;
	emissionRngState ^= emissionRngState >> 17;
	emissionRngState ^= emissionRngState << 5;
	return (emissionRngState >> 8) * (1.0f / 16777216.0f) < chance;
}

// FxPrt_c::AddParticle (8 args)
void __fastcall AddParticlePerFrame(void* prt, int, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8)
{
	if (!EmitThisFrame()) return;
	((void(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t))0x4AA440)(prt, a1, a2, a3, a4, a5, a6, a7, a8);
}

// FxManager_c::CreateFxSystem (4 args), null result makes caller skip it
void* __fastcall CreateFxSystemPerFrame(void* fxManager, int, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
	if (!EmitThisFrame()) return nullptr;
	return ((void*(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t))0x4A9BE0)(fxManager, a1, a2, a3, a4);
}

/////////////////////////////////////

class FramerateVigilante
{
public:

	static inline int _fpsLimit;
	static inline int _lastFpsLimit;
	static inline int _refreshRate;
	static inline bool _firstlySetFPS;
	static inline bool _isOnPauseMenu;

#if defined(GTASA)
	static inline bool _wasInGym;
	static inline unsigned int _gymExitLimitStartTime;
	static constexpr unsigned int gymExitGracePeriodMs = 1000;

	static bool IsPlayerInGym()
	{
		auto* player = CWorld::Players[0].m_pPed;
		if (!player) return false;

		const CVector& pos = player->GetPosition();
		float gymX;
		float gymY;

		switch (CGame::currArea)
		{
		case 5: // Ganton Gym, Los Santos
			gymX = 768.0793f;
			gymY = 5.8606f;
			break;
		case 6: // Cobra Martial Arts Gym, San Fierro
			gymX = 774.0870f;
			gymY = -47.9830f;
			break;
		case 7: // Below the Belt Gym, Las Venturas
			gymX = 774.2430f;
			gymY = -76.0090f;
			break;
		default:
			return false;
		}

		// Interior IDs 5-7 are shared with other interiors, so also check
		// proximity to the actual gym interior instead of limiting all of them.
		const float dx = pos.x - gymX;
		const float dy = pos.y - gymY;
		return (dx * dx + dy * dy) < (80.0f * 80.0f);
	}
#endif

	union AutoLimitFPS {
		int flagsInt;
		struct {
			unsigned int forMissions : 1;
			unsigned int forMinigames : 1;
			unsigned int forSchools : 1;
			unsigned int forCutscenes : 1;
			unsigned int forScriptedCutscenes : 1;
			unsigned int forPauseMenu : 1;
			unsigned int forGyms : 1;
		} flags;
	};
	static inline AutoLimitFPS autoLimitFPS;

	FramerateVigilante()
	{
		/////////////////////////////////////

		_lastFpsLimit = 0;

		CIniReader ini("FramerateVigilante.ini");

		_fpsLimit = ini.ReadInteger("Settings", "FPSlimit", 0);

		// Disabled by default on ini; prefer SilentPatch
		_refreshRate = ini.ReadInteger("Settings", "RefreshRate", 0);

		autoLimitFPS.flagsInt = 0;
		autoLimitFPS.flags.forMissions = ini.ReadInteger("AutoLimitFPS", "ForMissions", 1);
		autoLimitFPS.flags.forMinigames = ini.ReadInteger("AutoLimitFPS", "ForMinigames", 1);
		autoLimitFPS.flags.forSchools = ini.ReadInteger("AutoLimitFPS", "ForSchools", 1);
		autoLimitFPS.flags.forCutscenes = ini.ReadInteger("AutoLimitFPS", "ForCutscenes", 1);
		autoLimitFPS.flags.forScriptedCutscenes = ini.ReadInteger("AutoLimitFPS", "ForScriptedCutscenes", 1);
		autoLimitFPS.flags.forPauseMenu = ini.ReadInteger("AutoLimitFPS", "ForPauseMenu", 1);
#if defined(GTASA)
		autoLimitFPS.flags.forGyms = ini.ReadInteger("AutoLimitFPS", "ForGyms", 1);
#endif

		// Run after. It fixes problems such as installing f92la in modloader while using handling patch.
		Events::initRwEvent += [] {

			if (_fpsLimit > 0) {
			#if defined(GTASA)
				WriteMemory<uint8_t>(0x53E94C, 0, true); //removes 14 ms frame delay
				WriteMemory<uint8_t>(0x619626, _fpsLimit, true);
				WriteMemory<uint8_t>(0xC1704C, _fpsLimit, false);
			#endif
			#if defined(GTAVC)
				WriteMemory<uint8_t>(0x602D68, _fpsLimit, true);
				WriteMemory<uint8_t>(0x9B48EC, _fpsLimit, false);
			#endif
			#if defined(GTA3)
				WriteMemory<uint8_t>(0x584C78, _fpsLimit, true);
				WriteMemory<uint8_t>(0x8F4374, _fpsLimit, false);
			#endif
			}

			if (_refreshRate > 0 && _refreshRate != 60) {
			#if defined(GTASA)
				WriteMemory<uint8_t>(0x74612A + 2, _refreshRate); //min hz
				//patch::RedirectCall(0x74631E, PatchedSetRefreshRate);
				//RwD3D9EngineSetRefreshRate(_refreshRate);
			#endif
			#if defined(GTAVC)
				//WriteMemory<uint8_t>(0x60105B + 2, _refreshRate); //min hz
				//patch::RedirectCall(0x600F66, PatchedSetRefreshRate);
				RwD3D8EngineSetRefreshRate(_refreshRate);
			#endif
			#if defined(GTA3)
				//WriteMemory<uint8_t>(0x581D55 + 2, _refreshRate); //min hz
				//patch::RedirectCall(0x581F46, PatchedSetRefreshRate);
				RwD3D8EngineSetRefreshRate(_refreshRate);
			#endif
			}

		#if defined(GTASA)

			// Swing Door (mainly CDoor::Process) fixes
			MakeJMP(0x6F4422, asm_SwingCompAdd, true);
			MakeJMP(0x6F43A1, asm_SwingCompDampingA, true); // firela
			MakeJMP(0x6F43CA, asm_SwingCompDampingB, true); // m_nDirn & 0x20
			MakeJMP(0x6F4391, asm_SwingCompDampingC, true); // boat
			MakeJMP(0x6F43D2, asm_SwingCompDampingD, true); // other

			struct AimingRifleWalkFix
			{
				void operator()(reg_pack& regs)
				{
					asm_fmul(0.07f / (CTimer::ms_fTimeStep / normalizer));
				}
			}; MakeInline<AimingRifleWalkFix>(0x61E0CA, 0x61E0CA + 6);


			// CTaskSimpleSwim::ProcessSwimmingResistance fixes
			// The target velocity is built from m_vecAnimMovingShiftLocal, which is a per-frame distance (walking divides it by ms_fTimeStep, swimming doesn't).
			// So the whole target (x, y, z) is converted to speed here; constants added to it per swim state are pre-multiplied by ts/normalizer to cancel this.
			struct SwimSpeedFix
			{
				void operator()(reg_pack& regs)
				{
					float scale = 1.0f / (CTimer::ms_fTimeStep / normalizer);
					*(float*)(regs.esp + 0x1C) *= scale; // x * (1 - p)
					*(float*)(regs.esp + 0x20) *= scale; // y * (1 - p)
					asm_fmul(scale); // ST(0) = z * (1 - p)

					float f = *(float*)(regs.eax + 0x00);
					asm_fld_st1();
					asm_fmul(f);
					asm_fld_st2();
				}
			}; MakeInline<SwimSpeedFix>(0x68A50E, 0x68A50E + 6);


			// cBuoyancy::CalcBuoyancyForce: buoyancy is cut when mass * m_vecMoveSpeed.z > force * 4.0, but force is per frame (* ms_fTimeStep) and momentum isn't,
			// so rising speed was capped lower at high FPS. Compare against the 30 fps force.
			struct BuoyancyCutoffFix
			{
				void operator()(reg_pack& regs)
				{
					asm_fmul(4.0f / (CTimer::ms_fTimeStep / normalizer));
				}
			}; MakeInline<BuoyancyCutoffFix>(0x6C27C2, 0x6C27C2 + 6);


			// CTaskSimpleSwim::ProcessEffects emits every frame
			MakeCALL(0x68ADE2, AddParticlePerFrame, true); // surface sprint wake
			MakeCALL(0x68AD31, AddParticlePerFrame, true); // underwater bubbles
			MakeCALL(0x68AEBA, CreateFxSystemPerFrame, true); // sprinting "water_swim" splashes on 4 bones (+ its audio event)
			MakeCALL(0x68AF15, CreateFxSystemPerFrame, true);
			MakeCALL(0x68AF66, CreateFxSystemPerFrame, true);
			MakeCALL(0x68AFB3, CreateFxSystemPerFrame, true);


			// Dive z is a constant speed (anim progress * -0.1), cancel SwimSpeedFix scaling
			struct DiveFix
			{
				void operator()(reg_pack& regs)
				{
					float f = -0.1f * (CTimer::ms_fTimeStep / normalizer);
					asm_fmul(f);
				}
			}; MakeInline<DiveFix>(0x68A42B, 0x68A42B + 6);


			// Underwater sprint z: anim part is scaled by SwimSpeedFix, the +0.01 come-to-surface speed must not be
			struct DiveSprintComeToSurfaceFix
			{
				void operator()(reg_pack& regs)
				{
					asm_fadd(0.01f * (CTimer::ms_fTimeStep / normalizer));
				}
			}; MakeInline<DiveSprintComeToSurfaceFix>(0x68A4CA, 0x68A4CA + 6);


			// Surface hold: velocity towards water level is clamped to ms_fTimeStep * 0.1, make it the 30 fps value
			struct SwimSurfaceSpeedLimitFix
			{
				void operator()(reg_pack& regs)
				{
					asm_fld(normalizer); // * 0.1 by original code
				}
			}; MakeInline<SwimSurfaceSpeedLimitFix>(0x68A7E6, 0x68A7E6 + 6);


			// Dive pitch rate damping (*= 0.95 per frame)
			struct SwimPitchDampingFix
			{
				void operator()(reg_pack& regs)
				{
					asm_fmul(powf(0.95f, CTimer::ms_fTimeStep / normalizer));
				}
			};
			MakeInline<SwimPitchDampingFix>(0x68A6BD, 0x68A6BD + 6);
			MakeInline<SwimPitchDampingFix>(0x68A735, 0x68A735 + 6);
			MakeInline<SwimPitchDampingFix>(0x68A7BD, 0x68A7BD + 6);

			struct SkimmerResistanceFix
			{
				void operator()(reg_pack& regs)
				{
					float f = 30.0f * (CTimer::ms_fTimeStep / normalizer);
					asm_fmul(f);
				}
			}; MakeInline<SkimmerResistanceFix>(0x6D2771, 0x6D2771 + 6);

		#endif // defined(GTASA)


		#if defined(GTAVC)
			struct SkimmerResistanceFixVC
			{
				void operator()(reg_pack& regs)
				{
					float f = 30.0f * (CTimer::ms_fTimeStep / magic);
					asm_fld(f);
				}
			}; MakeInline<SkimmerResistanceFixVC>(0x59FB69, 0x59FB69 + 6);
		#endif defined(GTAVC)


		#if defined(GTASA)
			struct CarWheelOnRailsSpinFix1
			{
				void operator()(reg_pack& regs)
				{
					asm_fmul(CTimer::ms_fTimeStep);
					asm_fadd(*(float*)(regs.esi + 0x828));
				}
			}; MakeInline<CarWheelOnRailsSpinFix1>(0x6B523F, 0x6B523F + 6);

			struct CarWheelOnRailsSpinFix2
			{
				void operator()(reg_pack& regs)
				{
					asm_fmul(CTimer::ms_fTimeStep);
					asm_fadd(*(float*)(regs.esi + 0x82C));
				}
			}; MakeInline<CarWheelOnRailsSpinFix2>(0x6B524F, 0x6B524F + 6);

			struct CarWheelOnRailsSpinFix3
			{
				void operator()(reg_pack& regs)
				{
					asm_fmul(CTimer::ms_fTimeStep);
					asm_fadd(*(float*)(regs.esi + 0x830));
				}
			}; MakeInline<CarWheelOnRailsSpinFix3>(0x6B525D, 0x6B525D + 6);

			struct CarWheelOnRailsSpinFix4
			{
				void operator()(reg_pack& regs)
				{
					asm_fmul(CTimer::ms_fTimeStep);
					asm_fadd(*(float*)(regs.esi + 0x834));
				}
			}; MakeInline<CarWheelOnRailsSpinFix4>(0x6B5269, 0x6B5269 + 6);

		#endif // defined(GTASA)


			// CarWheelOnRailsSpinFix III VC
		#if defined(GTA3)
			MakeInline<MagicTimeStepFMUL>(0x5512D2, 0x5512D2 + 6);
		#endif defined(GTA3)

		#if defined(GTAVC)
			MakeInline<MagicTimeStepFMUL>(0x5BA952, 0x5BA952 + 6);
		#endif defined(GTAVC)


			// Burnout
		#if defined(GTASA)
			struct BurnoutFix
			{
				void operator()(reg_pack& regs)
				{
					float f = 3000.0f * (CTimer::ms_fTimeStep / normalizer);
					asm_fld(f);
				}
			};
			MakeInline<BurnoutFix>(0x6A4FE6, 0x6A4FE6 + 6);
		#endif defined(GTASA)

			struct CarSlowDownSpeedFix
			{
				void operator()(reg_pack& regs)
				{
					float f = 0.9f * (CTimer::ms_fTimeStep / normalizer);
					asm_fld(f);
				}
			};

			struct CarSlowDownSpeedFixMul
			{
				void operator()(reg_pack& regs)
				{
					float f = 0.9f * (CTimer::ms_fTimeStep / normalizer);
					asm_fmul(f);
				}
			};


		#if defined(GTASA)
			MakeInline<CarSlowDownSpeedFix>(0x6D6E69, 0x6D6E69 + 6);
			MakeInline<CarSlowDownSpeedFix>(0x6D6EA8, 0x6D6EA8 + 6);
			MakeInline<CarSlowDownSpeedFix>(0x6D767F, 0x6D767F + 6);
			MakeInline<CarSlowDownSpeedFix>(0x6D76AB, 0x6D76AB + 6);
			MakeInline<CarSlowDownSpeedFix>(0x6D76CD, 0x6D76CD + 6);
		#endif

		#if defined(GTAVC)
			MakeInline<CarSlowDownSpeedFixMul>(0x5BA392, 0x5BA392 + 6);
			MakeInline<CarSlowDownSpeedFixMul>(0x5BA3C3, 0x5BA3C3 + 6);
			MakeInline<CarSlowDownSpeedFixMul>(0x5BA3E5, 0x5BA3E5 + 6);
			MakeInline<CarSlowDownSpeedFix>(0x5BA3F0, 0x5BA3F0 + 6);
			MakeInline<CarSlowDownSpeedFixMul>(0x5B9AD2, 0x5B9AD2 + 6);
			MakeInline<CarSlowDownSpeedFixMul>(0x5B9B03, 0x5B9B03 + 6);
			MakeInline<CarSlowDownSpeedFixMul>(0x5B9B25, 0x5B9B25 + 6);
			MakeInline<CarSlowDownSpeedFix>(0x5B9B30, 0x5B9B30 + 6);
		#endif

		#if defined(GTA3)
			MakeInline<CarSlowDownSpeedFixMul>(0x5515ED, 0x5515ED + 6);
			MakeInline<CarSlowDownSpeedFix>(0x551600, 0x551600 + 6);
		#endif


			static unsigned int hornPressLastTime = 0;
			static bool hornHasPressed = false;
			static bool hornJustUp = false;

			struct SirenTurnOnFix
			{
				void operator()(reg_pack& regs)
				{
					CPad* pad;
					CVehicle* vehicle = (CVehicle*)regs.esi;

				#if defined(GTASA)
					// Bonus: fix second player unable to toggle siren
					pad = vehicle->m_pDriver == CWorld::Players[0].m_pPed ? CPad::GetPad(0) : CPad::GetPad(1);
				#else
					pad = CPad::GetPad(0);
				#endif

					// Store horn state
					if (pad->HornJustDown()) {
						hornPressLastTime = CTimer::m_snTimeInMilliseconds;
						hornHasPressed = true;
					}
					if (!pad->GetHorn()) { hornJustUp = hornHasPressed ? true : false; }
					else { hornJustUp = false; }

					// Select final mode
					uint32_t returnAddress;
					if (pad->GetHorn() && CTimer::m_snTimeInMilliseconds - hornPressLastTime >= 150) {
						// horn return
					#if defined(GTASA)
						returnAddress = 0x6E09E8;
					#endif
					#if defined(GTAVC)
						returnAddress = 0x597B39;
					#endif
					#if defined(GTA3)
						returnAddress = 0x534169;
					#endif
					}
					else if (hornJustUp && CTimer::m_snTimeInMilliseconds - hornPressLastTime < 150) {
						hornJustUp = false;
						hornHasPressed = false;
						// toggle siren return
					#if defined(GTASA)
						returnAddress = 0x6E0999;
					#endif
					#if defined(GTAVC)
						returnAddress = 0x597AB5;
					#endif
					#if defined(GTA3)
						returnAddress = 0x5340EB;
					#endif
					}
					else {
						// no horn return
					#if defined(GTASA)
						returnAddress = 0x6E09F7;
					#endif
					#if defined(GTAVC)
						returnAddress = 0x597AE0;
					#endif
					#if defined(GTA3)
						returnAddress = 0x534113;
					#endif
					}
					*(uint32_t*)(regs.esp - 0x4) = returnAddress;
				}
			};
		#if defined(GTASA)
			MakeInline<SirenTurnOnFix>(0x006E0961);
		#endif
		#if defined(GTAVC)
			MakeInline<SirenTurnOnFix>(0x00597A58);
		#endif
		#if defined(GTA3)
			MakeInline<SirenTurnOnFix>(0x00534092);
		#endif


		#if defined(GTASA)
			struct HeliRotorIncreaseSpeedA
			{
				void operator()(reg_pack& regs)
				{
					float* rotorFinalSpeed = ReadMemory<float*>(0x006C4EFE + 2, false); // MixSets adaptation
					float f = ((*rotorFinalSpeed / 220.0f) * 3.0f) * (CTimer::ms_fTimeStep / normalizer);
					asm_fadd(f);
				}
			}; MakeInline<HeliRotorIncreaseSpeedA>(0x6C4F37, 0x6C4F37 + 6);


			struct HeliRotorIncreaseSpeedB
			{
				void operator()(reg_pack& regs)
				{
					float* rotorFinalSpeed = ReadMemory<float*>(0x006C4EFE + 2, false); // MixSets adaptation
					float f = (*rotorFinalSpeed / 220.0f) * (CTimer::ms_fTimeStep / normalizer);
					asm_fadd(f);
				}
			}; MakeInline<HeliRotorIncreaseSpeedB>(0x6C4F29, 0x6C4F29 + 6);
		#endif


		#if defined(GTAVC)
			struct HeliRotorIncreaseSpeedVCAdd
			{
				void operator()(reg_pack& regs)
				{
					float* rotorFinalSpeed = ReadMemory<float*>(0x005AF238 + 2, true); // MixSets adaptation
					float f = (*rotorFinalSpeed / 13.0f) * (CTimer::ms_fTimeStep / magic);
					asm_fadd(f);
				}
			}; MakeInline<HeliRotorIncreaseSpeedVCAdd>(0x5AF226, 0x5AF226 + 6);

			struct HeliRotorIncreaseSpeedVCSub
			{
				void operator()(reg_pack& regs)
				{
					float* rotorFinalSpeed = ReadMemory<float*>(0x005AF238 + 2, true); // MixSets adaptation
					float f = *rotorFinalSpeed * (CTimer::ms_fTimeStep / magic);
					asm_fsub(f);
				}
			}; MakeInline<HeliRotorIncreaseSpeedVCSub>(0x5AF24B, 0x5AF24B + 6);
		#endif 

		#if defined(GTASA)
			struct PedPushCarForce
			{
				void operator()(reg_pack& regs)
				{
					*(float*)(regs.esp + 0xB0 - 0x90 + 0x0) *= (CTimer::ms_fTimeStep / normalizer);
					*(float*)(regs.esp + 0xB0 - 0x90 + 0x4) *= (CTimer::ms_fTimeStep / normalizer);
					*(float*)(regs.esp + 0xB0 - 0x90 + 0x8) *= (CTimer::ms_fTimeStep / normalizer);
					regs.edx = *(uint32_t*)(regs.esp + 0xB0 - 0x90 + 0x0); //mov     eax, [esp+0B0h+out_result.y]
					regs.eax = *(uint32_t*)(regs.esp + 0xB0 - 0x90 + 0x4); //mov     eax, [esp+0B0h+out_result.y]
				}
			}; MakeInline<PedPushCarForce>(0x549652, 0x549652 + 8);
		#endif

		#if defined(GTASA)
			if (autoLimitFPS.flagsInt != 0) {
				Events::processScriptsEvent += []() {

					// Just to make sure to use the value defined on ini (not crucial but prefered)
					if (!_firstlySetFPS && _fpsLimit > 0) {
						WriteMemory<uint8_t>(0x619626, _fpsLimit, true);
						WriteMemory<uint8_t>(0xC1704C, _fpsLimit, false);
						_firstlySetFPS = true;
					}

					if (_isOnPauseMenu) {
						if (_lastFpsLimit != 0) {
							WriteMemory<uint8_t>(0xC1704C, _lastFpsLimit, false);
							_lastFpsLimit = 0;
						}
						_isOnPauseMenu = false;
					}

					// Auto limit FPS on specific game cases
					int preferableFpsLimit = 0;

					// Gym exits can immediately retrigger the entrance at high FPS while
					// the exterior is still streaming. Keep 30 FPS inside the three gyms
					// and for a short grace period after returning to the outside world.
					bool keepGymLimit = false;
					if (autoLimitFPS.flags.forGyms) {
						const bool isInGym = IsPlayerInGym();
						if (isInGym) {
							_wasInGym = true;
							keepGymLimit = true;
						}
						else if (_wasInGym) {
							_wasInGym = false;
							_gymExitLimitStartTime = CTimer::m_snTimeInMilliseconds;
							keepGymLimit = true;
						}
						else if (_gymExitLimitStartTime != 0 &&
							CTimer::m_snTimeInMilliseconds - _gymExitLimitStartTime < gymExitGracePeriodMs) {
							keepGymLimit = true;
						}
					}

					if (keepGymLimit) {
						preferableFpsLimit = 30;
					}
					else if (CCutsceneMgr::ms_running) {
						if (autoLimitFPS.flags.forCutscenes) preferableFpsLimit = 60;
					}
					else if (TheCamera.m_bWideScreenOn) // Scene borders, used for scripted scenes
					{
						if (autoLimitFPS.flags.forScriptedCutscenes) preferableFpsLimit = 80; // No issues reported, but avoid too high FPS for safety reasons.
					}
					else if (_isOnPauseMenu) // Scene borders, used for scripted scenes
					{
						preferableFpsLimit = 60;
					}
					else
					{
						CRunningScript** activeThreadQueue = (CRunningScript**)ReadMemory<CRunningScript*>(0x468D76, true); //make sure is using updated pointer, compatibility for limit adjusters
						for (auto script = *activeThreadQueue; script; script = script->m_pNext)
						{
							// Minigames
							if (autoLimitFPS.flags.forMinigames && _stricmp("POOL2", script->m_szName) == 0) {
								preferableFpsLimit = 30;
							}
							else if (autoLimitFPS.flags.forMinigames && _stricmp("GFSEX", script->m_szName) == 0) {
								preferableFpsLimit = 30;
							}
							// Missions
							else if (autoLimitFPS.flags.forMissions && _stricmp("DRUGS1", script->m_szName) == 0) {
								// Big Smoke sometimes stops walking, causing a lock.
								if (CGame::currArea != 0) {
									preferableFpsLimit = 50;
								}
							}
							// Schools (still need to fix more vehicle physics issues, but I (Junior_Djjr) managed to get gold in all of them at 60 FPS, so I see no reason to use anything lower, but still, avoid too high FPS for safety reasons)
							else if (autoLimitFPS.flags.forSchools && _stricmp("DSKOOL", script->m_szName) == 0) {
								preferableFpsLimit = 80;
							}
							else if (autoLimitFPS.flags.forSchools && _stricmp("BOAT", script->m_szName) == 0) {
								preferableFpsLimit = 80;
							}
							else if (autoLimitFPS.flags.forSchools && _stricmp("BSKOOL", script->m_szName) == 0) {
								preferableFpsLimit = 80;
							}
						}
					}
					if (preferableFpsLimit != 0) {
						if (_lastFpsLimit == 0) {
							_lastFpsLimit = ReadMemory<uint8_t>(0xC1704C, false);
						}
						preferableFpsLimit = _lastFpsLimit < preferableFpsLimit ? _lastFpsLimit : preferableFpsLimit;
						WriteMemory<uint8_t>(0xC1704C, preferableFpsLimit, false);
					}
					else {
						if (_lastFpsLimit != 0) {
							WriteMemory<uint8_t>(0xC1704C, _lastFpsLimit, false);
							_lastFpsLimit = 0;
						}
					}
				}; //end of processScriptsEvent

				if (autoLimitFPS.flags.forPauseMenu) {
					Events::drawMenuBackgroundEvent += [] {
						_isOnPauseMenu = true;
						if (_lastFpsLimit == 0) {
							_lastFpsLimit = ReadMemory<uint8_t>(0xC1704C, false);
						}
						WriteMemory<uint8_t>(0xC1704C, 60, false);
					};
				}
			}
		#endif

		}; //end of init

	}
} framerateVigilante;