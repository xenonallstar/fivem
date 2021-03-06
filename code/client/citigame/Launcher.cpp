/*
 * This file is part of the CitizenFX project - http://citizen.re/
 *
 * See LICENSE and MENTIONS in the root of the source tree for information
 * regarding licensing.
 */

#include "StdInc.h"
#include "LauncherInterface.h"
#include "Launcher.h"
#include "CrossLibraryInterfaces.h"
#include "ResumeComponent.h"

#include "Hooking.Aux.h"

#include <ComponentLoader.h>

#include <HostSharedData.h>
#include <CfxState.h>

IGameSpecToHooks* g_hooksDLL;

__declspec(dllexport) void SetHooksDll(IGameSpecToHooks* dll);

bool LauncherInterface::PreLoadGame(void* cefSandbox)
{
	// if we don't have adhesive, set our gamePid
	static HostSharedData<CfxState> initState("CfxInitState");

	if (initState->IsMasterProcess())
	{
		const auto& map = ComponentLoader::GetInstance()->GetKnownComponents();

		if (map.find("adhesive") == map.end())
		{
			initState->gamePid = initState->initialGamePid;
		}
	}
	
	bool continueRunning = true;

	// HooksDLL only exists for GTA_NY
#ifdef GTA_NY
	HooksDLLInterface::PreGameLoad(&continueRunning, &g_hooksDLL);
#endif

	// initialize component instances
    if (!getenv("CitizenFX_ToolMode"))
    {
		DisableToolHelpScope scope;

        ComponentLoader::GetInstance()->ForAllComponents([&] (fwRefContainer<ComponentData> componentData)
        {
            for (auto& instance : componentData->GetInstances())
            {
                instance->Initialize();
            }
        });
    }

	SetHooksDll(g_hooksDLL);

	// and start running the game
	return continueRunning;
}

bool LauncherInterface::PostLoadGame(HMODULE hModule, void(**entryPoint)())
{
	bool continueRunning = true;

	{
		DisableToolHelpScope scope;
		ComponentLoader::GetInstance()->DoGameLoad(hModule);
	}

	// HooksDLL only exists for GTA_NY
#ifdef GTA_NY
	HooksDLLInterface::PostGameLoad(hModule, &continueRunning);
#endif

	InitFunctionBase::RunAll();

#if defined(GTA_NY)
	*entryPoint = (void(*)())0xD0D011;
#elif defined(PAYNE)
	// don't modify the entry point
	//*entryPoint = (void(*)())0;
#elif defined(_M_AMD64)
#else
#error TODO: define entry point for this title
#endif

	return continueRunning;
}

template<typename T>
void RunLifeCycleCallback(const T& cb)
{
	ComponentLoader::GetInstance()->ForAllComponents([&] (fwRefContainer<ComponentData> componentData)
	{
		auto& instances = componentData->GetInstances();

		if (instances.size())
		{
			auto& component = instances[0];

			auto lifeCycle = dynamic_component_cast<LifeCycleComponent*>(component.GetRef());

			if (lifeCycle)
			{
				cb(lifeCycle);
			}
		}
	});
}

bool LauncherInterface::PreResumeGame()
{
	RunLifeCycleCallback([] (LifeCycleComponent* component)
	{
		component->PreResumeGame();
	});

	return true;
}

#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

static void WarnOSVersion()
{
	static TASKDIALOGCONFIG taskDialogConfig = { 0 };
	taskDialogConfig.cbSize = sizeof(taskDialogConfig);
	taskDialogConfig.hInstance = GetModuleHandle(nullptr);
	taskDialogConfig.dwFlags = TDF_ENABLE_HYPERLINKS;
	taskDialogConfig.dwCommonButtons = TDCBF_CLOSE_BUTTON;
	taskDialogConfig.pszWindowTitle = L"Your Windows 7 PC is out of support";
	taskDialogConfig.pszMainIcon = TD_ERROR_ICON;
	taskDialogConfig.pszMainInstruction = L"Your Windows 7 PC is out of support";
	taskDialogConfig.pszContent = L"As of January 14, 2020, support for Windows 7 has come to an end. Your PC is more vulnerable to viruses and malware due to:\n\n- No security updates\n- No software updates\n- No tech support\n\nPlease upgrade to Windows 8.1 or higher as soon as possible. The game will continue to start now.";

	TaskDialogIndirect(&taskDialogConfig, nullptr, nullptr, nullptr);
}

#include <HostSharedData.h>
#include <CfxState.h>

bool LauncherInterface::PreInitializeGame()
{
	if (!IsWindows8OrGreater())
	{
		static HostSharedData<CfxState> initState("CfxInitState");

		if (initState->IsMasterProcess())
		{
			WarnOSVersion();
		}
	}

	// make the component loader initialize
	ComponentLoader::GetInstance()->Initialize();

	// run callbacks on the component loader
    if (!getenv("CitizenFX_ToolMode"))
    {
		DisableToolHelpScope thScope;

        RunLifeCycleCallback([] (LifeCycleComponent* component)
        {
            component->PreInitGame();
        });
    }

	return true;
}

static LauncherInterface g_launcherInterface;

extern "C" __declspec(dllexport) ILauncherInterface* GetLauncherInterface()
{
	return &g_launcherInterface;
}
