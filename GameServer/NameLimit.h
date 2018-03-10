#pragma once

#include <memory>
#include <functional>
#include <fstream>

#include <pbjson.hpp>

#include "P_Header.h"
#include "CommonUtil.h"
#include "MXLog.h"

namespace Adoter
{
namespace pb = google::protobuf;

class NameLimit : public std::enable_shared_from_this<NameLimit>   
{
	Asset::PingBi _pingbi;
public:
	static NameLimit& Instance()
	{
		static NameLimit _instance;
		return _instance;
	}

	bool Load()
	{
		std::ifstream fi("PingBi.txt", std::ifstream::in);
		pb::io::IstreamInputStream iis(&fi);

		auto result = pb::TextFormat::Parse(&iis, &_family_name);
		fi.close();

		if (!result) return false;
		
		return true;
	}

	std::string Get()
	{
		boost::trim(family_name); //姓

		return full_name; 
	}
};

#define NameInstance NameLimit::Instance()

}
