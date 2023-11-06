#include "pch.h"

struct CLibraryModule : public ATL::CAtlDllModuleT<CLibraryModule>
{
	DECLARE_NO_REGISTRY()
};

static CLibraryModule kModule;

BOOL WINAPI DllMain(_In_ HMODULE hModule, _In_ DWORD reason, _In_opt_ LPVOID reserved) throw()
{
	return kModule.DllMain(reason, reserved);
}

__control_entrypoint(DllExport) STDAPI DllCanUnloadNow() throw()
{
	return kModule.DllCanUnloadNow();
}

_Check_return_ STDAPI DllGetClassObject(_In_ REFCLSID rclsid, _In_ REFIID riid, _Outptr_ LPVOID FAR* ppv) throw()
{
	return kModule.DllGetClassObject(rclsid, riid, ppv);
}

STDAPI DllRegisterServer() throw()
{
	return kModule.DllRegisterServer(FALSE);
}

STDAPI DllUnregisterServer() throw()
{
	return kModule.DllRegisterServer(FALSE);
}
