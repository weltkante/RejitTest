#pragma once

#include "..\Engine\src\ExtensionsCommon\InstrumentationMethodBase.h"
#include "..\Engine\src\ExtensionsCommon\InteropInstrumentationHandler.h"
#include "..\Engine\src\ExtensionsCommon\ReflectionHelper.h"

class CProfilingTarget final
{
public:
	const std::wstring AssemblyName;
	const std::wstring ModuleName;
	const std::wstring TypeName;
	const std::wstring MethodName;

	CProfilingTarget(
		std::wstring AssemblyName,
		std::wstring ModuleName,
		std::wstring TypeName,
		std::wstring MethodName);

	CProfilingTarget(const CProfilingTarget&) = default;
	CProfilingTarget(CProfilingTarget&&) = default;
	CProfilingTarget& operator=(const CProfilingTarget&) = default;
	CProfilingTarget& operator=(CProfilingTarget&&) = default;

	bool operator==(const CProfilingTarget& other) const
	{
		return MethodName == other.MethodName
			&& TypeName == other.TypeName
			&& ModuleName == other.ModuleName
			&& AssemblyName == other.AssemblyName;
	}

	bool operator!=(const CProfilingTarget& other) const { return !(*this == other); }
};

namespace std
{
	template <>
	struct hash<CProfilingTarget>
	{
		size_t operator()(const CProfilingTarget& k) const
		{
			size_t res = 17;
			res = res * 31 + hash<wstring>()(k.AssemblyName);
			res = res * 31 + hash<wstring>()(k.ModuleName);
			res = res * 31 + hash<wstring>()(k.TypeName);
			res = res * 31 + hash<wstring>()(k.MethodName);
			return res;
		}
	};
}

class CSubjectRecord final : std::enable_shared_from_this<CSubjectRecord>
{
public:
	AGENT_DECLARE_NO_LOGGING;

	int32_t* CallCountPtr;

	CSubjectRecord() = delete;
	CSubjectRecord(const CSubjectRecord&) = delete;
	CSubjectRecord(CSubjectRecord&&) = delete;
	CSubjectRecord& operator=(const CSubjectRecord&) = delete;
	CSubjectRecord& operator=(CSubjectRecord&&) = delete;

	~CSubjectRecord() = default;

	explicit CSubjectRecord(int32_t* callCountPtr)
		: CallCountPtr(callCountPtr)
	{
	}

	HRESULT Instrument(const IProfilerManagerSptr& spManager, const IMethodInfoSptr& spMethodInfo);
};

EXTERN_C extern const GUID CLSID_CustomMethod;

typedef CDataContainerAdapterImplT<std::shared_ptr<CSubjectRecord>, IMethodInfoSptr, &CLSID_CustomMethod, &CLSID_CustomMethod> CSubjectDataAdapter;

class ATL_NO_VTABLE DECLSPEC_UUID("A2B3B0A6-3449-41CA-957C-2C160E925715") CCustomMethod
	: public ATL::CComObjectRootEx<ATL::CComMultiThreadModelNoCS>
	, public ATL::CComCoClass<CCustomMethod, &CLSID_CustomMethod>
	, public CInstrumentationMethodBase
{
public:
	DECLARE_NO_REGISTRY()
	DECLARE_PROTECT_FINAL_CONSTRUCT()
	DECLARE_NOT_AGGREGATABLE(CCustomMethod)
	BEGIN_COM_MAP(CCustomMethod)
		COM_INTERFACE_ENTRY(IInstrumentationMethod)
	END_COM_MAP()

	HRESULT InternalInitialize(
		_In_ const IProfilerManagerSptr & spHost) override final;

	_Success_(return == 0) HRESULT InternalShouldInstrumentMethod(
		_In_ const IMethodInfoSptr & spMethodInfo,
		_In_ BOOL isRejit,
		_Out_ BOOL * pbInstrument) override final;

	_Success_(return == 0) HRESULT InternalInstrumentMethod(
		_In_ const IMethodInfoSptr & spMethodInfo,
		_In_ BOOL isRejit) override final;

	_Success_(return == 0) HRESULT InternalAllowInlineSite(
		_In_ const IMethodInfoSptr & spMethodInfoInlinee,
		_In_ const IMethodInfoSptr & spMethodInfoCaller,
		_Out_ BOOL * pbAllowInline) override final;

	HANDLE InstallCounterHandler(
		_In_ const std::wstring & assemblyPtr,
		_In_ const std::wstring & modulePtr,
		_In_ const std::wstring & typeNamePtr,
		_In_ const std::wstring & methodNamePtr,
		_In_ int32_t * callCountPtr);

private:
	IProfilerManagerSptr m_spHost;
	Agent::Interop::CInteropInstrumentationHandlerUptr m_spInteropHandler;

	CSubjectDataAdapter m_dataAdapter;
	std::unordered_map<CProfilingTarget, std::shared_ptr<CSubjectRecord>> m_targetMethods;
};
