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

    struct SwimFix
    {
        void operator()(injector::reg_pack& regs)
        {
            //to-do
        }
    }; injector::MakeInline<SwimFix>(0x000000, 0x000000 + 0);
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