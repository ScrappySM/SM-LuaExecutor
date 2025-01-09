#include <Windows.h>
#include <iostream>
#include <optional>
#include <memory>
#include <queue>
#include <string>

#include <luajit/lua.hpp>
#include <MinHook.h>

#include "log.h"

// Static pointer to the LuaManager instance
constexpr static uintptr_t LuaManagerPtrOffset = 0x1267620;

static std::queue<std::string> g_luaQueue = {};

class LuaVM {
public:
	lua_State* L;
};

class LuaManager {
private:
	/* 0x0000 */ char pad_0x0000[0x2E8];
public:
	/* 0x02E8 */ std::shared_ptr<LuaVM> luaVM;

	static std::optional<LuaManager*> GetInstance() {
		LuaManager* pLuaManager = *reinterpret_cast<LuaManager**>((uintptr_t)GetModuleHandle(nullptr) + LuaManagerPtrOffset);

		// The LuaManager is nullptr when the PlayState does not exists (aka not in a world)
		if (pLuaManager == nullptr) {
			std::cout << "LuaManager is null" << std::endl;
			return std::nullopt;
		}

		return pLuaManager;
	}

	static void ExecuteString(const std::string& str) {
		g_luaQueue.push(str);
	}

	static void ExecuteQueue() {
		LuaManager* pLuaManager = GetInstance().value_or(nullptr);
		if (pLuaManager == nullptr) {
			std::cout << "Failed to get LuaManager instance" << std::endl;
			return;
		}
		lua_State* L = pLuaManager->luaVM->L;
		if (L == nullptr) {
			std::cout << "Failed to get Lua state" << std::endl;
			return;
		}
		while (!g_luaQueue.empty()) {
			std::string str = g_luaQueue.front();
			g_luaQueue.pop();
			if (luaL_dostring(L, str.c_str()) != LUA_OK) {
				std::cout << "Failed to execute Lua string: " << lua_tostring(L, -1) << std::endl;
				lua_pop(L, 1);
			}

			// Print out everything in global table
			lua_getglobal(L, "_G");
			lua_pushnil(L);
			while (lua_next(L, -2) != 0) {
				const char* key = lua_tostring(L, -2);
				const char* value = lua_tostring(L, -1);
				if (key != nullptr) {
					std::cout << key << " = ";
				}
				if (value != nullptr) {
					std::cout << value << std::endl;
				}
				else {
					std::cout << "nil" << std::endl;
				}
				lua_pop(L, 1);
			}
			lua_pop(L, 1);
		}
	}


};

constexpr uintptr_t UpdateFunctionOffset = 0x343030;

using UpdateFunction_t = int(__fastcall*)(__int64, __int64, __int64);
UpdateFunction_t o_UpdateFunction = (UpdateFunction_t)UpdateFunctionOffset;

static int __fastcall UpdateFunction_Hook(__int64 a1, __int64 a2, __int64 a3) {
	LuaManager::ExecuteQueue();
	return o_UpdateFunction(a1, a2, a3);
}

// DllMain function which gets called when the DLL is loaded
BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved) {
	// RAII object to handle console creation/destruction
	// Simply having this line here will allow you to print to the games console
	// Don't worry about it, but if you're interested feel free to look in `utils.h`
	// for the implementation
	Log_t logRAII;

	if (dwReason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(hModule);
		std::cout << "DLL_PROCESS_ATTACH" << std::endl;

		// Queue a hello world string to be executed
		LuaManager::ExecuteString("print('Hello, World!')");

		// We need to call it from one of the game's threads,
		// this is because the Lua state is not thread-safe
		// and the game's main thread is the only one that can
		// interact with the Lua state
		if (MH_Initialize() != MH_OK) {
			std::cout << "Failed to initialize MinHook" << std::endl;
			return FALSE;
		}

		// Hook the Update function
		uintptr_t updateFunctionAddr = (uintptr_t)GetModuleHandle(nullptr) + UpdateFunctionOffset;
		MH_STATUS status = MH_CreateHook((void*)updateFunctionAddr, &UpdateFunction_Hook, (void**)&o_UpdateFunction);
		if (status != MH_OK) {
			std::cout << "Failed to create hook for Update function: " << MH_StatusToString(status) << std::endl;
			return FALSE;
		}

		if (MH_EnableHook((void*)updateFunctionAddr) != MH_OK) {
			std::cout << "Failed to enable hook for Update function" << std::endl;
			return FALSE;
		}
	}

	if (dwReason == DLL_PROCESS_DETACH) {
		std::cout << "DLL_PROCESS_DETACH" << std::endl;

		uintptr_t updateFunctionAddr = (uintptr_t)GetModuleHandle(nullptr) + UpdateFunctionOffset;

		// Unhook the Update function
		if (MH_DisableHook((void*)updateFunctionAddr) != MH_OK) {
			std::cout << "Failed to disable hook for Update function" << std::endl;
			return FALSE;
		}

		if (MH_Uninitialize() != MH_OK) {
			std::cout << "Failed to uninitialize MinHook" << std::endl;
			return FALSE;
		}

		std::cout << "Unhooked Update function" << std::endl;
	}

	return TRUE;
}
