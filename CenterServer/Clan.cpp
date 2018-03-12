#include <string>

#include "Clan.h"
#include "Timer.h"
#include "Player.h"
#include "RedisManager.h"
#include "NameLimit.h"

namespace Adoter
{

extern int32_t g_server_id;
extern const Asset::CommonConst* g_const;

void Clan::Update()
{
	if (_dirty) Save();
}
	
bool Clan::Load()
{
	Asset::Clan clan;
	auto load = RedisInstance.Get("clan:" + std::to_string(_clan_id), clan);

	if (!load) return false;

	_stuff = clan;

	return true;
}

int32_t Clan::OnApply(std::shared_ptr<Player> player, Asset::ClanOperation* message)
{
	if (!player || !message) return Asset::ERROR_INNER;
	
	auto player_id = player->GetID();
	auto oper_type = message->oper_type();

	auto it = std::find_if(_stuff.mutable_message_list()->begin(), _stuff.mutable_message_list()->end(), [player_id](const Asset::SystemMessage& message){
				return player_id == message.player_id();	
			});

	if (it == _stuff.mutable_message_list()->end())
	{
		auto system_message = _stuff.mutable_message_list()->Add();
		system_message->set_player_id(player_id);
		system_message->set_name(player->GetName());
		system_message->set_oper_time(TimerInstance.GetTime());
		system_message->set_oper_type(oper_type);
	}
	else
	{
		it->set_oper_time(TimerInstance.GetTime());
		it->set_oper_type(oper_type);
	}

	message->set_oper_result(Asset::ERROR_SUCCESS);

	_dirty = true;
	return 0;
}

int32_t Clan::OnRecharge(std::shared_ptr<Player> player, int32_t count)
{
	if (!player || count <= 0) return Asset::ERROR_INNER;
			
	if (player->GetRoomCard() < count) return Asset::ERROR_CLAN_ROOM_CARD_NOT_ENOUGH; //房卡不足

	player->ConsumeRoomCard(Asset::ROOM_CARD_CHANGED_TYPE_RECHARGE_CLAN, count); //扣除馆长房卡
	return 0;
}

int32_t Clan::OnAgree(Asset::ClanOperation* message)
{
	if (!message) return Asset::ERROR_INNER;
	
	auto member_id = message->dest_player_id();
	auto oper_type = message->oper_type();

	//
	//申请列表状态更新
	//
	auto it = std::find_if(_stuff.mutable_message_list()->begin(), _stuff.mutable_message_list()->end(), [member_id](const Asset::SystemMessage& message){
				return member_id == message.player_id();	
			});

	if (it == _stuff.mutable_message_list()->end()) return Asset::ERROR_CLAN_NO_RECORD; //尚未申请记录
	
	//if (oper_type == it->oper_type()) return Asset::ERROR_SUCCESS; //状态一致

	it->set_oper_time(TimerInstance.GetTime());
	it->set_oper_type(oper_type);

	//
	//成员列表更新
	//
	auto it_ = std::find_if(_stuff.mutable_member_list()->begin(), _stuff.mutable_member_list()->end(), [member_id](const Asset::Clan_Member& member){
				return member_id == member.player_id();
			});

	if (it_ != _stuff.mutable_member_list()->end()) return Asset::ERROR_CLAN_HAS_JOINED; //已经是成员

	auto member_ptr = _stuff.mutable_member_list()->Add();
	member_ptr->set_player_id(it->player_id());
	member_ptr->set_name(it->name());
	member_ptr->set_status(Asset::CLAN_MEM_STATUS_TYPE_AVAILABLE);

	//message->set_oper_result(Asset::ERROR_SUCCESS);
	//player->SendProtocol(message);

	_dirty = true;
	return 0;
}
	
int32_t Clan::OnDisAgree(std::shared_ptr<Player> player, Asset::ClanOperation* message)
{
	if (!player || !message) return Asset::ERROR_INNER;
	
	auto member_id = message->dest_player_id();
	auto oper_type = message->oper_type();

	//
	//申请列表状态更新
	//
	auto it = std::find_if(_stuff.mutable_message_list()->begin(), _stuff.mutable_message_list()->end(), [member_id](const Asset::SystemMessage& message){
				return member_id == message.player_id();	
			});

	if (it == _stuff.mutable_message_list()->end()) return Asset::ERROR_CLAN_NO_RECORD; //尚未申请记录
	
	if (oper_type == it->oper_type()) return Asset::ERROR_SUCCESS; //状态一致

	it->set_oper_time(TimerInstance.GetTime());
	it->set_oper_type(oper_type);

	_dirty = true;
	return 0;
}

int32_t Clan::OnChangedInformation(std::shared_ptr<Player> player, Asset::ClanOperation* message)
{
	if (!player || !message) return Asset::ERROR_INNER;

	auto hoster_id = _stuff.hoster_id();

	if (hoster_id != player->GetID()) return Asset::ERROR_CLAN_NO_PERMISSION;

	auto name = message->name();
	auto announcement = message->announcement();

	if (name.size()) _stuff.set_name(name);
	if (announcement.size()) _stuff.set_name(announcement);

	_dirty = true;
	return 0;
}

void Clan::OnQueryMemberStatus(std::shared_ptr<Player> player, Asset::ClanOperation* message)
{
	if (!player || !message) return;

	for (int32_t i = 0; i < _stuff.member_list().size(); ++i)
	{
		auto member_ptr = _stuff.mutable_member_list(i);
		if (!member_ptr) continue;

		member_ptr->set_status(Asset::CLAN_MEM_STATUS_TYPE_AVAILABLE);
	
		Asset::Player stuff;

		auto loaded = RedisInstance.Get("player:" + std::to_string(member_ptr->player_id()), stuff);
		if (!loaded) continue;

		if (stuff.login_time()) //在线
		{
			if (stuff.room_id()) member_ptr->set_status(Asset::CLAN_MEM_STATUS_TYPE_GAMING); //游戏中
		}
		else if (stuff.logout_time())
		{
			member_ptr->set_status(Asset::CLAN_MEM_STATUS_TYPE_OFFLINE); //离线
		}
	}

	message->mutable_clan()->CopyFrom(_stuff);

	_dirty = true;
}

//
//茶馆当前牌局列表查询
//
//Client根据列表查询详细数据
//
void Clan::OnQueryRoomList(std::shared_ptr<Player> player, Asset::ClanOperation* message)
{
	if (!player || !message) return;

	auto room_query_start_index = message->query_start_index();
	if (room_query_start_index < 0 || room_query_start_index >= _stuff.room_list().size()) return;

	auto room_query_end_index = message->query_end_index();
	if (room_query_end_index < 0 || room_query_end_index >= _stuff.room_list().size()) return;

	for (int32_t i = room_query_start_index; i < room_query_end_index; ++i)
	{
		auto it = _rooms.find(_stuff.room_list(i));
		if (it == _rooms.end()) continue;
	
		auto room_battle = message->mutable_room_list()->Add();
		room_battle->CopyFrom(it->second);
	}
}

void Clan::Save(bool force)
{
	if (!force && !_dirty) return;
		
	RedisInstance.Save("clan:" + std::to_string(_clan_id), _stuff);

	_dirty = false;
}

void Clan::OnDisMiss()
{
	_stuff.set_dismiss(true); //解散

	_dirty = true;
}

void Clan::RemoveMember(int64_t player_id)
{
	for (int32_t i = 0; i < _stuff.member_list().size(); ++i)
	{
		if (player_id != _stuff.member_list(i).player_id()) continue;

		_stuff.mutable_member_list()->SwapElements(i, _stuff.member_list().size() - 1);
		_stuff.mutable_member_list()->RemoveLast(); //删除玩家
		break;
	}

	_dirty = true;
}

void Clan::BroadCast(const pb::Message* message)
{
	if (!message) return;

	BroadCast(*message);
}

void Clan::BroadCast(const pb::Message& message)
{
	for (const auto& member : _stuff.member_list())
	{
		auto member_ptr = PlayerInstance.Get(member.player_id());
		if (!member_ptr) continue;

		member_ptr->SendProtocol(message);
	}
}
	
bool Clan::CheckRoomCard(int32_t count)
{
	return _stuff.room_card_count() >= count;
}

void Clan::ConsumeRoomCard(int32_t count)
{
	if (count <= 0) return;

	_stuff.set_room_card_count(_stuff.room_card_count() - count);
	_dirty = true;
}

void Clan::AddRoomCard(int32_t count)
{
	if (count >= 0) return;

	_stuff.set_room_card_count(_stuff.room_card_count() + count);
	_dirty = true;
}
	
void Clan::OnRoomChanged(const Asset::ClanRoomStatusChanged* message)
{
	if (!message) return;

	switch (message->status())
	{
		case Asset::CLAN_ROOM_STATUS_TYPE_START:
		{
			const auto room_card = dynamic_cast<const Asset::Item_RoomCard*>(AssetInstance.Get(g_const->room_card_id()));
			if (!room_card || room_card->rounds() <= 0) return;

			auto consume_count = message->room().options().open_rands() / room_card->rounds(); //待消耗房卡数
			if (consume_count <= 0) return;

			if (!CheckRoomCard(consume_count)) 
			{
				LOG(ERROR, "茶馆房间消耗房卡失败,然而已经开局,数据:{}", message->ShortDebugString());
				return;
			}

			ConsumeRoomCard(consume_count);
		}
		break;
		
		case Asset::CLAN_ROOM_STATUS_TYPE_OVER:
		{
			_rooms.erase(message->room().room_id());

			for (int32_t i = 0; i < _stuff.room_list().size(); ++i)
			{
				auto room_id = _stuff.room_list(i);
				if (room_id != message->room().room_id()) continue;

				_stuff.mutable_room_list()->SwapElements(i, _stuff.room_list().size() - 1);
				_stuff.mutable_room_list()->RemoveLast();
				break;
			}
		}
		break;
	}

	_dirty = true;
}

void Clan::OnRoomSync(const Asset::RoomQueryResult& room_query)
{
	auto room_id = room_query.room_id();
	if (room_id <= 0) return;

	_rooms[room_id] = room_query;

	auto it = std::find(_stuff.room_list().begin(), _stuff.room_list().end(), room_id);
	if (it == _stuff.room_list().end()) _stuff.mutable_room_list()->Add(room_id);

	_dirty = true;
}

void ClanManager::Update(int32_t diff)
{
	if (_heart_count % 60 != 0) return;  //3秒

	++_heart_count;

	std::lock_guard<std::mutex> lock(_mutex);
	
	for (auto it = _clans.begin(); it != _clans.end();)
	{
		if (!it->second)
		{
			it = _clans.erase(it);
			continue; 
		}
		else
		{
			it->second->Update();
			++it;
		}
	}

	Load();
}
	
void ClanManager::Load()
{
	if (_loaded) return;

	std::vector<std::string> clan_list;
	bool has_record = RedisInstance.GetArray("clan:*", clan_list);	
	if (!has_record)
	{
		WARN("加载茶馆数据失败，可能是本服尚未创建茶馆，加载成功数量:{}", clan_list.size());
		return;
	}

	for (const auto& value : clan_list)
	{
		Asset::Clan clan;
		auto success = clan.ParseFromString(value);
		if (!success) continue;

		auto clan_ptr = std::make_shared<Clan>(clan);
		if (!clan_ptr) return;

		Emplace(clan.clan_id(), clan_ptr);
	}
		
	DEBUG("加载茶馆数据成功，加载成功数量:{}", clan_list.size());

	_loaded = true;
}

void ClanManager::Remove(int64_t clan_id)
{
	std::lock_guard<std::mutex> lock(_mutex);

	if (clan_id <= 0) return;

	auto it = _clans.find(clan_id);
	if (it == _clans.end()) return;
		
	if (it->second) 
	{
		it->second->OnDisMiss(); //解散
		it->second->Save();
		it->second.reset();
	}

	_clans.erase(it);
}

void ClanManager::Remove(std::shared_ptr<Clan> clan)
{
	if (!clan) return;

	Remove(clan->GetID());
}

void ClanManager::Emplace(int64_t clan_id, std::shared_ptr<Clan> clan)
{
	if (clan_id <= 0 || !clan) return;

	_clans[clan_id] = clan;

	DEBUG("添加茶馆:{} 成功，当前茶馆数量:{}", clan_id, _clans.size());
}

std::shared_ptr<Clan> ClanManager::GetClan(int64_t clan_id)
{
	auto it = _clans.find(clan_id);
	if (it == _clans.end()) return nullptr;

	return it->second;
}

std::shared_ptr<Clan> ClanManager::Get(int64_t clan_id)
{
	return GetClan(clan_id);
}

int32_t ClanManager::OnOperate(std::shared_ptr<Player> player, Asset::ClanOperation* message)
{
	if (!message || !player) return 1;
	
	if (message->server_id() == g_server_id)
	{
		WARN("服务器:{} 收到茶馆操作数据:{}, g_server_id, message->ShortDebugString()"); //收到本服发送的协议数据，不再处理
		return 3;
	}
	
	defer {
		player->SendProtocol(message); //返回结果
	};
			
	std::shared_ptr<Clan> clan = nullptr;
	
	if (message->oper_type() != Asset::CLAN_OPER_TYPE_CREATE && message->oper_type() != Asset::CLAN_OPER_TYPE_CLAN_LIST_QUERY) 
	{
		clan = ClanInstance.Get(message->clan_id());

		if (!clan) //创建茶馆//列表查询无需检查
		{
			message->set_oper_result(Asset::ERROR_CLAN_NOT_FOUND); //没找到茶馆
			return 2;
		}
	}

	message->set_server_id(g_server_id);

	switch (message->oper_type())
	{
		case Asset::CLAN_OPER_TYPE_CREATE: //创建
		{
			player->SendProtocol2GameServer(message);
		}
		break;
	
		case Asset::CLAN_OPER_TYPE_JOIN: //申请加入
		{
			auto result = clan->OnApply(player, message); //申请成功
			message->set_oper_result(result); 
		}
		break;
	
		case Asset::CLAN_OPER_TYPE_EDIT: //修改基本信息
		{
			auto result = clan->OnChangedInformation(player, message);
			message->set_oper_result(result); 
		}
		break;
	
		case Asset::CLAN_OPER_TYPE_DISMISS: //解散
		{
			if (clan->GetHoster() != player->GetID())
			{
				message->set_oper_result(Asset::ERROR_CLAN_NO_PERMISSION);
				return 10;
			}

			Remove(message->clan_id());
		}
		break;
		
		case Asset::CLAN_OPER_TYPE_MEMEBER_AGEE: //同意加入
		{
			if (clan->GetHoster() != player->GetID())
			{
				message->set_oper_result(Asset::ERROR_CLAN_NO_PERMISSION);
				return 11;
			}
			
			auto result = clan->OnAgree(message); //列表更新
			message->set_oper_result(result); 

			if (result != 0) return 14; //失败
			
			player->SendProtocol2GameServer(message); //通知逻辑服务器加入成功
		
			Asset::Player des_player;
			if (!PlayerInstance.GetCache(message->dest_player_id(), des_player)) return 13; //没有记录

			if (des_player.login_time() == 0) //离线
			{
				des_player.add_clan_joiners(message->clan_id());

				PlayerInstance.Save(message->dest_player_id(), des_player); //直接存盘
			}
			else //在线 
			{
				auto des_player = PlayerInstance.Get(message->dest_player_id()); //不在当前中心服务器
				if (!des_player) return 12;

				message->mutable_clan()->CopyFrom(clan->Get()); //茶馆信息

				des_player->OnClanJoin(message->clan_id());
				des_player->SendProtocol(message); //通知玩家馆长同意加入茶馆
				des_player->SendProtocol2GameServer(message); //通知逻辑服务器加入茶馆成功
			}
		}
		break;
		
		case Asset::CLAN_OPER_TYPE_MEMEBER_DISAGEE: //拒绝加入
		{
			if (clan->GetHoster() != player->GetID())
			{
				message->set_oper_result(Asset::ERROR_CLAN_NO_PERMISSION);
				return 10;
			}
			
			auto result = clan->OnDisAgree(player, message);
			message->set_oper_result(result); 
		}
		break;
		
		case Asset::CLAN_OPER_TYPE_MEMEBER_DELETE: //删除成员
		{
			if (clan->GetHoster() != player->GetID())
			{
				message->set_oper_result(Asset::ERROR_CLAN_NO_PERMISSION);
				return 10;
			}
			
			clan->RemoveMember(message->dest_player_id());
		}
		break;
		
		case Asset::CLAN_OPER_TYPE_MEMEBER_QUIT: //主动退出
		{
			clan->RemoveMember(player->GetID());
		}
		break;
		
		case Asset::CLAN_OPER_TYPE_RECHARGE: //充值
		{
			auto result = clan->OnRecharge(player, message->recharge_count());
			message->set_oper_result(result); 
		}

		case Asset::CLAN_OPER_TYPE_MEMEBER_QUERY: //成员状态查询
		{
			clan->OnQueryMemberStatus(player, message);
		}
		break;

		case Asset::CLAN_OPER_TYPE_ROOM_LIST_QUERY:
		{
			clan->OnQueryRoomList(player, message);
		}
		break;
		
		case Asset::CLAN_OPER_TYPE_CLAN_LIST_QUERY:
		{
			player->SendProtocol2GameServer(message);
		}
		break;
	
		default:
		{
			ERROR("玩家:{} 茶馆操作尚未处理:{}", player->GetID(), message->ShortDebugString());
			return 0;
		}
		break;
	}
			

	return 0;
}
	
void ClanManager::OnCreated(int64_t clan_id, std::shared_ptr<Clan> clan)
{
	if (clan_id <= 0 || !clan) return;
		
	Emplace(clan_id, clan);

	DEBUG("创建茶馆:{} 成功", clan_id);
}
	
void ClanManager::OnGameServerBack(const Asset::ClanOperationSync& message)
{
	const auto& operation = message.operation();
	if (operation.oper_result() != 0) return; //执行失败

	if (g_server_id == operation.server_id()) return; //本服不再处理

	auto clan_id = operation.clan_id();
	
	switch (operation.oper_type())
	{
		case Asset::CLAN_OPER_TYPE_CREATE: //创建
		{
			auto clan_ptr = std::make_shared<Clan>(operation.clan());
			clan_ptr->Save(true);

			OnCreated(clan_id, clan_ptr); //创建成功
		}
		break;
		
		case Asset::CLAN_OPER_TYPE_MEMEBER_AGEE: //同意加入
		{
			auto clan_ptr = Get(clan_id);
			if (!clan_ptr) return;

			auto clan_operation = operation;

			auto result = clan_ptr->OnAgree(&clan_operation); //列表更新
			clan_operation.set_oper_result(result); 

			auto des_player = PlayerInstance.Get(operation.dest_player_id()); //不在当前中心服务器
			if (!des_player) return;

			clan_operation.mutable_clan()->CopyFrom(clan_ptr->Get()); //茶馆信息

			des_player->OnClanJoin(operation.clan_id());
			des_player->SendProtocol(clan_operation); //通知玩家馆长同意加入茶馆
			des_player->SendProtocol2GameServer(clan_operation); //通知逻辑服务器加入茶馆成功
		}
		break;

		default:
		{
			auto clan_ptr = Get(clan_id);
			if (!clan_ptr) return;

			clan_ptr->Load();
		}
		break;
	}

	DEBUG("接收逻辑服务器返回茶馆操作数据:{}", operation.ShortDebugString());
}

}

