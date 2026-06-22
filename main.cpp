/*
 *  This Source Code Form is subject to the terms of the Mozilla Public License,
 *  v. 2.0. If a copy of the MPL was not distributed with this file, You can
 *  obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <sdk.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>

#include "bitstream.hpp"

using cell = int32_t;
struct AMX;
using AMX_NATIVE = cell(__CDECL*)(AMX*, const cell*);

struct AMX_NATIVE_INFO
{
	const char* name;
	AMX_NATIVE func;
};

struct IPawnScript
{
	virtual int Allot(int cells, cell* amx_addr, cell** phys_addr) = 0;
	virtual int Callback(cell index, cell* result, const cell* params) = 0;
	virtual int Cleanup() = 0;
	virtual int Clone(AMX* amxClone, void* data) const = 0;
	virtual int Exec(cell* retval, int index) = 0;
	virtual int FindNative(char const* name, int* index) const = 0;
	virtual int FindPublic(char const* funcname, int* index) const = 0;
	virtual int FindPubVar(char const* varname, cell* amx_addr) const = 0;
	virtual int FindTagId(cell tag_id, char* tagname) const = 0;
	virtual int Flags(uint16_t* flags) const = 0;
	virtual int GetAddr(cell amx_addr, cell** phys_addr) const = 0;
	virtual int GetNative(int index, char* funcname) const = 0;
	virtual int GetNativeByIndex(int index, AMX_NATIVE_INFO* ret) const = 0;
	virtual int GetPublic(int index, char* funcname) const = 0;
	virtual int GetPubVar(int index, char* varname, cell* amx_addr) const = 0;
	virtual int GetString(char const* dest, const cell* source, bool use_wchar, size_t size) const = 0;
	virtual int GetString(char* dest, const cell* source, bool use_wchar, size_t size) = 0;
	virtual int GetTag(int index, char* tagname, cell* tag_id) const = 0;
	virtual int GetUserData(long tag, void** ptr) const = 0;
	virtual int Init(void* program) = 0;
	virtual int InitJIT(void* reloc_table, void* native_code) = 0;
	virtual int MakeAddr(cell* phys_addr, cell* amx_addr) const = 0;
	virtual int MemInfo(long* codesize, long* datasize, long* stackheap) const = 0;
	virtual int NameLength(int* length) const = 0;
	virtual AMX_NATIVE_INFO* NativeInfo(char const* name, AMX_NATIVE func) const = 0;
	virtual int NumNatives(int* number) const = 0;
	virtual int NumPublics(int* number) const = 0;
	virtual int NumPubVars(int* number) const = 0;
	virtual int NumTags(int* number) const = 0;
	virtual int Push(cell value) = 0;
	virtual int PushArray(cell* amx_addr, cell** phys_addr, const cell array[], int numcells) = 0;
	virtual int PushString(cell* amx_addr, cell** phys_addr, StringView string, bool pack, bool use_wchar) = 0;
	virtual int RaiseError(int error) = 0;
	virtual int Register(const AMX_NATIVE_INFO* nativelist, int number) = 0;
	virtual int Release(cell amx_addr) = 0;
	virtual int SetCallback(void* callback) = 0;
	virtual int SetDebugHook(void* debug) = 0;
	virtual int SetString(cell* dest, StringView source, bool pack, bool use_wchar, size_t size) const = 0;
	virtual int SetUserData(long tag, void* ptr) = 0;
	virtual int StrLen(const cell* cstring, int* length) const = 0;
};

struct PawnEventHandler
{
	virtual void onAmxLoad(IPawnScript& script) = 0;
	virtual void onAmxUnload(IPawnScript& script) = 0;
};

static const UID PawnComponent_UID = UID(0x78906cd9f19c36a6);
struct IPawnComponent : public IComponent
{
	PROVIDE_UID(PawnComponent_UID);

	virtual IEventDispatcher<PawnEventHandler>& getEventDispatcher() = 0;
	virtual const StaticArray<void*, 52>& getAmxFunctions() const = 0;
	virtual IPawnScript const* getScript(AMX* amx) const = 0;
	virtual IPawnScript* getScript(AMX* amx) = 0;
	virtual IPawnScript* mainScript() = 0;
	virtual const Span<IPawnScript*> sideScripts() = 0;
};

class AdvancedQueryComponent final : public IComponent, public PawnEventHandler, public PlayerConnectEventHandler, public NetworkOutEventHandler
{
private:
	static constexpr int PlayerInitRpc = 139;
	static constexpr size_t MaxInitGameHostname = 64;
	static constexpr size_t VehicleModelCount = 212;

	struct PlayerInitData
	{
		bool enableZoneNames = false;
		bool usePlayerPedAnims = false;
		bool allowInteriorWeapons = false;
		bool useLimitGlobalChatRadius = false;
		float limitGlobalChatRadius = 0.0f;
		bool enableStuntBonus = false;
		float nameTagDrawDistance = 0.0f;
		bool disableInteriorEnterExits = false;
		bool disableNameTagLOS = false;
		bool manualVehicleEngineAndLights = false;
		uint32_t spawnInfoCount = 0;
		uint16_t playerID = 0;
		bool showNameTags = false;
		uint32_t showPlayerMarkers = 0;
		uint8_t worldTime = 0;
		uint8_t weather = 0;
		float gravity = 0.0f;
		bool lanMode = false;
		uint32_t deathDropAmount = 0;
		bool instagib = false;
		uint32_t onFootRate = 0;
		uint32_t inCarRate = 0;
		uint32_t weaponRate = 0;
		uint32_t multiplier = 0;
		uint32_t lagCompensation = 0;
		HybridString<MaxInitGameHostname> serverName;
		std::array<uint8_t, VehicleModelCount> vehicleModels {};
		bool enableVehicleFriendlyFire = false;
	};

	static AdvancedQueryComponent* self_;

	ICore* core_ = nullptr;
	IPawnComponent* pawn_ = nullptr;
	uint16_t requestedSlots_ = 0;
	uint16_t advertisedSlots_ = 0;
	bool active_ = false;
	Impl::String initGameHostname_;
	bool initGameHostnameActive_ = false;
	bool networkHandlersRegistered_ = false;

public:
	PROVIDE_UID(0x2EE35F58EC5C44D1);

	AdvancedQueryComponent()
	{
		self_ = this;
	}

	~AdvancedQueryComponent()
	{
		if (pawn_)
		{
			pawn_->getEventDispatcher().removeEventHandler(this);
		}
		if (core_)
		{
			core_->getPlayers().getPlayerConnectDispatcher().removeEventHandler(this);
			removeNetworkHandlers();
		}
		if (self_ == this)
		{
			self_ = nullptr;
		}
	}

	StringView componentName() const override
	{
		return "Advanced Query";
	}

	SemanticVersion componentVersion() const override
	{
		return SemanticVersion(1, 0, 0, 0);
	}

	void onLoad(ICore* core) override
	{
		core_ = core;
		core_->getPlayers().getPlayerConnectDispatcher().addEventHandler(this);
	}

	void onInit(IComponentList* components) override
	{
		pawn_ = components->queryComponent<IPawnComponent>();
		if (pawn_)
		{
			pawn_->getEventDispatcher().addEventHandler(this);
		}
	}

	void onReady() override
	{
		addNetworkHandlers();

		if (!pawn_)
		{
			return;
		}

		if (IPawnScript* script = pawn_->mainScript())
		{
			registerNatives(*script);
		}
		for (IPawnScript* script : pawn_->sideScripts())
		{
			if (script)
			{
				registerNatives(*script);
			}
		}
	}

	void onFree(IComponent* component) override
	{
		if (component == pawn_)
		{
			pawn_->getEventDispatcher().removeEventHandler(this);
			pawn_ = nullptr;
		}
	}

	void onAmxLoad(IPawnScript& script) override
	{
		registerNatives(script);
	}

	void onAmxUnload(IPawnScript& script) override
	{
	}

	void onPlayerConnect(IPlayer& player) override
	{
		refreshAdvertisedSlots();
	}

	void onPlayerDisconnect(IPlayer& player, PeerDisconnectReason reason) override
	{
		refreshAdvertisedSlots(player.isBot() ? 0 : 1);
	}

	bool onSendRPC(IPlayer* peer, int id, NetworkBitStream& bs) override
	{
		if (id == PlayerInitRpc && initGameHostnameActive_)
		{
			rewritePlayerInitHostname(bs);
		}
		return true;
	}

	void free() override
	{
		delete this;
	}

	void reset() override
	{
		active_ = false;
		requestedSlots_ = 0;
		advertisedSlots_ = 0;
		applyAdvertisedSlots(realMaxPlayers());
	}

	int setAdvertisedSlots(int slots)
	{
		const int maxPlayers = realMaxPlayers();
		if (maxPlayers <= 0)
		{
			return 0;
		}

		const int playerCount = currentHumanPlayers();
		const int requested = std::clamp(slots, 0, maxPlayers);
		const int clamped = std::max(requested, playerCount);
		if (!applyAdvertisedSlots(clamped))
		{
			return 0;
		}

		requestedSlots_ = static_cast<uint16_t>(requested);
		advertisedSlots_ = static_cast<uint16_t>(clamped);
		active_ = requested != maxPlayers;
		return clamped;
	}

	int getAdvertisedSlots() const
	{
		return active_ ? advertisedSlots_ : realMaxPlayers();
	}

	int resetAdvertisedSlots()
	{
		const int maxPlayers = realMaxPlayers();
		if (maxPlayers <= 0 || !applyAdvertisedSlots(maxPlayers))
		{
			return 0;
		}

		active_ = false;
		requestedSlots_ = static_cast<uint16_t>(maxPlayers);
		advertisedSlots_ = static_cast<uint16_t>(maxPlayers);
		return maxPlayers;
	}

	bool setInitGameHostname(StringView hostname)
	{
		if (hostname.length() > MaxInitGameHostname)
		{
			hostname = hostname.substr(0, MaxInitGameHostname);
		}

		initGameHostname_ = Impl::String(hostname);
		initGameHostnameActive_ = true;
		return true;
	}

	bool resetInitGameHostname()
	{
		initGameHostname_.clear();
		initGameHostnameActive_ = false;
		return true;
	}

	bool hasInitGameHostname() const
	{
		return initGameHostnameActive_;
	}

private:
	void addNetworkHandlers()
	{
		if (!core_ || networkHandlersRegistered_)
		{
			return;
		}

		for (INetwork* network : core_->getNetworks())
		{
			if (network)
			{
				network->getOutEventDispatcher().addEventHandler(this, EventPriority_Lowest);
			}
		}
		networkHandlersRegistered_ = true;
	}

	void removeNetworkHandlers()
	{
		if (!core_ || !networkHandlersRegistered_)
		{
			return;
		}

		for (INetwork* network : core_->getNetworks())
		{
			if (network)
			{
				network->getOutEventDispatcher().removeEventHandler(this);
			}
		}
		networkHandlersRegistered_ = false;
	}

	void refreshAdvertisedSlots(int disconnectingHumanPlayers = 0)
	{
		if (!active_)
		{
			return;
		}

		const int maxPlayers = realMaxPlayers();
		const int playerCount = std::max(0, currentHumanPlayers() - disconnectingHumanPlayers);
		const int clamped = std::clamp<int>(std::max<int>(requestedSlots_, playerCount), 0, maxPlayers);
		if (applyAdvertisedSlots(clamped))
		{
			advertisedSlots_ = static_cast<uint16_t>(clamped);
		}
	}

	void registerNatives(IPawnScript& script)
	{
		static const AMX_NATIVE_INFO natives[] = {
			{ "SetQueryMaxPlayers", &SetQueryMaxPlayers },
			{ "GetQueryMaxPlayers", &GetQueryMaxPlayers },
			{ "ResetQueryMaxPlayers", &ResetQueryMaxPlayers },
			{ "SetInitGameHostname", &SetInitGameHostname },
			{ "ResetInitGameHostname", &ResetInitGameHostname },
			{ "IsInitGameHostnameSet", &IsInitGameHostnameSet },
		};

		for (const AMX_NATIVE_INFO& native : natives)
		{
			int index = 0;
			if (script.FindNative(native.name, &index) != 0)
			{
				script.Register(&native, 1);
			}
		}
	}

	int realMaxPlayers() const
	{
		if (!core_)
		{
			return 0;
		}

		int* maxPlayers = core_->getConfig().getInt("max_players");
		return maxPlayers ? std::clamp(*maxPlayers, 0, int(std::numeric_limits<uint16_t>::max())) : 0;
	}

	int currentHumanPlayers() const
	{
		return core_ ? static_cast<int>(core_->getPlayers().players().size()) : 0;
	}

	bool applyAdvertisedSlots(int slots)
	{
		if (!core_)
		{
			return false;
		}

		bool patched = false;
		for (INetwork* network : core_->getNetworks())
		{
			if (!network || network->getNetworkType() != ENetworkType_RakNetLegacy)
			{
				continue;
			}

			uint16_t* queryMaxPlayers = findLegacyQueryMaxPlayers(*network);
			if (!queryMaxPlayers)
			{
				continue;
			}

			*queryMaxPlayers = static_cast<uint16_t>(slots);
			network->update();
			patched = true;
		}

		return patched;
	}

	uint16_t* findLegacyQueryMaxPlayers(INetwork& network) const
	{
		const uintptr_t expectedCore = reinterpret_cast<uintptr_t>(core_);
		auto* bytes = reinterpret_cast<unsigned char*>(&network);
		constexpr size_t ScanBytes = 4096;

		for (size_t offset = 0; offset + (sizeof(uintptr_t) * 4) < ScanBytes; offset += alignof(uintptr_t))
		{
			uintptr_t first;
			uintptr_t second;
			std::memcpy(&first, bytes + offset, sizeof(first));
			std::memcpy(&second, bytes + offset + sizeof(uintptr_t), sizeof(second));

			if (first == expectedCore && second == expectedCore)
			{
				return reinterpret_cast<uint16_t*>(bytes + offset + (sizeof(uintptr_t) * 3));
			}
		}

		return nullptr;
	}

	bool readPlayerInit(NetworkBitStream& bs, PlayerInitData& data) const
	{
		bs.resetReadPointer();
		return bs.readBIT(data.enableZoneNames)
			&& bs.readBIT(data.usePlayerPedAnims)
			&& bs.readBIT(data.allowInteriorWeapons)
			&& bs.readBIT(data.useLimitGlobalChatRadius)
			&& bs.readFLOAT(data.limitGlobalChatRadius)
			&& bs.readBIT(data.enableStuntBonus)
			&& bs.readFLOAT(data.nameTagDrawDistance)
			&& bs.readBIT(data.disableInteriorEnterExits)
			&& bs.readBIT(data.disableNameTagLOS)
			&& bs.readBIT(data.manualVehicleEngineAndLights)
			&& bs.readUINT32(data.spawnInfoCount)
			&& bs.readUINT16(data.playerID)
			&& bs.readBIT(data.showNameTags)
			&& bs.readUINT32(data.showPlayerMarkers)
			&& bs.readUINT8(data.worldTime)
			&& bs.readUINT8(data.weather)
			&& bs.readFLOAT(data.gravity)
			&& bs.readBIT(data.lanMode)
			&& bs.readUINT32(data.deathDropAmount)
			&& bs.readBIT(data.instagib)
			&& bs.readUINT32(data.onFootRate)
			&& bs.readUINT32(data.inCarRate)
			&& bs.readUINT32(data.weaponRate)
			&& bs.readUINT32(data.multiplier)
			&& bs.readUINT32(data.lagCompensation)
			&& bs.readDynStr8(data.serverName)
			&& bs.readArray(Span<uint8_t>(data.vehicleModels.data(), data.vehicleModels.size()))
			&& bs.readUINT32(data.enableVehicleFriendlyFire);
	}

	void writePlayerInit(NetworkBitStream& bs, const PlayerInitData& data, StringView hostname) const
	{
		bs.resetWritePointer();
		bs.writeBIT(data.enableZoneNames);
		bs.writeBIT(data.usePlayerPedAnims);
		bs.writeBIT(data.allowInteriorWeapons);
		bs.writeBIT(data.useLimitGlobalChatRadius);
		bs.writeFLOAT(data.limitGlobalChatRadius);
		bs.writeBIT(data.enableStuntBonus);
		bs.writeFLOAT(data.nameTagDrawDistance);
		bs.writeBIT(data.disableInteriorEnterExits);
		bs.writeBIT(data.disableNameTagLOS);
		bs.writeBIT(data.manualVehicleEngineAndLights);
		bs.writeUINT32(data.spawnInfoCount);
		bs.writeUINT16(data.playerID);
		bs.writeBIT(data.showNameTags);
		bs.writeUINT32(data.showPlayerMarkers);
		bs.writeUINT8(data.worldTime);
		bs.writeUINT8(data.weather);
		bs.writeFLOAT(data.gravity);
		bs.writeBIT(data.lanMode);
		bs.writeUINT32(data.deathDropAmount);
		bs.writeBIT(data.instagib);
		bs.writeUINT32(data.onFootRate);
		bs.writeUINT32(data.inCarRate);
		bs.writeUINT32(data.weaponRate);
		bs.writeUINT32(data.multiplier);
		bs.writeUINT32(data.lagCompensation);
		bs.writeDynStr8(hostname);
		bs.writeArray(Span<const uint8_t>(data.vehicleModels.data(), data.vehicleModels.size()));
		bs.writeUINT32(data.enableVehicleFriendlyFire);
		bs.resetReadPointer();
	}

	void rewritePlayerInitHostname(NetworkBitStream& bs) const
	{
		PlayerInitData data;
		if (readPlayerInit(bs, data))
		{
			writePlayerInit(bs, data, StringView(initGameHostname_.data(), initGameHostname_.length()));
		}
	}

	bool readPawnString(AMX* amx, cell address, Impl::String& output, size_t maxLen) const
	{
		if (!pawn_)
		{
			return false;
		}

		IPawnScript* script = pawn_->getScript(amx);
		if (!script)
		{
			return false;
		}

		cell* phys = nullptr;
		if (script->GetAddr(address, &phys) != 0 || !phys)
		{
			return false;
		}

		int length = 0;
		if (script->StrLen(phys, &length) != 0 || length < 0)
		{
			return false;
		}

		const size_t safeLength = std::min<size_t>(static_cast<size_t>(length), maxLen);
		Impl::String buffer;
		buffer.resize(safeLength + 1);
		if (script->GetString(buffer.data(), phys, false, safeLength + 1) != 0)
		{
			return false;
		}

		buffer.resize(std::strlen(buffer.c_str()));
		output = buffer;
		return true;
	}

	static cell __CDECL SetQueryMaxPlayers(AMX*, const cell* params)
	{
		if (!self_ || params[0] < static_cast<cell>(sizeof(cell)))
		{
			return 0;
		}
		return self_->setAdvertisedSlots(params[1]);
	}

	static cell __CDECL GetQueryMaxPlayers(AMX*, const cell*)
	{
		return self_ ? self_->getAdvertisedSlots() : 0;
	}

	static cell __CDECL ResetQueryMaxPlayers(AMX*, const cell*)
	{
		return self_ ? self_->resetAdvertisedSlots() : 0;
	}

	static cell __CDECL SetInitGameHostname(AMX* amx, const cell* params)
	{
		if (!self_ || params[0] < static_cast<cell>(sizeof(cell)))
		{
			return 0;
		}

		Impl::String hostname;
		if (!self_->readPawnString(amx, params[1], hostname, MaxInitGameHostname))
		{
			return 0;
		}
		return self_->setInitGameHostname(StringView(hostname.data(), hostname.length())) ? 1 : 0;
	}

	static cell __CDECL ResetInitGameHostname(AMX*, const cell*)
	{
		return self_ && self_->resetInitGameHostname() ? 1 : 0;
	}

	static cell __CDECL IsInitGameHostnameSet(AMX*, const cell*)
	{
		return self_ && self_->hasInitGameHostname() ? 1 : 0;
	}
};

AdvancedQueryComponent* AdvancedQueryComponent::self_ = nullptr;

COMPONENT_ENTRY_POINT()
{
	return new AdvancedQueryComponent();
}
