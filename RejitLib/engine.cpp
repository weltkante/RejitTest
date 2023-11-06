#include "pch.h"
#include "engine.h"

namespace Agent { namespace Interop { static const GUID GUID_MethodInfoDataItem = CLSID_CustomMethod; } }

EXTERN_C const GUID CLSID_CustomMethod = __uuidof(CCustomMethod);
OBJECT_ENTRY_AUTO(CLSID_CustomMethod, CCustomMethod)

static __int64 STDMETHODCALLTYPE InstallCounterHandler(
	_In_ CCustomMethod* pThis,
	_In_ const __int64 assemblyPtr,
	_In_ const __int64 modulePtr,
	_In_ const __int64 typeNamePtr,
	_In_ const __int64 methodNamePtr,
	_In_ const __int64 callCountPtr)
{
	return (__int64)pThis->InstallCounterHandler(
		reinterpret_cast<const wchar_t*>(assemblyPtr),
		reinterpret_cast<const wchar_t*>(modulePtr),
		reinterpret_cast<const wchar_t*>(typeNamePtr),
		reinterpret_cast<const wchar_t*>(methodNamePtr),
		reinterpret_cast<int32_t*>(callCountPtr));
}

HRESULT CCustomMethod::InternalInitialize(_In_ const IProfilerManagerSptr& spHost)
{
	auto hr = S_OK;

	IUnknownSptr spIUnknown;
	IfFailRet(spHost->GetCorProfilerInfo(&spIUnknown));
	ICorProfilerInfoQiSptr spICorProfilerInfo = spIUnknown;
	IfFailRet(spICorProfilerInfo->SetEventMask(COR_PRF_DISABLE_ALL_NGEN_IMAGES | COR_PRF_ENABLE_REJIT));

	Agent::Instrumentation::CMethodInstrumentationInfoCollectionSptr spInteropMethods;
	IfFailRet(Create(spInteropMethods));

	Agent::Instrumentation::Native::CNativeInstanceMethodInstrumentationInfoSptr spInteropMethod;
	IfFailRet(Create(spInteropMethod,
		Agent::Interop::WildcardName,
		Agent::Interop::WildcardName,
		L"RejitTest.NativeRuntime.InstallCounter", 5, 0,
		reinterpret_cast<UINT_PTR>(this),
		reinterpret_cast<UINT_PTR>(&::InstallCounterHandler)));
	IfFailRet(spInteropMethods->Add(spInteropMethod));

	IfFailRet(CreateAndBuildUp(m_spInteropHandler, spInteropMethods));

	m_spHost = spHost;
	return hr;
}

_Success_(return == 0) HRESULT CCustomMethod::InternalShouldInstrumentMethod(_In_ const IMethodInfoSptr & spMethodInfo, _In_ BOOL isRejit, _Out_ BOOL * pbInstrument)
{
	*pbInstrument = FALSE;

	HRESULT hr = S_OK;
	IfFailRet(hr = m_spInteropHandler->ShouldInstrument(spMethodInfo));

	if (hr == S_FALSE && !m_targetMethods.empty()) {
		std::shared_ptr<CSubjectRecord> record;
		IfFailRet(hr = m_dataAdapter.GetDataItem(spMethodInfo, record));
		if (hr == S_FALSE) {
			ATL::CComBSTR bstrFullMethodName;
			IfFailRet(spMethodInfo->GetFullName(&bstrFullMethodName));

			IModuleInfoSptr sptrModuleInfo;
			IfFailRet(spMethodInfo->GetModuleInfo(&sptrModuleInfo));

			ATL::CComBSTR bstrModuleName;
			IfFailRet(sptrModuleInfo->GetModuleName(&bstrModuleName));

			IAssemblyInfoSptr sptrAssemblyInfo;
			IfFailRet(sptrModuleInfo->GetAssemblyInfo(&sptrAssemblyInfo));

			ATL::CComBSTR bstrAssemblyName;
			IfFailRet(sptrAssemblyInfo->GetName(&bstrAssemblyName));

			ATL::CComBSTR bstrMethodName;
			IfFailRet(spMethodInfo->GetName(&bstrMethodName));

			std::wstring sFullMethodName(bstrFullMethodName);
			std::wstring sTypeName = sFullMethodName.substr(0, sFullMethodName.length() - bstrMethodName.Length() - 1);
			std::wstring sAssemblyName(bstrAssemblyName, bstrAssemblyName.Length());
			std::wstring sModuleName(bstrModuleName, bstrModuleName.Length());
			std::wstring sMethodName(bstrMethodName, bstrMethodName.Length());

			CProfilingTarget target(sAssemblyName, sModuleName, sTypeName, sMethodName);

			auto it = m_targetMethods.find(target);
			if (it != m_targetMethods.end()) {
				IfFailRet(m_dataAdapter.SetDataItem(spMethodInfo, it->second));
				hr = S_OK;
			}
		}
	}

	*pbInstrument = (hr == S_OK);
	return S_OK;
}

_Success_(return == 0) HRESULT CCustomMethod::InternalInstrumentMethod(_In_ const IMethodInfoSptr & spMethodInfo, _In_ BOOL isRejit)
{
	HRESULT hr = S_OK;

	std::shared_ptr<CSubjectRecord> ip;
	IfFailRetHresult(m_dataAdapter.GetDataItem(spMethodInfo, ip), hr);
	if (hr == S_OK) {
		IfFailRet(ip->Instrument(m_spHost, spMethodInfo));
		return S_OK;
	}

	IfFailRet(m_spInteropHandler->Instrument(spMethodInfo));
	return S_OK;
}

_Success_(return == 0) HRESULT CCustomMethod::InternalAllowInlineSite(_In_ const IMethodInfoSptr & spMethodInfoInlinee, _In_ const IMethodInfoSptr & spMethodInfoCaller, _Out_ BOOL * pbAllowInline)
{
	HRESULT hr = S_OK;
	IfFailRetHresult(m_spInteropHandler->AllowInline(spMethodInfoInlinee, spMethodInfoCaller), hr);
	*pbAllowInline = (hr != S_OK);
	return hr;
}

struct RejitRequestArgs
{
	HANDLE ev{};
	IProfilerManagerSptr m_spHost;
	std::wstring assemblyName;
	std::wstring moduleName;
	std::wstring typeName;
	std::wstring methodName;
	int32_t* callCountPtr{};
};

unsigned __stdcall RejitRequestProc(void* args)
{
	auto e = (RejitRequestArgs*)args;

	ATL::CComBSTR bstrAssemblyName(e->assemblyName.size(), e->assemblyName.c_str());
	ATL::CComBSTR bstrModuleName(e->moduleName.size(), e->moduleName.c_str());
	ATL::CComBSTR bstrTypeName(e->typeName.size(), e->typeName.c_str());
	ATL::CComBSTR bstrMethodName(e->methodName.size(), e->methodName.c_str());

	ATL::CComPtr<IAppDomainCollection> pAppDomains;
	if (FAILED(e->m_spHost->GetAppDomainCollection(&pAppDomains)))
		return E_FAIL;

	ATL::CComPtr<IEnumAppDomainInfo> pAppDomainIter;
	if (FAILED(pAppDomains->GetAppDomains(&pAppDomainIter)))
		return E_FAIL;

	for (;;) {
		ULONG n = 0;
		ATL::CComPtr<IAppDomainInfo> pAppDomain;
		if (FAILED(pAppDomainIter->Next(1, &pAppDomain, &n)) || !pAppDomain || !n)
			break;

		ATL::CComPtr<IEnumModuleInfo> pModuleIter;
		if (FAILED(pAppDomain->GetModuleInfosByName(bstrModuleName, &pModuleIter)))
			continue;

		for (;;) {
			ATL::CComPtr<IModuleInfo> pModule;
			if (FAILED(pModuleIter->Next(1, &pModule, &n)) || !pModule || !n)
				break;

			ATL::CComPtr<IAssemblyInfo> pAssembly;
			if (FAILED(pModule->GetAssemblyInfo(&pAssembly)))
				continue;

			ATL::CComBSTR bstrThisAssemblyName;
			if (FAILED(pAssembly->GetName(&bstrThisAssemblyName)) || bstrThisAssemblyName != bstrAssemblyName)
				continue;

			ATL::CComPtr<IMetaDataImport2> pMetaDataImport;
			if (FAILED(pModule->GetMetaDataImport((IUnknown**)&pMetaDataImport)))
				continue;

			mdTypeDef typeId;
			if (FAILED(pMetaDataImport->FindTypeDefByName(bstrTypeName, mdTokenNil, &typeId)))
				continue;

			mdMethodDef methodIds[8];
			ULONG methodCount = 0;
			HCORENUM hMethodIter = NULL;
			for (;;) {
				if (FAILED(pMetaDataImport->EnumMethodsWithName(&hMethodIter, typeId, bstrMethodName, methodIds, ARRAYSIZE(methodIds), &methodCount)) || !methodCount)
					break;

				for (ULONG methodIndex = 0; methodIndex < methodCount; ++methodIndex) {
					mdMethodDef methodId = methodIds[methodIndex];

					ATL::CComPtr<IMethodInfo> pMethod;
					if (FAILED(pModule->GetMethodInfoByToken(methodId, &pMethod)))
						continue;

					if (FAILED(pModule->RequestRejit(methodId)))
						continue;
				}
			}
			pMetaDataImport->CloseEnum(hMethodIter);
		}
	}

	SetEvent(e->ev);
	delete e;
	return S_OK;
}

HANDLE CCustomMethod::InstallCounterHandler(
	_In_ const std::wstring & assemblyName,
	_In_ const std::wstring & moduleName,
	_In_ const std::wstring & typeName,
	_In_ const std::wstring & methodName,
	_In_ int32_t * callCountPtr)
{
	auto ev = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (!ev) return NULL;

	auto args = new RejitRequestArgs{};
	args->ev = ev;
	args->m_spHost = m_spHost;
	args->assemblyName = assemblyName;
	args->moduleName = moduleName;
	args->typeName = typeName;
	args->methodName = methodName;
	args->callCountPtr = callCountPtr;

	HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, &RejitRequestProc, args, 0, 0);
	if (hThread == INVALID_HANDLE_VALUE || hThread == NULL) {
		delete args;
		CloseHandle(ev);
		return NULL;
	}

	CloseHandle(hThread);

	ATL::CComBSTR bstrAssemblyName(assemblyName.size(), assemblyName.c_str());
	ATL::CComBSTR bstrModuleName(moduleName.size(), moduleName.c_str());
	ATL::CComBSTR bstrTypeName(typeName.size(), typeName.c_str());
	ATL::CComBSTR bstrMethodName(methodName.size(), methodName.c_str());

	m_targetMethods.insert(std::make_pair(
		CProfilingTarget(assemblyName, moduleName, typeName, methodName),
		std::make_shared<CSubjectRecord>(callCountPtr)));

	return ev;
}

CProfilingTarget::CProfilingTarget(std::wstring AssemblyName, std::wstring ModuleName, std::wstring TypeName, std::wstring MethodName)
	: AssemblyName(std::move(AssemblyName))
	, ModuleName(std::move(ModuleName))
	, TypeName(std::move(TypeName))
	, MethodName(std::move(MethodName))
{
}

HRESULT CSubjectRecord::Instrument(const IProfilerManagerSptr & spManager, const IMethodInfoSptr & spMethodInfo)
{
	IInstructionFactorySptr spInstructionFactory;
	IfFailRet(spMethodInfo->GetInstructionFactory(&spInstructionFactory));

	IInstructionGraphSptr spInstructionGraph;
	IfFailRet(spMethodInfo->GetInstructions(&spInstructionGraph));

	IInstructionSptr spMethodStart;
	IfFailRet(spInstructionGraph->GetFirstInstruction(&spMethodStart));

	IInstructionSptr spLoadAddr;
#if defined(_M_X64)
	static_assert(sizeof(CallCountPtr) == sizeof(INT64), "unexpected pointer size");
	IfFailRet(spInstructionFactory->CreateLongOperandInstruction(ILOrdinalOpcode::Cee_Ldc_I8, (INT64)CallCountPtr, &spLoadAddr));
#elif defined(_M_IX86)
	static_assert(sizeof(CallCountPtr) == sizeof(INT32), "unexpected pointer size");
	IfFailRet(spInstructionFactory->CreateIntOperandInstruction(ILOrdinalOpcode::Cee_Ldc_I4, (INT32)CallCountPtr, &spLoadAddr));
#else
	static_assert(false, "Unsupported processor architecture");
#endif
	IfFailRet(spInstructionGraph->InsertBefore(spMethodStart, spLoadAddr));

	IInstructionSptr spConv;
	IfFailRet(spInstructionFactory->CreateInstruction(ILOrdinalOpcode::Cee_Conv_I, &spConv));
	IfFailRet(spInstructionGraph->InsertBefore(spMethodStart, spConv));

	IInstructionSptr spDup;
	IfFailRet(spInstructionFactory->CreateInstruction(ILOrdinalOpcode::Cee_Dup, &spDup));
	IfFailRet(spInstructionGraph->InsertBefore(spMethodStart, spDup));

	IInstructionSptr spLoadInd;
	IfFailRet(spInstructionFactory->CreateInstruction(ILOrdinalOpcode::Cee_Ldind_I4, &spLoadInd));
	IfFailRet(spInstructionGraph->InsertBefore(spMethodStart, spLoadInd));

	IInstructionSptr spLoadInc;
	IfFailRet(spInstructionFactory->CreateIntOperandInstruction(ILOrdinalOpcode::Cee_Ldc_I4, 1, &spLoadInc));
	IfFailRet(spInstructionGraph->InsertBefore(spMethodStart, spLoadInc));

	IInstructionSptr spAddInc;
	IfFailRet(spInstructionFactory->CreateInstruction(ILOrdinalOpcode::Cee_Add, &spAddInc));
	IfFailRet(spInstructionGraph->InsertBefore(spMethodStart, spAddInc));

	IInstructionSptr spStoreInd;
	IfFailRet(spInstructionFactory->CreateInstruction(ILOrdinalOpcode::Cee_Stind_I4, &spStoreInd));
	IfFailRet(spInstructionGraph->InsertBefore(spMethodStart, spStoreInd));

	return S_OK;
}
