#include "plugin.h"
#include "../injector/assembly.hpp"
#include "CWorld.h"
#include "CTimer.h"

using namespace plugin;
using namespace injector;

class FramerateVigilante
{
public:
    FramerateVigilante()
	{
		static constexpr float magic = 50.0f / 30.0f;


	#if defined(GTASA)

		struct SwimSpeedFix
		{
			void operator()(reg_pack& regs)
			{
				*(float*)(regs.esp + 0x1C) *= 1.0f / (CTimer::ms_fTimeStep / magic);
				*(float*)(regs.esp + 0x20) *= 1.0f / (CTimer::ms_fTimeStep / magic);
				*(float*)(regs.esp + 0x18) *= 1.0f / (CTimer::ms_fTimeStep / magic);

				float f = *(float*)(regs.eax + 0x00);
				_asm {fld st(1)}
				_asm {fmul dword ptr[f]}
				_asm {fld st(2)}
			}
		}; MakeInline<SwimSpeedFix>(0x68A50E, 0x68A50E + 6);


		struct BuoyancyFix
		{
			void operator()(reg_pack& regs)
			{
				float f = (1.0f + ((CTimer::ms_fTimeStep / magic) / 1.5f)) * (CTimer::ms_fTimeStep / magic);
				_asm {fmul dword ptr[f]}
			}
		}; MakeInline<BuoyancyFix>(0x6C27AE, 0x6C27AE + 6);


		struct DiveFix
		{
			void operator()(reg_pack& regs)
			{
				float f = -0.1f * (CTimer::ms_fTimeStep / magic);
				_asm {fmul dword ptr[f]}
			}
		}; MakeInline<DiveFix>(0x68A42B, 0x68A42B + 6);

	#endif // defined(GTASA)



		struct CarSlowDownSpeedFix
		{
			void operator()(reg_pack& regs)
			{
				float f = 0.9f * (CTimer::ms_fTimeStep / magic);
				_asm {fld dword ptr[f]}
			}
		};
		struct CarSlowDownSpeedFixMul
		{
			void operator()(reg_pack& regs)
			{
				float f = 0.9f * (CTimer::ms_fTimeStep / magic);
				_asm {fmul dword ptr[f]}
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
				CPad *pad;
				CVehicle *vehicle = (CVehicle *)regs.esi;

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
				#if defined(GTASA)
					returnAddress = 0x6E09E8; // horn return
				#endif
				#if defined(GTAVC)
					returnAddress = 0x597B39; // horn return
				#endif
				#if defined(GTA3)
					returnAddress = 0x534169; // horn return
				#endif
				}
				else if (hornJustUp && CTimer::m_snTimeInMilliseconds - hornPressLastTime < 150) {
					hornJustUp = false;
					hornHasPressed = false;
				#if defined(GTASA)
					returnAddress = 0x6E0999; // toggle siren return
				#endif
				#if defined(GTAVC)
					returnAddress = 0x597AB5; // toggle siren return
				#endif
				#if defined(GTA3)
					returnAddress = 0x5340EB; // toggle siren return
				#endif
				}
				else {
				#if defined(GTASA)
					returnAddress = 0x6E09F7; // no horn return
				#endif
				#if defined(GTAVC)
					returnAddress = 0x597AE0; // no horn return
				#endif
				#if defined(GTA3)
					returnAddress = 0x534113; // no horn return
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
				float *rotorFinalSpeed = ReadMemory<float*>(0x006C4EFE + 2, false); // MixSets adaptation
				float f = ((*rotorFinalSpeed / 220.0f) * 3.0f) * (CTimer::ms_fTimeStep / magic);
				_asm {fadd dword ptr[f]}
			}
		}; MakeInline<HeliRotorIncreaseSpeedA>(0x6C4F37, 0x6C4F37 + 6);


		struct HeliRotorIncreaseSpeedB
		{
			void operator()(reg_pack& regs)
			{
				float *rotorFinalSpeed = ReadMemory<float*>(0x006C4EFE + 2, false); // MixSets adaptation
				float f = (*rotorFinalSpeed / 220.0f) * (CTimer::ms_fTimeStep / magic);
				_asm {fadd dword ptr[f]}
			}
		}; MakeInline<HeliRotorIncreaseSpeedB>(0x6C4F29, 0x6C4F29 + 6);
	#endif


    }
} framerateVigilante;
