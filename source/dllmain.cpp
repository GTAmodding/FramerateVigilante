#include <windows.h>
#include <stdint.h>
#include <injector/injector.hpp>
#include <injector/assembly.hpp>
#include <injector/calling.hpp>
#include <injector/hooking.hpp>
#include <injector/utility.hpp>

void InitSA()
{
    static float& ms_fTimeStep = *(float*)0xB7CB5C;
    static constexpr float magic = 50.0f / 30.0f;

    struct SwimSpeedFix
    {
        void operator()(injector::reg_pack& regs)
        {
            *(float*)(regs.esp + 0x1C) *= 1.0f / (ms_fTimeStep / magic);
            *(float*)(regs.esp + 0x20) *= 1.0f / (ms_fTimeStep / magic);
            *(float*)(regs.esp + 0x18) *= 1.0f / (ms_fTimeStep / magic);

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
            float f = (1.0f + ((ms_fTimeStep / magic) / 1.5f)) * (ms_fTimeStep / magic);
            _asm {fmul dword ptr[f]}
        }
    }; injector::MakeInline<BuoyancyFix>(0x6C27AE, 0x6C27AE + 6);

    struct DiveFix
    {
        void operator()(injector::reg_pack& regs)
        {
            float f = -0.1f * (ms_fTimeStep / magic);
            _asm {fmul dword ptr[f]}
        }
    }; injector::MakeInline<DiveFix>(0x68A42B, 0x68A42B + 6);

    struct CarSlowDownSpeedFix
    {
        void operator()(injector::reg_pack& regs)
        {
            float f = **(float**)0x5BFAB0 * (ms_fTimeStep / magic);
            _asm {fld dword ptr[f]}
        }
    };
    injector::MakeInline<CarSlowDownSpeedFix>(0x6D6E69, 0x6D6E69 + 6);
    injector::MakeInline<CarSlowDownSpeedFix>(0x6D6EA8, 0x6D6EA8 + 6);
    injector::MakeInline<CarSlowDownSpeedFix>(0x6D767F, 0x6D767F + 6);
    injector::MakeInline<CarSlowDownSpeedFix>(0x6D76AB, 0x6D76AB + 6);
    injector::MakeInline<CarSlowDownSpeedFix>(0x6D76CD, 0x6D76CD + 6);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        if (injector::address_manager::singleton().IsSA())
        {
            InitSA();
        }
    }

    return TRUE;
}