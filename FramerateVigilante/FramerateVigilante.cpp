#include "plugin.h"
#include "../injector/assembly.hpp"
#include "CWorld.h"
#include "CTimer.h"

using namespace plugin;

class FramerateVigilante
{
public:
    FramerateVigilante()
	{
		static constexpr float magic = 50.0f / 30.0f; // only sa?

	#ifdef GTASA

		struct SwimSpeedFix
		{
			void operator()(injector::reg_pack& regs)
			{
				*(float*)(regs.esp + 0x1C) *= 1.0f / (CTimer::ms_fTimeStep / magic);
				*(float*)(regs.esp + 0x20) *= 1.0f / (CTimer::ms_fTimeStep / magic);
				*(float*)(regs.esp + 0x18) *= 1.0f / (CTimer::ms_fTimeStep / magic);

				float f = *(float*)(regs.eax + 0x00);
				_asm {fld st(1)}
				_asm {fmul dword ptr[f]}
				_asm {fld st(2)}
			}
		}; injector::MakeInline<SwimSpeedFix>(0x68A50E, 0x68A50E + 6);


		struct BuoyancyFix
		{
			void operator()(injector::reg_pack& regs)
			{
				float f = (1.0f + ((CTimer::ms_fTimeStep / magic) / 1.5f)) * (CTimer::ms_fTimeStep / magic);
				_asm {fmul dword ptr[f]}
			}
		}; injector::MakeInline<BuoyancyFix>(0x6C27AE, 0x6C27AE + 6);


		struct DiveFix
		{
			void operator()(injector::reg_pack& regs)
			{
				float f = -0.1f * (CTimer::ms_fTimeStep / magic);
				_asm {fmul dword ptr[f]}
			}
		}; injector::MakeInline<DiveFix>(0x68A42B, 0x68A42B + 6);


		struct CarSlowDownSpeedFix
		{
			void operator()(injector::reg_pack& regs)
			{
				float f = 0.9f * (CTimer::ms_fTimeStep / magic);
				_asm {fld dword ptr[f]}
			}
		};
		injector::MakeInline<CarSlowDownSpeedFix>(0x6D6E69, 0x6D6E69 + 6);
		injector::MakeInline<CarSlowDownSpeedFix>(0x6D6EA8, 0x6D6EA8 + 6);
		injector::MakeInline<CarSlowDownSpeedFix>(0x6D767F, 0x6D767F + 6);
		injector::MakeInline<CarSlowDownSpeedFix>(0x6D76AB, 0x6D76AB + 6);
		injector::MakeInline<CarSlowDownSpeedFix>(0x6D76CD, 0x6D76CD + 6);


		static unsigned int hornPressLastTime = 0;
		static bool hornHasPressed = false;
		static bool hornJustUp = false;

		struct SirenTurnOnFix
		{
			void operator()(injector::reg_pack& regs)
			{
				CPad *pad;
				CVehicle *vehicle = (CVehicle *)regs.esi;

				// Bonus: fix second player unable to toggle siren
				pad = vehicle->m_pDriver == CWorld::Players[0].m_pPed ? CPad::GetPad(0) : CPad::GetPad(1);

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
					returnAddress = 0x6E09E8; // horn return
				}
				else if (hornJustUp && CTimer::m_snTimeInMilliseconds - hornPressLastTime < 150) {
					hornJustUp = false;
					hornHasPressed = false;
					returnAddress = 0x6E0999; // toggle siren return
				}
				else {
					returnAddress = 0x6E09F7; // no horn return
				}
				*(uint32_t*)(regs.esp - 0x4) = returnAddress;
			}
		}; injector::MakeInline<SirenTurnOnFix>(0x006E0961);


	#endif // GTASA

    }
} framerateVigilante;
