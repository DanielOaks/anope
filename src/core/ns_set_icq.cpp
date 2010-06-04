/* NickServ core functions
 *
 * (C) 2003-2010 Anope Team
 * Contact us at team@anope.org
 *
 * Please read COPYING and README for further details.
 *
 * Based on the original code of Epona by Lara.
 * Based on the original code of Services by Andy Church.
 *
 * $Id$
 *
 */
/*************************************************************************/

#include "module.h"

class CommandNSSetICQ : public Command
{
 public:
	CommandNSSetICQ(const ci::string &cname) : Command(cname, 0)
	{
	}

	CommandReturn Execute(User *u, const std::vector<ci::string> &params)
	{
		if (!params.empty())
		{
			int32 tmp = atol(params[0].c_str());
			if (!tmp)
				notice_lang(Config.s_NickServ, u, NICK_SET_ICQ_INVALID, params[0].c_str());
			else
			{
				u->Account()->icq = tmp;
				notice_lang(Config.s_NickServ, u, NICK_SET_ICQ_CHANGED, params[0].c_str());
			}
		}
		else
		{
			u->Account()->icq = 0;
			notice_lang(Config.s_NickServ, u, NICK_SET_ICQ_UNSET);
		}

		return MOD_CONT;
	}

	bool OnHelp(User *u, const ci::string &)
	{
		notice_help(Config.s_NickServ, u, NICK_HELP_SET_ICQ);
		return true;
	}

	void OnServHelp(User *u)
	{
		notice_lang(Config.s_NickServ, u, NICK_HELP_CMD_SET_ICQ);
	}
};

class CommandNSSASetICQ : public Command
{
 public:
	CommandNSSASetICQ(const ci::string &cname) : Command(cname, 1, 2, "nickserv/saset/icq")
	{
	}

	CommandReturn Execute(User *u, const std::vector<ci::string> &params)
	{
		NickCore *nc = findcore(params[0]);
		assert(nc);

		const char *param = params.size() > 1 ? params[1].c_str() : NULL;

		if (param)
		{
			int32 tmp = atol(param);

			if (tmp)
				notice_lang(Config.s_NickServ, u, NICK_SASET_ICQ_INVALID, param);
			else
			{
				nc->icq = tmp;
				notice_lang(Config.s_NickServ, u, NICK_SASET_ICQ_CHANGED, nc->display, param);
			}
		}
		else
		{
			nc->icq = 0;
			notice_lang(Config.s_NickServ, u, NICK_SASET_ICQ_UNSET, nc->display);
		}

		return MOD_CONT;
	}

	bool OnHelp(User *u, const ci::string &)
	{
		notice_help(Config.s_NickServ, u, NICK_HELP_SASET_ICQ);
		return true;
	}

	void OnServHelp(User *u)
	{
		notice_lang(Config.s_NickServ, u, NICK_HELP_CMD_SASET_ICQ);
	}
};

class NSSetICQ : public Module
{
 public:
	NSSetICQ(const std::string &modname, const std::string &creator) : Module(modname, creator)
	{
		this->SetAuthor("Anope");
		this->SetVersion("$Id$");
		this->SetType(CORE);

		Command *c = FindCommand(NickServ, "SET");
		if (c)
			c->AddSubcommand(new CommandNSSetICQ("ICQ"));

		c = FindCommand(NickServ, "SASET");
		if (c)
			c->AddSubcommand(new CommandNSSASetICQ("ICQ"));
	}

	~NSSetICQ()
	{
		Command *c = FindCommand(NickServ, "SET");
		if (c)
			c->DelSubcommand("ICQ");

		c = FindCommand(NickServ, "SASET");
		if (c)
			c->DelSubcommand("ICQ");
	}
};

MODULE_INIT(NSSetICQ)
