#include <spdlog/spdlog.h>

#include "CenterSession.h"
#include "MXLog.h"
#include "Player.h"
#include "GmtSession.h"
#include "Protocol.h"

namespace Adoter
{

namespace spd = spdlog;

bool CenterSession::OnInnerProcess(const Asset::Meta& meta)
{
	pb::Message* msg = ProtocolInstance.GetMessage(meta.type_t());	
	if (!msg) return false;

	auto message = msg->New();

	defer {
		delete message;
		message = nullptr;
	};

	auto result = message->ParseFromArray(meta.stuff().c_str(), meta.stuff().size());
	if (!result) return false; 

	switch (meta.type_t())
	{
		case Asset::META_TYPE_S2S_REGISTER: //注册服务器成功
		{
			DEBUG("游戏逻辑服务器注册到中心服成功.");
		}
		break;

		case Asset::META_TYPE_S2S_GMT_INNER_META: //GMT命令
		{
			const auto gmt_inner_meta = dynamic_cast<const Asset::GmtInnerMeta*>(message);
			if (!gmt_inner_meta) return false;

			GmtInstance.SetSession(std::dynamic_pointer_cast<CenterSession>(shared_from_this()));

			Asset::InnerMeta inner_meta;
			inner_meta.ParseFromString(gmt_inner_meta->inner_meta());
			GmtInstance.OnInnerProcess(inner_meta);
		}
		break;
		
		case Asset::META_TYPE_S2S_KICKOUT_PLAYER: //防止玩家退出后收到踢出继续初始化
		{
			auto player = GetPlayer(meta.player_id());
			if (!player) return false;

			player->Logout(message);
		}
		break;

		default:
		{
			ERROR("尚未存在协议处理回调:{} 协议:{}", meta.type_t(), message->ShortDebugString());
		}
		break;
	}

	return true;
}

}
