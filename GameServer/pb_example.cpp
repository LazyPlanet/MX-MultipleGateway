// pb_example.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "test.pb.h"
#include "hiredis.h"

#include <iostream>  
#include <fstream> 

int main()
{
	// ����User����
	cn::vicky::model::seri::User u;
	u.set_id(1);
	u.set_username("Jack");
	u.set_password("123456");
	u.set_email("289997171@qq.com");

	// ����User�е�һ����ɫ
	cn::vicky::model::seri::Person* _person1 = u.add_person();
	_person1->set_id(1);
	_person1->set_name("P1");

	// ������ɫ�е�һ���绰����:1
	cn::vicky::model::seri::PhoneNumber* _phone1 = _person1->add_phone();
	_phone1->set_number("+8613618074943");
	_phone1->set_type(cn::vicky::model::seri::MOBILE);

	// ������ɫ�е�һ���绰����:2
	cn::vicky::model::seri::PhoneNumber* _phone2 = _person1->add_phone();
	_phone2->set_number("02882334717");
	_phone2->set_type(cn::vicky::model::seri::WORK);


	// ����User�е�һ����ɫ
	cn::vicky::model::seri::Person* _person2 = u.add_person();
	_person2->set_id(2);
	_person2->set_name("P2");

	// ������ɫ�е�һ���绰����:1
	cn::vicky::model::seri::PhoneNumber* _phone3 = _person2->add_phone();
	_phone3->set_number("+8613996398667");
	_phone3->set_type(cn::vicky::model::seri::MOBILE);

	// ������ɫ�е�һ���绰����:2
	cn::vicky::model::seri::PhoneNumber* _phone4 = _person2->add_phone();
	_phone4->set_number("02882334717");
	_phone4->set_type(cn::vicky::model::seri::WORK);


	// �������Զ����Ʊ���
	const int byteSize = u.ByteSize();
	std::cout << "byteSize = " << byteSize << std::endl;
	char buf[byteSize];
	bzero(buf, byteSize);
	u.SerializeToArray(buf, byteSize);


	// ����redis����
	redisContext *c;
	redisReply *reply;

	struct timeval timeout = { 1, 500000 }; // 1.5 seconds
	c = redisConnectWithTimeout((char*) "127.0.0.1", 3307, timeout);
	if (c->err) {
		printf("Connection error: %s\n", c->errstr);
		exit(1);
	}

	//    ��һ��ִ��:������д��redis���ݿ�
	//    reply = (redisReply*) redisCommand(c, "SET %b %b", u.username().c_str(), (int) u.username().length(), buf, byteSize); // �ص�!!!
	//    printf("SET (binary API): %s\n", reply->str);
	//    freeReplyObject(reply);

	//    �ڶ���ִ��:��redis���ݿ��ȡ��������
	reply = (redisReply*)redisCommand(c, "Get Jack");
	std::cout << "reply->len = " << reply->len << "\nreply->str : \n" << reply->str << std::endl; // �����ӡ����

	std::cout << "---------------------------" << std::endl;

	cn::vicky::model::seri::User u2;
	u2.ParseFromArray(reply->str, reply->len);

	std::cout << u2.id() << std::endl;
	std::cout << u2.username() << std::endl;
	std::cout << u2.password() << std::endl;
	std::cout << u2.email() << std::endl;

	std::cout << "---------------------------" << std::endl;
	for (int i = 0; i < u2.person_size(); i++) {
		cn::vicky::model::seri::Person* p = u2.mutable_person(i);
		std::cout << p->id() << std::endl;
		std::cout << p->name() << std::endl;
		for (int j = 0; j < p->phone_size(); j++) {
			cn::vicky::model::seri::PhoneNumber* phone = p->mutable_phone(j);
			std::cout << phone->number() << std::endl;
		}
		std::cout << "---------------------------" << std::endl;
	}
    return 0;
}

