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
#include <limits>
#include <string>

#include <Server/Components/Pawn/pawn.hpp>
#include <Server/Components/Pawn/Impl/pawn_natives.hpp>
#include <Server/Components/Pawn/Impl/pawn_impl.hpp>
#include "bitstream.hpp"

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

	static AdvancedQueryComponent* instance()
	{
		return self_;
	}

	void onLoad(ICore* core) override
	{
		core_ = core;
		core_->getPlayers().getPlayerConnectDispatcher().addEventHandler(this);
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
		constexpr size_t ScanBytes = 65536;
		constexpr size_t QueryHeaderSearchBytes = 256;
		const uint16_t realMax = static_cast<uint16_t>(realMaxPlayers());
		const uint16_t currentValue = active_ ? advertisedSlots_ : realMax;

		for (size_t offset = 0; offset + sizeof(uintptr_t) < ScanBytes; ++offset)
		{
			uintptr_t first;
			std::memcpy(&first, bytes + offset, sizeof(first));
			if (first != expectedCore)
			{
				continue;
			}

			const size_t searchEnd = std::min(offset + QueryHeaderSearchBytes, ScanBytes - sizeof(uint16_t));
			for (size_t secondOffset = offset + 1; secondOffset + sizeof(uintptr_t) < searchEnd; ++secondOffset)
			{
				uintptr_t second;
				std::memcpy(&second, bytes + secondOffset, sizeof(second));
				if (second != expectedCore)
				{
					continue;
				}

				for (size_t candidateOffset = secondOffset + sizeof(uintptr_t); candidateOffset + sizeof(uint16_t) < searchEnd; ++candidateOffset)
				{
					uint16_t candidate;
					std::memcpy(&candidate, bytes + candidateOffset, sizeof(candidate));
					if (candidate == realMax || candidate == currentValue)
					{
						return reinterpret_cast<uint16_t*>(bytes + candidateOffset);
					}
				}
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
