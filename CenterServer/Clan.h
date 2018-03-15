#pragma once

#include <mutex>
#include <memory>
#include <unordered_map>
#include <functional>

#include "P_Header.h"
#include "Player.h"
#include "LockedQueue.h"

namespace Adoter
{
namespace pb = google::protobuf;

class Clan : public std::enable_shared_from_this<Clan>
{
private:
	Asset::Clan _stuff;
	bool _dirty = false;
	int64_t _clan_id = 0;
	std::unordered_map<int64_t, Asset::RoomQueryResult> _rooms; 
	std::vector<int64_t> _gaming_room_list;
public:
	Clan(const Asset::Clan& clan) { _clan_id = clan.clan_id(); _stuff = clan; }

	const Asset::Clan& Get() { return _stuff; }
	bool Load();
	int64_t GetID() { return _stuff.clan_id(); }
	int64_t GetHoster() { return _stuff.hoster_id(); }

	void Update();
	void Save(bool force = true);
	void OnDisMiss();
	void RemoveMember(int64_t player_id);
	void BroadCast(const pb::Message* message);
	void BroadCast(const pb::Message& message);

	bool CheckRoomCard(int32_t count);
	void ConsumeRoomCard(int32_t count);
	void AddRoomCard(int32_t count);
	int32_t GetRoomCard() { return _stuff.room_card_count(); }

	void OnRoomChanged(const Asset::ClanRoomStatusChanged* message);
	void OnRoomSync(const Asset::RoomQueryResult& room_query);
	
	int32_t OnApply(int64_t player_id, const std::string& player_name, Asset::ClanOperation* message);
	int32_t OnChangedInformation(std::shared_ptr<Player> player, Asset::ClanOperation* message);
	int32_t OnAgree(Asset::ClanOperation* message);
	int32_t OnDisAgree(std::shared_ptr<Player> player, Asset::ClanOperation* message);
	int32_t OnRecharge(std::shared_ptr<Player> player, int32_t count);
	void OnQueryMemberStatus(Asset::ClanOperation* message);
	void OnQueryRoomList(std::shared_ptr<Player> player, Asset::ClanOperation* message);
	void OnQueryGamingList(Asset::ClanOperation* message);
};

class ClanManager : public std::enable_shared_from_this<ClanManager>
{
private:
	std::mutex _mutex;
	std::unordered_map<int64_t, std::shared_ptr<Clan>> _clans; 
	int64_t _heart_count = 0;
	bool _loaded = false;
public:
	static ClanManager& Instance()
	{
		static ClanManager _instance;
		return _instance;
	}

	void Update(int32_t diff);
	void Load();

	void Remove(int64_t Clan_id);
	void Remove(std::shared_ptr<Clan> clan);
	void Emplace(int64_t Clan_id, std::shared_ptr<Clan> clan);

	std::shared_ptr<Clan> GetClan(int64_t clan_id);
	std::shared_ptr<Clan> Get(int64_t clan_id);

	void OnOperate(std::shared_ptr<Player> player, Asset::ClanOperation* message);
	void OnCreated(int64_t clan_id, std::shared_ptr<Clan> clan); //创建成功
	void OnGameServerBack(const Asset::ClanOperationSync& message);
	bool IsLocal(int64_t clan_id);
	bool GetClan(int64_t clan_id, Asset::Clan& clan);
};

#define ClanInstance ClanManager::Instance()
}
