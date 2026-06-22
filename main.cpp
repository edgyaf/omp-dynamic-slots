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
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

#if OMP_BUILD_PLATFORM == OMP_WINDOWS
#include <Windows.h>
#endif

#include <Server/Components/Pawn/pawn.hpp>
#include <Server/Components/Pawn/Impl/pawn_natives.hpp>
#include <Server/Components/Pawn/Impl/pawn_impl.hpp>
#include "bitstream.hpp"

class AdvancedQueryComponent final : public IComponent, public CoreEventHandler, public PawnEventHandler, public PlayerConnectEventHandler, public NetworkOutEventHandler
{
private:
	static constexpr int PlayerInitRpc = 139;
	static constexpr int QueryPatchRetryTicks = 200;
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

	struct MemoryWindow
	{
		unsigned char* data = nullptr;
		size_t length = 0;
	};

	static AdvancedQueryComponent* self_;

	ICore* core_ = nullptr;
	IPawnComponent* pawn_ = nullptr;
	uint16_t requestedSlots_ = 0;
	uint16_t advertisedSlots_ = 0;
	uint16_t patchedInternalSlots_ = 0;
	bool active_ = false;
	Impl::String initGameHostname_;
	bool initGameHostnameActive_ = false;
	bool networkHandlersRegistered_ = false;
	bool queryPatchPending_ = false;
	int queryPatchRetriesLeft_ = 0;
	bool warnedNoLegacyNetwork_ = false;
	bool warnedQueryPatchFailed_ = false;

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
			core_->getEventDispatcher().removeEventHandler(this);
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

	static AdvancedQueryComponent* instance()
	{
		return self_;
	}

	void onLoad(ICore* core) override
	{
		core_ = core;
		core_->getEventDispatcher().addEventHandler(this);
		core_->getPlayers().getPlayerConnectDispatcher().addEventHandler(this, EventPriority_Lowest);
		setAmxLookups(core_);
	}

	void onInit(IComponentList* components) override
	{
		pawn_ = components->queryComponent<IPawnComponent>();
		if (pawn_)
		{
			setAmxFunctions(pawn_->getAmxFunctions());
			setAmxLookups(components);
			pawn_->getEventDispatcher().addEventHandler(this);
		}
	}

	void onReady() override
	{
		addNetworkHandlers();
	}

	void onFree(IComponent* component) override
	{
		if (component == pawn_)
		{
			pawn_->getEventDispatcher().removeEventHandler(this);
			pawn_ = nullptr;
			setAmxFunctions();
			setAmxLookups();
		}
	}

	void onAmxLoad(IPawnScript& script) override
	{
		pawn_natives::AmxLoad(script.GetAMX());
	}

	void onAmxUnload(IPawnScript& script) override
	{
	}

	void onTick(Microseconds elapsed, TimePoint now) override
	{
		if (!queryPatchPending_)
		{
			return;
		}

		if (applyAdvertisedSlots(active_ ? advertisedSlots_ : publicMaxPlayersLimit(), false))
		{
			queryPatchPending_ = false;
			queryPatchRetriesLeft_ = 0;
			return;
		}

		if (queryPatchRetriesLeft_ > 0)
		{
			--queryPatchRetriesLeft_;
		}
		if (queryPatchRetriesLeft_ == 0 && !warnedQueryPatchFailed_)
		{
			core_->logLn(LogLevel::Warning, "Advanced Query could not locate LegacyNetwork query data in this open.mp build.");
			warnedQueryPatchFailed_ = true;
			queryPatchPending_ = false;
		}
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
		patchedInternalSlots_ = 0;
		applyAdvertisedSlotsOrRetry(publicMaxPlayersLimit());
	}

	int setAdvertisedSlots(int slots)
	{
		const int maxPlayers = realMaxPlayers();
		if (maxPlayers <= 0)
		{
			return 0;
		}

		const int humanPlayers = currentHumanPlayers();
		const int publicMaxPlayers = publicMaxPlayersLimit();
		const int requested = std::clamp(slots, 0, publicMaxPlayers);
		const int clamped = std::max(requested, humanPlayers);
		requestedSlots_ = static_cast<uint16_t>(requested);
		advertisedSlots_ = static_cast<uint16_t>(clamped);
		active_ = requested != publicMaxPlayers;
		applyAdvertisedSlotsOrRetry(clamped);
		return clamped;
	}

	int getAdvertisedSlots() const
	{
		return active_ ? advertisedSlots_ : publicMaxPlayersLimit();
	}

	int resetAdvertisedSlots()
	{
		const int publicMaxPlayers = publicMaxPlayersLimit();
		if (publicMaxPlayers <= 0)
		{
			return 0;
		}

		active_ = false;
		requestedSlots_ = static_cast<uint16_t>(publicMaxPlayers);
		advertisedSlots_ = static_cast<uint16_t>(publicMaxPlayers);
		applyAdvertisedSlotsOrRetry(publicMaxPlayers);
		return publicMaxPlayers;
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

		const int publicMaxPlayers = publicMaxPlayersLimit();
		const int humanPlayers = std::max(0, currentHumanPlayers() - disconnectingHumanPlayers);
		const int clamped = std::clamp<int>(std::max<int>(requestedSlots_, humanPlayers), 0, publicMaxPlayers);
		advertisedSlots_ = static_cast<uint16_t>(clamped);
		applyAdvertisedSlotsOrRetry(clamped);
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
		return std::max(0, currentTotalPlayers() - currentBotPlayers());
	}

	int currentTotalPlayers() const
	{
		return core_ ? static_cast<int>(core_->getPlayers().players().size()) : 0;
	}

	int currentBotPlayers() const
	{
		return core_ ? static_cast<int>(core_->getPlayers().bots().size()) : 0;
	}

	int publicMaxPlayersLimit() const
	{
		return std::max(0, realMaxPlayers() - currentBotPlayers());
	}

	bool applyAdvertisedSlotsOrRetry(int slots)
	{
		if (applyAdvertisedSlots(slots, false))
		{
			queryPatchPending_ = false;
			queryPatchRetriesLeft_ = 0;
			return true;
		}

		queryPatchPending_ = true;
		queryPatchRetriesLeft_ = QueryPatchRetryTicks;
		return false;
	}

	bool applyAdvertisedSlots(int slots, bool warn)
	{
		if (!core_)
		{
			return false;
		}

		const int internalSlots = std::clamp(slots + currentBotPlayers(), 0, realMaxPlayers());
		bool patched = false;
		bool foundLegacyNetwork = false;
		for (INetwork* network : core_->getNetworks())
		{
			if (!network || network->getNetworkType() != ENetworkType_RakNetLegacy)
			{
				continue;
			}
			foundLegacyNetwork = true;

			if (uint16_t* queryMaxPlayers = findLegacyQueryMaxPlayers(*network))
			{
				patchedInternalSlots_ = static_cast<uint16_t>(internalSlots);
				*queryMaxPlayers = patchedInternalSlots_;
				network->update();
				patched = true;
			}
		}

		if (warn && !foundLegacyNetwork && !warnedNoLegacyNetwork_)
		{
			core_->logLn(LogLevel::Warning, "Advanced Query could not find a RakNet legacy network to patch query slots.");
			warnedNoLegacyNetwork_ = true;
		}
		else if (warn && foundLegacyNetwork && !patched && !warnedQueryPatchFailed_)
		{
			core_->logLn(LogLevel::Warning, "Advanced Query could not locate LegacyNetwork query data in this open.mp build.");
			warnedQueryPatchFailed_ = true;
		}

		return patched;
	}

	uint16_t* findLegacyQueryMaxPlayers(INetwork& network) const
	{
		auto* completeObject = static_cast<unsigned char*>(dynamic_cast<void*>(&network));
		if (uint16_t* value = findLegacyQueryMaxPlayersInWindow(memoryWindow(completeObject, 0, 65536)))
		{
			return value;
		}

		auto* networkInterface = reinterpret_cast<unsigned char*>(&network);
		if (networkInterface != completeObject)
		{
			return findLegacyQueryMaxPlayersInWindow(memoryWindow(networkInterface, 4096, 65536));
		}

		return nullptr;
	}

	uint16_t* findLegacyQueryMaxPlayersInWindow(MemoryWindow window) const
	{
		if (!window.data || window.length == 0)
		{
			return nullptr;
		}

		const uintptr_t expectedCore = reinterpret_cast<uintptr_t>(core_);
		constexpr size_t QueryMaxPlayersOffset = sizeof(uintptr_t) + sizeof(uintptr_t);
		const uint16_t realMax = static_cast<uint16_t>(realMaxPlayers());
		const uint16_t currentValue = patchedInternalSlots_ ? patchedInternalSlots_ : realMax;
		const uint16_t desiredValue = active_ ? static_cast<uint16_t>(std::clamp<int>(advertisedSlots_ + currentBotPlayers(), 0, realMax)) : realMax;

		for (size_t offset = 0; offset + QueryMaxPlayersOffset + sizeof(uint16_t) < window.length; ++offset)
		{
			uintptr_t queryCore;
			std::memcpy(&queryCore, window.data + offset, sizeof(queryCore));
			if (queryCore != expectedCore)
			{
				continue;
			}

			const size_t candidateOffset = offset + QueryMaxPlayersOffset;
			uint16_t candidate;
			std::memcpy(&candidate, window.data + candidateOffset, sizeof(candidate));
			if (candidate == realMax || candidate == currentValue || candidate == desiredValue)
			{
				return reinterpret_cast<uint16_t*>(window.data + candidateOffset);
			}
		}

		return nullptr;
	}

	MemoryWindow memoryWindow(const void* address, size_t bytesBefore, size_t bytesAfter) const
	{
		if (!address || (bytesBefore == 0 && bytesAfter == 0))
		{
			return {};
		}

#if OMP_BUILD_PLATFORM == OMP_WINDOWS
		MEMORY_BASIC_INFORMATION mbi {};
		if (!VirtualQuery(address, &mbi, sizeof(mbi)))
		{
			return {};
		}

		const auto start = reinterpret_cast<uintptr_t>(address);
		const auto regionStart = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
		const uintptr_t regionEnd = regionStart + static_cast<uintptr_t>(mbi.RegionSize);
		if (mbi.State != MEM_COMMIT || start < regionStart || start >= regionEnd || !hasReadableProtection(mbi.Protect))
		{
			return {};
		}

		const uintptr_t windowStart = start - std::min(bytesBefore, static_cast<size_t>(start - regionStart));
		const uintptr_t requestedEnd = start + bytesAfter;
		const uintptr_t windowEnd = std::min(requestedEnd, regionEnd);
		return { reinterpret_cast<unsigned char*>(windowStart), static_cast<size_t>(windowEnd - windowStart) };
#else
		std::ifstream maps("/proc/self/maps");
		if (!maps)
		{
			return {};
		}

		const auto start = reinterpret_cast<uintptr_t>(address);
		Impl::String line;
		while (std::getline(maps, line))
		{
			std::istringstream stream(line);
			Impl::String range;
			Impl::String perms;
			if (!(stream >> range >> perms))
			{
				continue;
			}

			const size_t dash = range.find('-');
			if (dash == Impl::String::npos)
			{
				continue;
			}

			const uintptr_t regionStart = static_cast<uintptr_t>(std::stoull(range.substr(0, dash), nullptr, 16));
			const uintptr_t regionEnd = static_cast<uintptr_t>(std::stoull(range.substr(dash + 1), nullptr, 16));
			if (start >= regionStart && start < regionEnd && !perms.empty() && perms[0] == 'r')
			{
				const uintptr_t windowStart = start - std::min(bytesBefore, static_cast<size_t>(start - regionStart));
				const uintptr_t requestedEnd = start + bytesAfter;
				const uintptr_t windowEnd = std::min(requestedEnd, regionEnd);
				return { reinterpret_cast<unsigned char*>(windowStart), static_cast<size_t>(windowEnd - windowStart) };
			}
		}
		return {};
#endif
	}

#if OMP_BUILD_PLATFORM == OMP_WINDOWS
	bool hasReadableProtection(DWORD protect) const
	{
		if (protect & (PAGE_GUARD | PAGE_NOACCESS))
		{
			return false;
		}

		switch (protect & 0xff)
		{
		case PAGE_READONLY:
		case PAGE_READWRITE:
		case PAGE_WRITECOPY:
		case PAGE_EXECUTE_READ:
		case PAGE_EXECUTE_READWRITE:
		case PAGE_EXECUTE_WRITECOPY:
			return true;
		default:
			return false;
		}
	}
#endif

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
};

AdvancedQueryComponent* AdvancedQueryComponent::self_ = nullptr;

COMPONENT_ENTRY_POINT()
{
	return new AdvancedQueryComponent();
}

SCRIPT_API(SetQueryMaxPlayers, int(int slots))
{
	if (AdvancedQueryComponent* component = AdvancedQueryComponent::instance())
	{
		return component->setAdvertisedSlots(slots);
	}
	return 0;
}

SCRIPT_API(GetQueryMaxPlayers, int())
{
	if (AdvancedQueryComponent* component = AdvancedQueryComponent::instance())
	{
		return component->getAdvertisedSlots();
	}
	return 0;
}

SCRIPT_API(ResetQueryMaxPlayers, int())
{
	if (AdvancedQueryComponent* component = AdvancedQueryComponent::instance())
	{
		return component->resetAdvertisedSlots();
	}
	return 0;
}

SCRIPT_API(SetInitGameHostname, bool(std::string const& hostname))
{
	if (AdvancedQueryComponent* component = AdvancedQueryComponent::instance())
	{
		return component->setInitGameHostname(StringView(hostname.data(), hostname.length()));
	}
	return false;
}

SCRIPT_API(ResetInitGameHostname, bool())
{
	if (AdvancedQueryComponent* component = AdvancedQueryComponent::instance())
	{
		return component->resetInitGameHostname();
	}
	return false;
}

SCRIPT_API(IsInitGameHostnameSet, bool())
{
	if (AdvancedQueryComponent* component = AdvancedQueryComponent::instance())
	{
		return component->hasInitGameHostname();
	}
	return false;
}
