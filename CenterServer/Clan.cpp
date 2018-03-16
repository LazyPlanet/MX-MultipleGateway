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

	if (!ClanInstance.IsLocal(_clan_id)) Load(); //从茶馆加载数据
}
	
bool Clan::Load()
{
	ClanInstance.GetClan(_clan_id, _stuff);

	DEBUG("茶馆:{} 加载数据:{}", _clan_id, _stuff.ShortDebugString());

	return true;
}

int32_t Clan::OnApply(int64_t player_id, const std::string& player_name, Asset::ClanOperation* message)
{
	if (!message) return Asset::ERROR_INNER;
	
	auto oper_type = message->oper_type();

	auto it = std::find_if(_stuff.mutable_message_list()->begin(), _stuff.mutable_message_list()->end(), [player_id](const Asset::SystemMessage& message){
				return player_id == message.player_id();	
			});

	if (it == _stuff.mutable_message_list()->end())
	{
		auto system_message = _stuff.mutable_message_list()->Add();
		system_message->set_player_id(player_id);
		system_message->set_name(player_name);
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

int32_t Clan::OnRecharge(int32_t count)
{
	AddRoomCard(count);

	DEBUG("成功给茶馆:{} 充值房卡:{}", _clan_id, count);
	return 0;
}

int32_t Clan::OnAgree(Asset::ClanOperation* message)
{
	if (!message) return Asset::ERROR_INNER;
	
	auto member_id = message->dest_player_id();
	auto dest_sys_message_index = message->dest_sys_message_index() - 1;

	if (dest_sys_message_index < 0 || dest_sys_message_index > _stuff.message_list().size()) return Asset::ERROR_CLAN_NO_RECORD; //尚未申请记录

	//
	//申请列表状态更新
	//

	const auto& sys_message = _stuff.message_list(dest_sys_message_index);
	if (member_id != sys_message.player_id() || Asset::CLAN_OPER_TYPE_JOIN != sys_message.oper_type()) return Asset::ERROR_CLAN_NO_RECORD; //尚未申请记录

	auto it = _stuff.mutable_message_list(dest_sys_message_index);

	it->set_oper_time(TimerInstance.GetTime());
	it->set_oper_type(message->oper_type());

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

	_dirty = true;

	return 0;
}
	
int32_t Clan::OnDisAgree(std::shared_ptr<Player> player, Asset::ClanOperation* message)
{
	if (!player || !message) return Asset::ERROR_INNER;
	
	auto member_id = message->dest_player_id();
	auto dest_sys_message_index = message->dest_sys_message_index() - 1;
	
	if (dest_sys_message_index < 0 || dest_sys_message_index > _stuff.message_list().size()) return Asset::ERROR_CLAN_NO_RECORD; //尚未申请记录

	//
	//申请列表状态更新
	//

	const auto& sys_message = _stuff.message_list(dest_sys_message_index);
	if (member_id != sys_message.player_id() || Asset::CLAN_OPER_TYPE_JOIN != sys_message.oper_type()) return Asset::ERROR_CLAN_NO_RECORD; //尚未申请记录

	auto it = _stuff.mutable_message_list(dest_sys_message_index);

	it->set_oper_time(TimerInstance.GetTime());
	it->set_oper_type(message->oper_type());

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

	if (name.size()) 
	{
		if (!NameLimitInstance.IsValid(name)) return Asset::ERROR_CLAN_NAME_INVALID;

		_stuff.set_name(name);
	}
	if (announcement.size()) 
	{
		if (!NameLimitInstance.IsValid(announcement)) return Asset::ERROR_CLAN_ANNOUCEMENT_INVALID;

		_stuff.set_announcement(announcement);
	}

	_dirty = true;

	return 0;
}

void Clan::OnQueryMemberStatus(Asset::ClanOperation* message)
{
	if (!message) return;

	message->mutable_clan()->CopyFrom(_stuff); //茶馆数据

	for (int32_t i = 0; i < _stuff.member_list().size(); ++i)
	{
		auto member_ptr = _stuff.mutable_member_list(i);
		if (!member_ptr) continue;

		member_ptr->set_status(Asset::CLAN_MEM_STATUS_TYPE_AVAILABLE);
	
		Asset::Player player;

		auto loaded = PlayerInstance.GetCache(member_ptr->player_id(), player);
		if (!loaded) continue;
		
		Asset::User user;
			
		loaded = RedisInstance.GetUser(player.account(), user);
		if (!loaded) continue;

		member_ptr->set_headimgurl(user.wechat().headimgurl()); //头像信息

		if (player.login_time()) //在线
		{
			if (player.room_id()) member_ptr->set_status(Asset::CLAN_MEM_STATUS_TYPE_GAMING); //游戏中
		}
		else if (player.logout_time())
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
	std::lock_guard<std::mutex> lock(_mutex);

	if (!player || !message) return;

	size_t room_query_start_index = message->query_start_index() - 1;
	if (room_query_start_index < 0 || room_query_start_index >= _gaming_room_list.size()) return;

	size_t room_query_end_index = message->query_end_index() - 1;
	if (room_query_end_index < 0 || room_query_end_index >= _gaming_room_list.size()) return;

	for (size_t i = room_query_start_index; i <= room_query_end_index; ++i)
	{
		auto it = _rooms.find(_gaming_room_list[i]);
		if (it == _rooms.end()) continue;
	
		auto room_battle = message->mutable_room_list()->Add();
		room_battle->CopyFrom(it->second);
	}
}

void Clan::OnQueryGamingList(Asset::ClanOperation* message)
{
	std::lock_guard<std::mutex> lock(_mutex);

	if (!message) return;

	for (auto room_id : _gaming_room_list) message->add_room_gaming_list(room_id);
}

void Clan::Save(bool force)
{
	if (!force && !_dirty) return;

	if (!ClanInstance.IsLocal(_clan_id)) return; //不是本服俱乐部，不进行存储

	DEBUG("存储茶馆:{} 数据:{} 成功", _clan_id, _stuff.ShortDebugString());
		
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
			OnRoomOver(message->room().room_id());
		}
		break;
	}

	_dirty = true;
}
	
void Clan::OnRoomOver(int64_t room_id)
{
	std::lock_guard<std::mutex> lock(_mutex);

	_rooms.erase(room_id);

	auto it = std::find(_gaming_room_list.begin(), _gaming_room_list.end(), room_id);
	if (it != _gaming_room_list.end()) _gaming_room_list.erase(it);
}

void Clan::OnRoomSync(const Asset::RoomQueryResult& room_query)
{
	std::lock_guard<std::mutex> lock(_mutex);

	auto room_id = room_query.room_id();
	_rooms[room_id] = room_query;

	auto it = std::find(_gaming_room_list.begin(), _gaming_room_list.end(), room_id);
	if (it == _gaming_room_list.end()) _gaming_room_list.push_back(room_id);

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
		auto success = RedisInstance.Get(value, clan);
		if (!success) continue;

		auto clan_ptr = std::make_shared<Clan>(clan);
		if (!clan_ptr) return;

		Emplace(clan.clan_id(), clan_ptr);
	}
		
	DEBUG("加载茶馆数据成功，加载成功数量:{}", _clans.size());

	_loaded = true;
}

void ClanManager::Remove(int64_t clan_id)
{
	WARN("删除茶馆:{}", clan_id);

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

void ClanManager::OnOperate(std::shared_ptr<Player> player, Asset::ClanOperation* message)
{
	if (!message || !player) return;
	
	if (message->server_id() == g_server_id) //理论上[server_id]是默认值，不会赋值
	{
		WARN("服务器:{} 收到茶馆操作数据:{}, g_server_id, message->ShortDebugString()"); //收到本服发送的协议数据，不再处理
		return;
	}
	
	defer {
		if (message->oper_type() == Asset::CLAN_OPER_TYPE_CREATE || message->oper_type() == Asset::CLAN_OPER_TYPE_CLAN_LIST_QUERY) return; //不进行协议转发

		player->SendProtocol(message); //返回结果
	};
			
	std::shared_ptr<Clan> clan = nullptr;
	
	if (message->oper_type() != Asset::CLAN_OPER_TYPE_CREATE && message->oper_type() != Asset::CLAN_OPER_TYPE_CLAN_LIST_QUERY) 
	{
		clan = ClanInstance.Get(message->clan_id());

		if (!clan) //创建茶馆//列表查询无需检查
		{
			message->set_oper_result(Asset::ERROR_CLAN_NOT_FOUND); //没找到茶馆
			return;
		}
	}

	message->set_server_id(g_server_id);

	switch (message->oper_type())
	{
		case Asset::CLAN_OPER_TYPE_CREATE: //创建
		{
			player->SendProtocol2GameServer(message); //到逻辑服务器进行检查
		}
		break;
	
		case Asset::CLAN_OPER_TYPE_JOIN: //申请加入
		{
			auto result = clan->OnApply(player->GetID(), player->GetName(), message); 
			message->set_oper_result(result); 

			if (result == 0) //申请成功
			{
				if (!IsLocal(message->clan_id())) //本服玩家申请加入另一个服的茶馆
				{
					player->SendProtocol2GameServer(message); //发给另一个中心服处理
					return; //不是本服
				}

				auto hoster_id = clan->GetHoster();
				auto hoster_ptr = PlayerInstance.Get(hoster_id);
				if (!hoster_ptr) return;

				message->mutable_clan()->CopyFrom(clan->Get());
				hoster_ptr->SendProtocol(message); //通知茶馆老板
			}
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
				return;
			}

			Remove(message->clan_id());
		}
		break;
		
		case Asset::CLAN_OPER_TYPE_MEMEBER_AGEE: //同意加入
		{
			if (clan->GetHoster() != player->GetID())
			{
				message->set_oper_result(Asset::ERROR_CLAN_NO_PERMISSION);
				return;
			}
			
			auto result = clan->OnAgree(message); //列表更新
			message->set_oper_result(result); 

			if (result != 0) return; //失败
			
			player->SendProtocol2GameServer(message); //通知逻辑服务器加入成功
		
			Asset::Player des_player;
			if (!PlayerInstance.GetCache(message->dest_player_id(), des_player)) return; //没有记录

			if (des_player.login_time() == 0) //离线
			{
				des_player.add_clan_joiners(message->clan_id());

				PlayerInstance.Save(message->dest_player_id(), des_player); //直接存盘
			}
			else //在线 
			{
				auto des_player = PlayerInstance.Get(message->dest_player_id()); //不在当前中心服务器
				if (!des_player) return;

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
				return;
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
				return;
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
			player->SendProtocol2GameServer(message); //到逻辑服务器进行检查
		}

		case Asset::CLAN_OPER_TYPE_MEMEBER_QUERY: //成员状态查询
		{
			clan->OnQueryMemberStatus(message);
			player->SendProtocol2GameServer(message); //到逻辑服务器进行同步当前茶馆
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
		
		case Asset::CLAN_OPER_TYPE_ROOM_GAMING_LIST_QUERY:
		{
			clan->OnQueryGamingList(message);
		}
		break;

		default:
		{
			ERROR("玩家:{} 茶馆操作尚未处理:{}", player->GetID(), message->ShortDebugString());
			return;
		}
		break;
	}
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

	//if (g_server_id == operation.server_id()) return; //本服不再处理

	auto clan_id = operation.clan_id();
	
	switch (operation.oper_type())
	{
		case Asset::CLAN_OPER_TYPE_CREATE: //创建
		{
			auto clan_ptr = std::make_shared<Clan>(operation.clan());
			clan_ptr->Save(true); //存盘

			OnCreated(clan_id, clan_ptr); //创建成功
		}
		break;
		
		case Asset::CLAN_OPER_TYPE_JOIN: //申请加入
		{
			auto clan = Get(clan_id);
			if (!clan) return;
		
			Asset::Player stuff;

			auto loaded = PlayerInstance.GetCache(message.player_id(), stuff);
			if (!loaded) return;

			auto oper = message.operation();
			auto result = clan->OnApply(stuff.common_prop().player_id(), stuff.common_prop().name(), &oper); 
			
			if (result == 0) //申请成功
			{
				auto hoster_id = clan->GetHoster();
				auto hoster_ptr = PlayerInstance.Get(hoster_id);
				if (!hoster_ptr) return;

				oper.mutable_clan()->CopyFrom(clan->Get());
				hoster_ptr->SendProtocol(oper); //通知茶馆老板
			}
		}
		break;
		
		case Asset::CLAN_OPER_TYPE_RECHARGE: //充值
		{
			auto clan_ptr = Get(clan_id);
			if (!clan_ptr) return;
		
			clan_ptr->OnRecharge(message.operation().recharge_count());
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
	
bool ClanManager::IsLocal(int64_t clan_id)
{
	int64_t server_id = clan_id >> 20;
	return server_id == g_server_id;
}

bool ClanManager::GetClan(int64_t clan_id, Asset::Clan& clan)
{
	return RedisInstance.Get("clan:" + std::to_string(clan_id), clan);
}

}

