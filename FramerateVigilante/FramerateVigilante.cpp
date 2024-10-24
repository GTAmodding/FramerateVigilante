#include "plugin.h"
#include "IniReader/IniReader.h"
#include "../injector/assembly.hpp"
#include "CWorld.h"
#include "CTimer.h"
#include "CRunningScript.h"

using namespace plugin;
using namespace injector;
using namespace std;

class FramerateVigilante
{
public:

	static void asm_fmul(float f) { _asm {fmul dword ptr[f]} }
	static void asm_fdiv(float f) { _asm {fdiv dword ptr[f]} }
	static void asm_fld(float f) { _asm {fld  dword ptr[f]} }
	static void asm_fadd(float f) { _asm {fadd dword ptr[f]} }
	static void asm_fsub(float f) { _asm {fsub dword ptr[f]} }
	static void asm_fld_st1() { _asm {fld st(1)} }
	static void asm_fld_st2() { _asm {fld st(2)} }

	static inline int _fpsLimit;
	static inline int _lastFpsLimit;
	static inline int _refreshRate;
	static inline bool _autoLimitFPS;

	union {
		int _isOnFlagsInt;
		struct {
			unsigned int poolGame : 1;
			unsigned int drivingSchool : 1;
			unsigned int boatSchool : 1;
			unsigned int bikeSchool : 1;
			unsigned int gfSex : 1;
		} _isOnFlags;
	};

	FramerateVigilante()
	{
		/////////////////////////////////////
		
		// Generic

		static constexpr float magic = 50.0f / 30.0f;

		struct MagicTimeStepFMUL
		{
			void operator()(reg_pack& regs)
			{
				asm_fmul(CTimer::ms_fTimeStep / magic);
			}
		};

		struct MagicTimeStepFLD
		{
			void operator()(reg_pack& regs)
			{
				asm_fld(CTimer::ms_fTimeStep / magic);
			}
		};

		/////////////////////////////////////

		// Run after. It fixes problems such as installing f92la in modloader while using handling patch.
		Events::initRwEvent += [] {

			_lastFpsLimit = 0;
			
			CIniReader ini("FramerateVigilante.ini");
			_fpsLimit = ini.ReadInteger("Settings", "FPSlimit", 0);

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

			_refreshRate = ini.ReadInteger("Settings", "RefreshRate", 0);

			if (_refreshRate > 0 && _refreshRate != 60) {
			#if defined(GTASA)
				//WriteMemory<uint8_t>(0x74612A + 2, _refreshRate); //min hz
				//patch::RedirectCall(0x74631E, PatchedSetRefreshRate);
				RwD3D9EngineSetRefreshRate(_refreshRate);
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

			_autoLimitFPS = ini.ReadInteger("Settings", "AutoLimitFPS", 0);


			struct AimingRifleWalkFix
			{
				void operator()(reg_pack& regs)
				{
					asm_fmul(0.07f / (CTimer::ms_fTimeStep / magic));
				}
			}; MakeInline<AimingRifleWalkFix>(0x61E0CA, 0x61E0CA + 6);


			struct SwimSpeedFix
			{
				void operator()(reg_pack& regs)
				{
					*(float*)(regs.esp + 0x1C) *= 1.0f / (CTimer::ms_fTimeStep / magic);
					*(float*)(regs.esp + 0x20) *= 1.0f / (CTimer::ms_fTimeStep / magic);
					*(float*)(regs.esp + 0x18) *= 1.0f / (CTimer::ms_fTimeStep / magic);

					float f = *(float*)(regs.eax + 0x00);
					asm_fld_st1();
					asm_fmul(f);
					asm_fld_st2();
				}
			}; MakeInline<SwimSpeedFix>(0x68A50E, 0x68A50E + 6);


			struct BuoyancySpeedFix
			{
				void operator()(reg_pack& regs)
				{
					float f = CTimer::ms_fTimeStep;
					CPhysical* physical = (CPhysical*)regs.eax;
					if (physical->m_nType == eEntityType::ENTITY_TYPE_PED)
					{
						CPed* ped = (CPed*)regs.eax;
						if (ped->IsPlayer()) // we only need this for player, due to swim bug
						{
							f = (1.0f + ((CTimer::ms_fTimeStep / magic) / 1.5f)) * (CTimer::ms_fTimeStep / magic);
						}
					}
					asm_fmul(f);
				}
			}; MakeInline<BuoyancySpeedFix>(0x6C27AE, 0x6C27AE + 6);


			struct DiveFix
			{
				void operator()(reg_pack& regs)
				{
					float f = -0.1f * (CTimer::ms_fTimeStep / magic);
					asm_fmul(f);
				}
			}; MakeInline<DiveFix>(0x68A42B, 0x68A42B + 6);


			struct DiveSprintComeToSurfaceFix
			{
				void operator()(reg_pack& regs)
				{
					float f = 1.0f / (CTimer::ms_fTimeStep / magic);
					asm_fadd(0.01f);
					asm_fmul(f);
				}
			}; MakeInline<DiveSprintComeToSurfaceFix>(0x68A4CA, 0x68A4CA + 6);

			struct SkimmerResistanceFix
			{
				void operator()(reg_pack& regs)
				{
					float f = 30.0f * (CTimer::ms_fTimeStep / magic);
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


			struct CarSlowDownSpeedFix
			{
				void operator()(reg_pack& regs)
				{
					float f = 0.9f * (CTimer::ms_fTimeStep / magic);
					asm_fld(f);
				}
			};

			struct CarSlowDownSpeedFixMul
			{
				void operator()(reg_pack& regs)
				{
					float f = 0.9f * (CTimer::ms_fTimeStep / magic);
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
					float f = ((*rotorFinalSpeed / 220.0f) * 3.0f) * (CTimer::ms_fTimeStep / magic);
					asm_fadd(f);
				}
			}; MakeInline<HeliRotorIncreaseSpeedA>(0x6C4F37, 0x6C4F37 + 6);


			struct HeliRotorIncreaseSpeedB
			{
				void operator()(reg_pack& regs)
				{
					float* rotorFinalSpeed = ReadMemory<float*>(0x006C4EFE + 2, false); // MixSets adaptation
					float f = (*rotorFinalSpeed / 220.0f) * (CTimer::ms_fTimeStep / magic);
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
					*(float*)(regs.esp + 0xB0 - 0x90 + 0x0) *= (CTimer::ms_fTimeStep / magic);
					*(float*)(regs.esp + 0xB0 - 0x90 + 0x4) *= (CTimer::ms_fTimeStep / magic);
					*(float*)(regs.esp + 0xB0 - 0x90 + 0x8) *= (CTimer::ms_fTimeStep / magic);
					regs.edx = *(uint32_t*)(regs.esp + 0xB0 - 0x90 + 0x0); //mov     eax, [esp+0B0h+out_result.y]
					regs.eax = *(uint32_t*)(regs.esp + 0xB0 - 0x90 + 0x4); //mov     eax, [esp+0B0h+out_result.y]
				}
			}; MakeInline<PedPushCarForce>(0x549652, 0x549652 + 8);
		#endif

		}; //end of init

	#if defined(GTASA)
		if (_autoLimitFPS) {
			Events::processScriptsEvent += []() {

				// Consider submissions/minigames
				framerateVigilante._isOnFlagsInt = 0; //reset
				CRunningScript** activeThreadQueue = (CRunningScript**)0x00A8B42C;
				for (auto script = *activeThreadQueue; script; script = script->m_pNext)
				{
					if (_stricmp("POOL2", script->m_szName) == 0) {
						framerateVigilante._isOnFlags.poolGame = true;
					}
					else if (_stricmp("DSKOOL", script->m_szName) == 0) {
						framerateVigilante._isOnFlags.drivingSchool = true;
					}
					else if (_stricmp("BOAT", script->m_szName) == 0) {
						framerateVigilante._isOnFlags.boatSchool = true;
					}
					else if (_stricmp("BSKOOL", script->m_szName) == 0) {
						framerateVigilante._isOnFlags.bikeSchool = true;
					}
					else if (_stricmp("GFSEX", script->m_szName) == 0) {
						framerateVigilante._isOnFlags.gfSex = true;
					}
				}
				if (framerateVigilante._isOnFlagsInt != 0) {
					if (_lastFpsLimit == 0) {
						_lastFpsLimit = ReadMemory<uint8_t>(0xC1704C, false);
					}
					WriteMemory<uint8_t>(0xC1704C, 30, false);
				}
				else {
					if (_lastFpsLimit != 0) {
						WriteMemory<uint8_t>(0xC1704C, _lastFpsLimit, false);
						_lastFpsLimit = 0;
					}
				}

			}; //end of processScriptsEvent
		}
	#endif

	}
} framerateVigilante;