/* Inspircd 2.0 functions
 *
 * (C) 2003-2010 Anope Team
 * Contact us at team@anope.org
 *
 * Please read COPYING and README for further details.
 *
 * Based on the original code of Epona by Lara.
 * Based on the original code of Services by Andy Church.
 *
 *
 */

/*************************************************************************/

#include "services.h"
#include "modules.h"
#include "hashcomp.h"

#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#ifdef _WIN32
#include <winsock.h>
int inet_aton(const char *name, struct in_addr *addr)
{
	uint32 a = inet_addr(name);
	addr->s_addr = a;
	return a != (uint32) - 1;
}
#endif

IRCDVar myIrcd[] = {
	{"InspIRCd 2.0",			/* ircd name */
	 "+I",					  /* Modes used by pseudoclients */
	 5,						 /* Chan Max Symbols	 */
	 "+ao",					 /* Channel Umode used by Botserv bots */
	 1,						 /* SVSNICK */
	 1,						 /* Vhost  */
	 0,						 /* Supports SNlines	 */
	 1,						 /* Supports SQlines	 */
	 1,						 /* Supports SZlines	 */
	 4,						 /* Number of server args */
	 0,						 /* Join 2 Set		   */
	 0,						 /* Join 2 Message	   */
	 1,						 /* TS Topic Forward	 */
	 0,						 /* TS Topci Backward	*/
	 0,						 /* Chan SQlines		 */
	 0,						 /* Quit on Kill		 */
	 0,						 /* SVSMODE unban		*/
	 1,						 /* Reverse			  */
	 1,						 /* vidents			  */
	 1,						 /* svshold			  */
	 0,						 /* time stamp on mode   */
	 0,						 /* NICKIP			   */
	 0,						 /* O:LINE			   */
	 1,						 /* UMODE			   */
	 1,						 /* VHOST ON NICK		*/
	 0,						 /* Change RealName	  */
	 1,						 /* No Knock requires +i */
	 0,						 /* We support inspircd TOKENS */
	 0,						 /* TIME STAMPS are BASE64 */
	 0,						 /* Can remove User Channel Modes with SVSMODE */
	 0,						 /* Sglines are not enforced until user reconnects */
	 1,						 /* ts6 */
	 0,						 /* p10 */
	 1,						 /* CIDR channelbans */
	 "$",					   /* TLD Prefix for Global */
	 20,					/* Max number of modes we can send per line */
	 }
	,
	{NULL}
};

static int has_servicesmod = 0;
static int has_globopsmod = 0;
static int has_svsholdmod = 0;
static int has_chghostmod = 0;
static int has_chgidentmod = 0;
static int has_messagefloodmod = 0;
static int has_banexceptionmod = 0;
static int has_inviteexceptionmod = 0;
static int has_hidechansmod = 0;

/* Previously introduced user during burst */
static User *prev_u_intro = NULL;

/* CHGHOST */
void inspircd_cmd_chghost(const char *nick, const char *vhost)
{
	if (has_chghostmod != 1)
	{
		ircdproto->SendGlobops(OperServ, "CHGHOST not loaded!");
		return;
	}

	BotInfo *bi = OperServ;
	send_cmd(bi->uid, "CHGHOST %s %s", nick, vhost);
}

int anope_event_idle(const char *source, int ac, const char **av)
{
	BotInfo *bi = findbot(av[0]);
	if (!bi)
		return MOD_CONT;

	send_cmd(bi->uid, "IDLE %s %ld %ld", source, static_cast<long>(start_time), static_cast<long>(time(NULL) - bi->lastmsg));
	return MOD_CONT;
}

static char currentpass[1024];

/* PASS */
void inspircd_cmd_pass(const char *pass)
{
	strlcpy(currentpass, pass, sizeof(currentpass));
}


class InspIRCdProto : public IRCDProto
{
	void SendAkillDel(XLine *x)
	{
		BotInfo *bi = OperServ;
		send_cmd(bi->uid, "GLINE %s", x->Mask.c_str());
	}

	void SendTopic(BotInfo *whosets, Channel *c, const char *whosetit, const char *topic)
	{
		send_cmd(whosets->uid, "FTOPIC %s %lu %s :%s", c->name.c_str(), static_cast<unsigned long>(c->topic_time), whosetit, topic);
	}

	void SendVhostDel(User *u)
	{
		if (u->HasMode(UMODE_CLOAK))
			inspircd_cmd_chghost(u->nick.c_str(), u->chost.c_str());
		else
			inspircd_cmd_chghost(u->nick.c_str(), u->host);

		if (has_chgidentmod && u->GetIdent() != u->GetVIdent())
		{
			inspircd_cmd_chgident(u->nick.c_str(), u->GetIdent().c_str());
		}
	}

	void SendAkill(XLine *x)
	{
		// Calculate the time left before this would expire, capping it at 2 days
		time_t timeleft = x->Expires - time(NULL);
		if (timeleft > 172800 || !x->Expires)
			timeleft = 172800;
		BotInfo *bi = OperServ;
		send_cmd(bi->uid, "ADDLINE G %s@%s %s %ld %ld :%s", x->GetUser().c_str(), x->GetHost().c_str(), x->By.c_str(), static_cast<long>(time(NULL)), static_cast<long>(timeleft), x->Reason.c_str());
	}

	void SendSVSKillInternal(BotInfo *source, User *user, const char *buf)
	{
		send_cmd(source ? source->uid : TS6SID, "KILL %s :%s", user->GetUID().c_str(), buf);
	}

	void SendSVSMode(User *u, int ac, const char **av)
	{
		this->SendModeInternal(NULL, u, merge_args(ac, av));
	}

	void SendNumericInternal(const char *source, int numeric, const char *dest, const char *buf)
	{
		send_cmd(TS6SID, "PUSH %s ::%s %03d %s %s", dest, source, numeric, dest, buf);
	}

	void SendGuestNick(const char *nick, const char *user, const char *host, const char *real, const char *modes)
	{
		send_cmd(TS6SID, "UID %ld %s %s %s %s +%s 0.0.0.0 :%s", static_cast<long>(time(NULL)), nick, host, host, user, modes, real);
	}

	void SendModeInternal(BotInfo *source, Channel *dest, const char *buf)
	{
		send_cmd(source ? source->uid : TS6SID, "FMODE %s %u %s", dest->name.c_str(), static_cast<unsigned>(dest->creation_time), buf);
	}

	void SendModeInternal(BotInfo *bi, User *u, const char *buf)
	{
		if (!buf) return;
		send_cmd(bi ? bi->uid : TS6SID, "MODE %s %s", u->GetUID().c_str(), buf);
	}

	void SendClientIntroduction(const std::string &nick, const std::string &user, const std::string &host, const std::string &real, const char *modes, const std::string &uid)
	{
		send_cmd(TS6SID, "UID %s %ld %s %s %s %s 0.0.0.0 %ld %s :%s", uid.c_str(), static_cast<long>(time(NULL)), nick.c_str(), host.c_str(), host.c_str(), user.c_str(), static_cast<long>(time(NULL)), modes, real.c_str());
	}

	void SendKickInternal(BotInfo *source, Channel *chan, User *user, const char *buf)
	{
		if (buf)
			send_cmd(source->uid, "KICK %s %s :%s", chan->name.c_str(), user->GetUID().c_str(), buf);
		else
			send_cmd(source->uid, "KICK %s %s :%s", chan->name.c_str(), user->GetUID().c_str(), user->nick.c_str());
	}

	void SendNoticeChanopsInternal(BotInfo *source, Channel *dest, const char *buf)
	{
		send_cmd(TS6SID, "NOTICE @%s :%s", dest->name.c_str(), buf);
	}

	/* SERVER services-dev.chatspike.net password 0 :Description here */
	void SendServer(Server *server)
	{
		send_cmd(NULL, "SERVER %s %s %d %s :%s", server->GetName().c_str(), currentpass, server->GetHops(), server->GetSID().c_str(), server->GetDescription().c_str());
	}

	/* JOIN */
	void SendJoin(BotInfo *user, const char *channel, time_t chantime)
	{
		send_cmd(TS6SID, "FJOIN %s %ld + :,%s", channel, static_cast<long>(chantime), user->uid.c_str());
	}

	/* UNSQLINE */
	void SendSQLineDel(XLine *x)
	{
		send_cmd(TS6SID, "DELLINE Q %s", x->Mask.c_str());
	}

	/* SQLINE */
	void SendSQLine(XLine *x)
	{
		send_cmd(TS6SID, "ADDLINE Q %s %s %ld 0 :%s", x->Mask.c_str(), Config.s_OperServ, static_cast<long>(time(NULL)), x->Reason.c_str());
	}

	/* SQUIT */
	void SendSquit(const char *servname, const char *message)
	{
		send_cmd(TS6SID, "SQUIT %s :%s", servname, message);
	}

	/* Functions that use serval cmd functions */

	void SendVhost(User *u, const std::string &vIdent, const std::string &vhost)
	{
		if (!vIdent.empty())
			inspircd_cmd_chgident(u->nick.c_str(), vIdent.c_str());
		if (!vhost.empty())
			inspircd_cmd_chghost(u->nick.c_str(), vhost.c_str());
	}

	void SendConnect()
	{
		Me = new Server(NULL, Config.ServerName, 0, Config.ServerDesc, TS6SID);
		inspircd_cmd_pass(uplink_server->password);
		SendServer(Me);
		send_cmd(TS6SID, "BURST");
		send_cmd(TS6SID, "VERSION :Anope-%s %s :%s - (%s) -- %s", version_number, Config.ServerName, ircd->name, Config.EncModuleList.begin()->c_str(), version_build);
	}

	/* CHGIDENT */
	void inspircd_cmd_chgident(const char *nick, const char *vIdent)
	{
		if (has_chgidentmod == 0)
		{
			ircdproto->SendGlobops(OperServ, "CHGIDENT not loaded!");
		}
		else
		{
			BotInfo *bi = OperServ;
			send_cmd(bi->uid, "CHGIDENT %s %s", nick, vIdent);
		}
	}

	/* SVSHOLD - set */
	void SendSVSHold(const char *nick)
	{
		BotInfo *bi = OperServ;
		send_cmd(bi->uid, "SVSHOLD %s %u :%s", nick, static_cast<unsigned>(Config.NSReleaseTimeout), "Being held for registered user");
	}

	/* SVSHOLD - release */
	void SendSVSHoldDel(const char *nick)
	{
		BotInfo *bi = OperServ;
		send_cmd(bi->uid, "SVSHOLD %s", nick);
	}

	/* UNSZLINE */
	void SendSZLineDel(XLine *x)
	{
		send_cmd(TS6SID, "DELLINE Z %s", x->Mask.c_str());
	}

	/* SZLINE */
	void SendSZLine(XLine *x)
	{
		send_cmd(TS6SID, "ADDLINE Z %s %s %ld 0 :%s", x->Mask.c_str(), x->By.c_str(), static_cast<long>(time(NULL)), x->Reason.c_str());
	}

	/* SVSMODE -r */
	void SendUnregisteredNick(User *u)
	{
		u->RemoveMode(NickServ, UMODE_REGISTERED);
	}

	void SendSVSJoin(const char *source, const char *nick, const char *chan, const char *param)
	{
		User *u = finduser(nick);
		BotInfo *bi = findbot(source);
		send_cmd(bi->uid, "SVSJOIN %s %s", u->GetUID().c_str(), chan);
	}

	void SendSVSPart(const char *source, const char *nick, const char *chan)
	{
		User *u = finduser(nick);
		BotInfo *bi = findbot(source);
		send_cmd(bi->uid, "SVSPART %s %s", u->GetUID().c_str(), chan);
	}

	void SendSWhois(const char *source, const char *who, const char *mask)
	{
		User *u = finduser(who);

		send_cmd(TS6SID, "METADATA %s swhois :%s", u->GetUID().c_str(), mask);
	}

	void SendEOB()
	{
		send_cmd(TS6SID, "ENDBURST");
	}

	void SendGlobopsInternal(BotInfo *source, const char *buf)
	{
		if (has_globopsmod)
			send_cmd(source ? source->uid : TS6SID, "SNONOTICE g :%s", buf);
		else
			send_cmd(source ? source->uid : TS6SID, "SNONOTICE A :%s", buf);
	}

	void SendAccountLogin(User *u, NickCore *account)
	{
		send_cmd(TS6SID, "METADATA %s accountname :%s", u->GetUID().c_str(), account->display);
	}

	void SendAccountLogout(User *u, NickCore *account)
	{
		send_cmd(TS6SID, "METADATA %s accountname :", u->GetUID().c_str());
	}

	int IsNickValid(const char *nick)
	{
		/* InspIRCd, like TS6, uses UIDs on collision, so... */
		if (isdigit(*nick))
			return 0;
		return 1;
	}

	void SetAutoIdentificationToken(User *u)
	{
		if (!u->Account())
			return;

		u->SetMode(NickServ, UMODE_REGISTERED);
	}

} ircd_proto;






int anope_event_ftopic(const char *source, int ac, const char **av)
{
	/* :source FTOPIC channel ts setby :topic */
	const char *temp;
	if (ac < 4)
		return MOD_CONT;
	temp = av[1];			   /* temp now holds ts */
	av[1] = av[2];			  /* av[1] now holds set by */
	av[2] = temp;			   /* av[2] now holds ts */
	do_topic(source, ac, av);
	return MOD_CONT;
}

int anope_event_mode(const char *source, int ac, const char **av)
{
	if (*av[0] == '#' || *av[0] == '&')
	{
		do_cmode(source, ac, av);
	}
	else
	{
		/* InspIRCd lets opers change another
		   users modes, we have to kludge this
		   as it slightly breaks RFC1459
		 */
		User *u = finduser(source);
		User *u2 = finduser(av[0]);

		// This can happen with server-origin modes.
		if (u == NULL)
			u = u2;

		// if it's still null, drop it like fire.
		// most likely situation was that server introduced a nick which we subsequently akilled
		if (u == NULL)
			return MOD_CONT;

		av[0] = u2->nick.c_str();
		do_umode(u->nick.c_str(), ac, av);
	}
	return MOD_CONT;
}

int anope_event_opertype(const char *source, int ac, const char **av)
{
	/* opertype is equivalent to mode +o because servers
	   dont do this directly */
	User *u;
	u = finduser(source);
	if (u && !is_oper(u)) {
		const char *newav[2];
		newav[0] = source;
		newav[1] = "+o";
		return anope_event_mode(source, 2, newav);
	} else
		return MOD_CONT;
}

int anope_event_fmode(const char *source, int ac, const char **av)
{
	const char *newav[25];
	int n, o;
	Channel *c;

	/* :source FMODE #test 12345678 +nto foo */
	if (ac < 3)
		return MOD_CONT;

	/* Checking the TS for validity to avoid desyncs */
	if ((c = findchan(av[0]))) {
		if (c->creation_time > strtol(av[1], NULL, 10)) {
			/* Our TS is bigger, we should lower it */
			c->creation_time = strtol(av[1], NULL, 10);
		} else if (c->creation_time < strtol(av[1], NULL, 10)) {
			/* The TS we got is bigger, we should ignore this message. */
			return MOD_CONT;
		}
	} else {
		/* Got FMODE for a non-existing channel */
		return MOD_CONT;
	}

	/* TS's are equal now, so we can proceed with parsing */
	n = o = 0;
	while (n < ac) {
		if (n != 1) {
			newav[o] = av[n];
			o++;
			Alog(LOG_DEBUG) << "Param: " << newav[o - 1];
		}
		n++;
	}

	return anope_event_mode(source, ac - 1, newav);
}

/*
 * [Nov 03 22:31:57.695076 2009] debug: Received: :964 FJOIN #test 1223763723 +BPSnt :,964AAAAAB ,964AAAAAC ,966AAAAAA
 *
 * 0: name
 * 1: channel ts (when it was created, see protocol docs for more info)
 * 2: channel modes + params (NOTE: this may definitely be more than one param!)
 * last: users
 */
int anope_event_fjoin(const char *source, int ac, const char **av)
{
	Channel *c = findchan(av[0]);
	time_t ts = atol(av[1]);
	bool was_created = false;
	bool keep_their_modes = true;

	if (!c)
	{
		c = new Channel(av[0], ts);
		was_created = true;
	}
	/* Our creation time is newer than what the server gave us */
	else if (c->creation_time > ts)
	{
		c->creation_time = ts;

		/* Remove status from all of our users */
		for (CUserList::iterator it = c->users.begin(); it != c->users.end(); ++it)
		{
			UserContainer *uc = *it;

			c->RemoveMode(NULL, CMODE_OWNER, uc->user->nick);
			c->RemoveMode(NULL, CMODE_PROTECT, uc->user->nick);
			c->RemoveMode(NULL, CMODE_OP, uc->user->nick);
			c->RemoveMode(NULL, CMODE_HALFOP, uc->user->nick);
			c->RemoveMode(NULL, CMODE_VOICE, uc->user->nick);
		}
		if (c->ci)
		{
			/* Rejoin the bot to fix the TS */
			if (c->ci->bi)
			{
				ircdproto->SendPart(c->ci->bi, c, "TS reop");
				bot_join(c->ci);
			}
			/* Reset mlock */
			check_modes(c);
		}
	}
	/* Their TS is newer than ours, our modes > theirs, unset their modes if need be */
	else
		keep_their_modes = false;
	
	/* Mark the channel as syncing */
	if (was_created)
		c->SetFlag(CH_SYNCING);
	
	/* If we need to keep their modes, and this FJOIN string contains modes */
	if (keep_their_modes && ac >= 4)
	{
		/* Set the modes internally */
		ChanSetInternalModes(c, ac - 3, av + 2);
	}

	spacesepstream sep(av[ac - 1]);
	std::string buf;
	while (sep.GetToken(buf))
	{
		std::list<ChannelMode *> Status;
		Status.clear();

		/* Loop through prefixes and find modes for them */
		while (buf[0] != ',')
		{
			ChannelMode *cm = ModeManager::FindChannelModeByChar(buf[0]);
			if (!cm)
			{
				Alog() << "Recieved unknown mode prefix " << buf[0] << " in FJOIN string";
				buf.erase(buf.begin());
				continue;
			}

			buf.erase(buf.begin());
			Status.push_back(cm);
		}
		buf.erase(buf.begin());

		User *u = finduser(buf);
		if (!u)
		{
			Alog(LOG_DEBUG) << "FJOIN for nonexistant user " << buf << " on " << c->name;
			continue;
		}

		EventReturn MOD_RESULT;
		FOREACH_RESULT(I_OnPreJoinChannel, OnPreJoinChannel(u, c));

		/* Add the user to the channel */
		c->JoinUser(u);

		/* Update their status internally on the channel
		 * This will enforce secureops etc on the user
		 */
		for (std::list<ChannelMode *>::iterator it = Status.begin(); it != Status.end(); ++it)
		{
			c->SetModeInternal(*it, buf);
		}

		/* Now set whatever modes this user is allowed to have on the channel */
		chan_set_correct_modes(u, c, 1);

		/* Check to see if modules want the user to join, if they do
		 * check to see if they are allowed to join (CheckKick will kick/ban them)
		 * Don't trigger OnJoinChannel event then as the user will be destroyed
		 */
		if (MOD_RESULT != EVENT_STOP && c->ci && c->ci->CheckKick(u))
			continue;

		FOREACH_MOD(I_OnJoinChannel, OnJoinChannel(u, c));
	}

	/* Channel is done syncing */
	if (was_created)
	{
		/* Unset the syncing flag */
		c->UnsetFlag(CH_SYNCING);

		/* If there are users in the channel they are allowed to be, set topic mlock etc */
		if (!c->users.empty())
			c->Sync();
		/* If there are no users in the channel, there is a ChanServ timer set to part the service bot
		 * and destroy the channel soon
		 */
	}
	
	return MOD_CONT;
}

/* Events */
int anope_event_ping(const char *source, int ac, const char **av)
{
	if (ac == 1)
		ircdproto->SendPong(NULL, av[0]);

	if (ac == 2)
		ircdproto->SendPong(av[1], av[0]);

	return MOD_CONT;
}

int anope_event_time(const char *source, int ac, const char **av)
{
	if (ac !=2)
		return MOD_CONT;

	send_cmd(TS6SID, "TIME %s %s %ld", source, av[1], static_cast<long>(time(NULL)));

	/* We handled it, don't pass it on to the core..
	 * The core doesn't understand our syntax anyways.. ~ Viper */
	return MOD_STOP;
}

int anope_event_436(const char *source, int ac, const char **av)
{
	m_nickcoll(av[0]);
	return MOD_CONT;
}

int anope_event_away(const char *source, int ac, const char **av)
{
	m_away(source, (ac ? av[0] : NULL));
	return MOD_CONT;
}

/* Taken from hybrid.c, topic syntax is identical */
int anope_event_topic(const char *source, int ac, const char **av)
{
	Channel *c = findchan(av[0]);
	time_t topic_time = time(NULL);
	User *u = finduser(source);

	if (!c)
	{
		Alog(LOG_DEBUG) << "debug: TOPIC " << merge_args(ac - 1, av + 1) << " for nonexistent channel " << av[0];
		return MOD_CONT;
	}

	if (check_topiclock(c, topic_time))
		return MOD_CONT;

	if (c->topic) {
		delete [] c->topic;
		c->topic = NULL;
	}
	if (ac > 1 && *av[1])
		c->topic = sstrdup(av[1]);

	c->topic_setter = u ? u->nick : source;
	c->topic_time = topic_time;

	record_topic(av[0]);

	if (ac > 1 && *av[1]) {
		FOREACH_MOD(I_OnTopicUpdated, OnTopicUpdated(c, av[0]));
	}
	else {
		FOREACH_MOD(I_OnTopicUpdated, OnTopicUpdated(c, ""));
	}

	return MOD_CONT;
}

int anope_event_squit(const char *source, int ac, const char **av)
{
	do_squit(source, ac, av);
	return MOD_CONT;
}

int anope_event_rsquit(const char *source, int ac, const char **av)
{
	/* On InspIRCd we must send a SQUIT when we recieve RSQUIT for a server we have juped */
	Server *s = Server::Find(av[0]);
	if (s && s->HasFlag(SERVER_JUPED))
	{
		send_cmd(TS6SID, "SQUIT %s :%s", s->GetSID().c_str(), ac > 1 ? av[1] : "");
	}

	do_squit(source, ac, av);

	return MOD_CONT;
}

int anope_event_quit(const char *source, int ac, const char **av)
{
	do_quit(source, ac, av);
	return MOD_CONT;
}


int anope_event_kill(const char *source, int ac, const char **av)
{
	User *u = finduser(av[0]);
	BotInfo *bi = findbot(av[0]);
	m_kill(u ? u->nick.c_str() : (bi ? bi->nick : av[0]), av[1]);
	return MOD_CONT;
}

int anope_event_kick(const char *source, int ac, const char **av)
{
	do_kick(source, ac, av);
	return MOD_CONT;
}


int anope_event_join(const char *source, int ac, const char **av)
{
	do_join(source, ac, av);
	return MOD_CONT;
}

int anope_event_motd(const char *source, int ac, const char **av)
{
	m_motd(source);
	return MOD_CONT;
}

int anope_event_setname(const char *source, int ac, const char **av)
{
	User *u;

	u = finduser(source);
	if (!u)
	{
		Alog(LOG_DEBUG) << "SETNAME for nonexistent user " << source;
		return MOD_CONT;
	}

	u->SetRealname(av[0]);
	return MOD_CONT;
}

int anope_event_chgname(const char *source, int ac, const char **av)
{
	User *u;

	u = finduser(source);
	if (!u)
	{
		Alog(LOG_DEBUG) << "FNAME for nonexistent user " << source;
		return MOD_CONT;
	}

	u->SetRealname(av[0]);
	return MOD_CONT;
}

int anope_event_setident(const char *source, int ac, const char **av)
{
	User *u;

	u = finduser(source);
	if (!u)
	{
		Alog(LOG_DEBUG) << "SETIDENT for nonexistent user " << source;
		return MOD_CONT;
	}

	u->SetIdent(av[0]);
	return MOD_CONT;
}

int anope_event_chgident(const char *source, int ac, const char **av)
{
	User *u;

	u = finduser(av[0]);
	if (!u)
	{
		Alog(LOG_DEBUG) << "CHGIDENT for nonexistent user " << av[0];
		return MOD_CONT;
	}

	u->SetIdent(av[1]);
	return MOD_CONT;
}

int anope_event_sethost(const char *source, int ac, const char **av)
{
	User *u;

	u = finduser(source);
	if (!u)
	{
		Alog(LOG_DEBUG) << "SETHOST for nonexistent user " << source;
		return MOD_CONT;
	}

	u->SetDisplayedHost(av[0]);
	return MOD_CONT;
}


int anope_event_nick(const char *source, int ac, const char **av)
{
	do_nick(source, av[0], NULL, NULL, NULL, NULL, 0, 0, NULL, NULL);
	return MOD_CONT;
}


/*
 * [Nov 03 22:09:58.176252 2009] debug: Received: :964 UID 964AAAAAC 1225746297 w00t2 localhost testnet.user w00t 127.0.0.1 1225746302 +iosw +ACGJKLNOQcdfgjklnoqtx :Robin Burchell <w00t@inspircd.org>
 * 0: uid
 * 1: ts
 * 2: nick
 * 3: host
 * 4: dhost
 * 5: ident
 * 6: ip
 * 7: signon
 * 8+: modes and params -- IMPORTANT, some modes (e.g. +s) may have parameters. So don't assume a fixed position of realname!
 * last: realname
*/

int anope_event_uid(const char *source, int ac, const char **av)
{
	User *user;
	NickAlias *na;
	struct in_addr addy;
	Server *s = Server::Find(source ? source : "");
	uint32 *ad = reinterpret_cast<uint32 *>(&addy);
	int ts = strtoul(av[1], NULL, 10);

	/* Check if the previously introduced user was Id'd for the nickgroup of the nick he s currently using.
	 * If not, validate the user.  ~ Viper*/
	user = prev_u_intro;
	prev_u_intro = NULL;
	if (user) na = findnick(user->nick);
	if (user && !user->server->IsSynced() && (!na || na->nc != user->Account()))
	{
		validate_user(user);
		if (user->HasMode(UMODE_REGISTERED))
			user->RemoveMode(NickServ, UMODE_REGISTERED);
	}
	user = NULL;

	inet_aton(av[6], &addy);
	user = do_nick("", av[2],   /* nick */
			av[5],   /* username */
			av[3],   /* realhost */
			s->GetName().c_str(),  /* server */
			av[ac - 1],   /* realname */
			ts, htonl(*ad), av[4], av[0]);
	if (user)
	{
		UserSetInternalModes(user, 1, &av[8]);
		user->SetCloakedHost(av[4]);
		if (!user->server->IsSynced())
		{
			prev_u_intro = user;
		}
		else
		{
			validate_user(user);
		}
	}

	return MOD_CONT;
}

int anope_event_chghost(const char *source, int ac, const char **av)
{
	User *u;

	u = finduser(source);
	if (!u)
	{
		Alog(LOG_DEBUG) << "FHOST for nonexistent user " << source;
		return MOD_CONT;
	}

	u->SetDisplayedHost(av[0]);
	return MOD_CONT;
}

/*
 * [Nov 04 00:08:46.308435 2009] debug: Received: SERVER irc.inspircd.com pass 0 964 :Testnet Central!
 * 0: name
 * 1: pass
 * 2: hops
 * 3: numeric
 * 4: desc
 */
int anope_event_server(const char *source, int ac, const char **av)
{
	do_server(source, av[0], atoi(av[2]), av[4], av[3]);
	return MOD_CONT;
}


int anope_event_privmsg(const char *source, int ac, const char **av)
{
	if (!finduser(source))
		return MOD_CONT; // likely a message from a server, which can happen.

	m_privmsg(source, av[0], av[1]);
	return MOD_CONT;
}

int anope_event_part(const char *source, int ac, const char **av)
{
	do_part(source, ac, av);
	return MOD_CONT;
}

int anope_event_whois(const char *source, int ac, const char **av)
{
	m_whois(source, av[0]);
	return MOD_CONT;
}

int anope_event_metadata(const char *source, int ac, const char **av)
{
	User *u;

	if (ac < 3)
		return MOD_CONT;
	else if (!strcmp(av[1], "accountname"))
	{
		if ((u = finduser(av[0])))
		{
			/* Identify the user for this account - Adam */
			u->AutoID(av[2]);
		}
	}

	return MOD_CONT;
}

int anope_event_capab(const char *source, int ac, const char **av)
{
	if (strcasecmp(av[0], "START") == 0) {
		/* reset CAPAB */
		has_servicesmod = 0;
		has_globopsmod = 0;
		has_svsholdmod = 0;
		has_chghostmod = 0;
		has_chgidentmod = 0;
		has_messagefloodmod = 0;
		has_banexceptionmod = 0;
		has_inviteexceptionmod = 0;
		has_hidechansmod = 0;

	} else if (strcasecmp(av[0], "MODULES") == 0) {
		if (strstr(av[1], "m_globops.so")) {
			has_globopsmod = 1;
		}
		if (strstr(av[1], "m_services_account.so")) {
			has_servicesmod = 1;
		}
		if (strstr(av[1], "m_svshold.so")) {
			has_svsholdmod = 1;
		}
		if (strstr(av[1], "m_chghost.so")) {
			has_chghostmod = 1;
		}
		if (strstr(av[1], "m_chgident.so")) {
			has_chgidentmod = 1;
		}
		if (strstr(av[1], "m_messageflood.so")) {
			has_messagefloodmod = 1;
		}
		if (strstr(av[1], "m_banexception.so")) {
			has_banexceptionmod = 1;
		}
		if (strstr(av[1], "m_inviteexception.so")) {
			has_inviteexceptionmod = 1;
		}
		if (strstr(av[1], "m_hidechans.so")) {
			has_hidechansmod = 1;
		}
		if (strstr(av[1], "m_servprotect.so")) {
			ircd->pseudoclient_mode = "+Ik";
		}
	} else if (strcasecmp(av[0], "CAPABILITIES") == 0) {
		spacesepstream ssep(av[1]);
		std::string capab;
		while (ssep.GetToken(capab))
		{
			if (capab.find("CHANMODES") != std::string::npos)
			{
				std::string modes(capab.begin() + 10, capab.end());
				commasepstream sep(modes);
				std::string modebuf;

				sep.GetToken(modebuf);
				for (size_t t = 0; t < modebuf.size(); ++t)
				{
					switch (modebuf[t])
					{
						case 'b':
							ModeManager::AddChannelMode(new ChannelModeBan('b'));
							continue;
						case 'e':
							ModeManager::AddChannelMode(new ChannelModeExcept('e'));
							continue;
						case 'I':
							ModeManager::AddChannelMode(new ChannelModeInvite('I'));
							continue;
						/* InspIRCd sends q and a here if they have no prefixes */
						case 'q':
							ModeManager::AddChannelMode(new ChannelModeStatus(CMODE_OWNER, "CMODE_OWNER", 'q', '@'));
							continue;
						case 'a':
							ModeManager::AddChannelMode(new ChannelModeStatus(CMODE_PROTECT	, "CMODE_PROTECT", 'a', '@'));
							continue;
						// XXX list modes needs a bit of a rewrite, we need to be able to support +g here
						default:
							ModeManager::AddChannelMode(new ChannelModeList(CMODE_END, "", modebuf[t]));
					}
				}

				sep.GetToken(modebuf);
				for (size_t t = 0; t < modebuf.size(); ++t)
				{
					switch (modebuf[t])
					{
						case 'k':
							ModeManager::AddChannelMode(new ChannelModeKey('k'));
							continue;
						default:
							ModeManager::AddChannelMode(new ChannelModeParam(CMODE_END, "", modebuf[t]));
					}
				}

				sep.GetToken(modebuf);
				for (size_t t = 0; t < modebuf.size(); ++t)
				{
					switch (modebuf[t])
					{
						case 'F':
							ModeManager::AddChannelMode(new ChannelModeParam(CMODE_NICKFLOOD, "CMODE_NICKFLOOD", 'F', true));
							continue;
						case 'J':
							ModeManager::AddChannelMode(new ChannelModeParam(CMODE_NOREJOIN, "CMODE_NOREJOIN", 'J', true));
							continue;
						case 'L':
							ModeManager::AddChannelMode(new ChannelModeParam(CMODE_REDIRECT, "CMODE_REDIRECT", 'L', true));
							continue;
						case 'f':
							ModeManager::AddChannelMode(new ChannelModeFlood('f', true));
							continue;
						case 'j':
							ModeManager::AddChannelMode(new ChannelModeParam(CMODE_JOINFLOOD, "CMODE_JOINFLOOD", 'j', true));
							continue;
						case 'l':
							ModeManager::AddChannelMode(new ChannelModeParam(CMODE_LIMIT, "CMODE_LIMIT", 'l', true));
							continue;
						default:
							ModeManager::AddChannelMode(new ChannelModeParam(CMODE_END, "", modebuf[t], true));
					}
				}

				sep.GetToken(modebuf);
				for (size_t t = 0; t < modebuf.size(); ++t)
				{
					switch (modebuf[t])
					{
						case 'A':
							ModeManager::AddChannelMode(new ChannelMode(CMODE_ALLINVITE, "CMODE_ALLINVITE", 'A'));
							continue;
						case 'B':
							ModeManager::AddChannelMode(new ChannelMode(CMODE_BLOCKCAPS, "CMODE_BLOCKCAPS", 'B'));
							continue;
						case 'C':
							ModeManager::AddChannelMode(new ChannelMode(CMODE_NOCTCP, "CMODE_NOCTCP", 'C'));
							continue;
						case 'D':
							ModeManager::AddChannelMode(new ChannelMode(CMODE_DELAYEDJOIN, "CMODE_DELAYEDJOIN", 'D'));
							continue;
						case 'G':
							ModeManager::AddChannelMode(new ChannelMode(CMODE_FILTER, "CMODE_FILTER", 'G'));
							continue;
						case 'K':
							ModeManager::AddChannelMode(new ChannelMode(CMODE_NOKNOCK, "CMODE_NOKNOCK", 'K'));
							continue;
						case 'M':
							ModeManager::AddChannelMode(new ChannelMode(CMODE_REGMODERATED, "CMODE_REGMODERATED", 'M'));
							continue;
						case 'N':
							ModeManager::AddChannelMode(new ChannelMode(CMODE_NONICK, "CMODE_NONICK", 'N'));
							continue;
						case 'O':
							ModeManager::AddChannelMode(new ChannelModeOper('O'));
							continue;
						case 'P':
							ModeManager::AddChannelMode(new ChannelMode(CMODE_PERM, "CMODE_PERM", 'P'));
							continue;
						case 'Q':
							ModeManager::AddChannelMode(new ChannelMode(CMODE_NOKICK, "CMODE_NOKICK", 'Q'));
							continue;
						case 'R':
							ModeManager::AddChannelMode(new ChannelMode(CMODE_REGISTEREDONLY, "CMODE_REGISTEREDONLY", 'R'));
							continue;
						case 'S':
							ModeManager::AddChannelMode(new ChannelMode(CMODE_STRIPCOLOR, "CMODE_STRIPCOLOR", 'S'));
							continue;
						case 'T':
							ModeManager::AddChannelMode(new ChannelMode(CMODE_NONOTICE, "CMODE_NONOTICE", 'T'));
							continue;
						case 'c':
							ModeManager::AddChannelMode(new ChannelMode(CMODE_BLOCKCOLOR, "CMODE_BLOCKCOLOR", 'c'));
							continue;
						case 'i':
							ModeManager::AddChannelMode(new ChannelMode(CMODE_INVITE, "CMODE_INVITE", 'i'));
							continue;
						case 'm':
							ModeManager::AddChannelMode(new ChannelMode(CMODE_MODERATED, "CMODE_MODERATED", 'm'));
							continue;
						case 'n':
							ModeManager::AddChannelMode(new ChannelMode(CMODE_NOEXTERNAL, "CMODE_NOEXTERNAL", 'n'));
							continue;
						case 'p':
							ModeManager::AddChannelMode(new ChannelMode(CMODE_PRIVATE, "CMODE_PRIVATE", 'p'));
							continue;
						case 'r':
							ModeManager::AddChannelMode(new ChannelModeRegistered('r'));
							continue;
						case 's':
							ModeManager::AddChannelMode(new ChannelMode(CMODE_SECRET, "CMODE_SECRET", 's'));
							continue;
						case 't':
							ModeManager::AddChannelMode(new ChannelMode(CMODE_TOPIC, "CMODE_TOPIC", 't'));
							continue;
						case 'u':
							ModeManager::AddChannelMode(new ChannelMode(CMODE_AUDITORIUM, "CMODE_AUDITORIUM", 'u'));
							continue;
						case 'z':
							ModeManager::AddChannelMode(new ChannelMode(CMODE_SSL, "CMODE_SSL", 'z'));
							continue;
						default:
							ModeManager::AddChannelMode(new ChannelMode(CMODE_END, "", modebuf[t]));
					}
				}
			}
			else if (capab.find("USERMODES") != std::string::npos)
			{
				std::string modes(capab.begin() + 10, capab.end());
				commasepstream sep(modes);
				std::string modebuf;

				while (sep.GetToken(modebuf))
				{
					for (size_t t = 0; t < modebuf.size(); ++t)
					{
						switch (modebuf[t])
						{
							case 'h':
								ModeManager::AddUserMode(new UserMode(UMODE_HELPOP, "UMODE_HELPOP", 'h'));
								continue;
							case 's':
								ModeManager::AddUserMode(new UserMode(UMODE_STRIPCOLOR, "UMODE_STRIPCOLOR", 'S'));
								continue;
							case 'B':
								ModeManager::AddUserMode(new UserMode(UMODE_BOT, "UMODE_BOT", 'B'));
								continue;
							case 'G':
								ModeManager::AddUserMode(new UserMode(UMODE_FILTER, "UMODE_FILTER", 'G'));
								continue;
							case 'H':
								ModeManager::AddUserMode(new UserMode(UMODE_HIDEOPER, "UMODE_HIDEOPER", 'H'));
								continue;
							case 'I':
								ModeManager::AddUserMode(new UserMode(UMODE_PRIV, "UMODE_PRIV", 'I'));
								continue;
							case 'Q':
								ModeManager::AddUserMode(new UserMode(UMODE_HIDDEN, "UMODE_HIDDEN", 'Q'));
								continue;
							case 'R':
								ModeManager::AddUserMode(new UserMode(UMODE_REGPRIV, "UMODE_REGPRIV", 'R'));
								continue;
							case 'S':
								ModeManager::AddUserMode(new UserMode(UMODE_STRIPCOLOR, "UMODE_STRIPCOLOR", 'S'));
								continue;
							case 'W':
								ModeManager::AddUserMode(new UserMode(UMODE_WHOIS, "UMODE_WHOIS", 'W'));
								continue;
							case 'c':
								ModeManager::AddUserMode(new UserMode(UMODE_COMMONCHANS, "UMODE_COMMONCHANS", 'c'));
								continue;
							case 'g':
								ModeManager::AddUserMode(new UserMode(UMODE_CALLERID, "UMODE_CALLERID", 'g'));
								continue;
							case 'i':
								ModeManager::AddUserMode(new UserMode(UMODE_INVIS, "UMODE_INVIS", 'i'));
								continue;
							case 'k':
								ModeManager::AddUserMode(new UserMode(UMODE_PROTECTED, "UMODE_PROTECTED", 'k'));
								continue;
							case 'o':
								ModeManager::AddUserMode(new UserMode(UMODE_OPER, "UMODE_OPER", 'o'));
								continue;
							case 'r':
								ModeManager::AddUserMode(new UserMode(UMODE_REGISTERED, "UMODE_REGISTERED", 'r'));
								continue;
							case 'w':
								ModeManager::AddUserMode(new UserMode(UMODE_WALLOPS, "UMODE_WALLOPS", 'w'));
								continue;
							case 'x':
								ModeManager::AddUserMode(new UserMode(UMODE_CLOAK, "UMODE_CLOAK", 'x'));
								continue;
							case 'd':
								ModeManager::AddUserMode(new UserMode(UMODE_DEAF, "UMODE_DEAF", 'd'));
								continue;
							default:
								ModeManager::AddUserMode(new UserMode(UMODE_END, "", modebuf[t]));
						}
					}
				}
			}
			else if (capab.find("PREFIX=(") != std::string::npos)
			{
				std::string modes(capab.begin() + 8, capab.begin() + capab.find(")"));
				std::string chars(capab.begin() + capab.find(")") + 1, capab.end());
				
				for (size_t t = 0; t < modes.size(); ++t)
				{
					switch (modes[t])
					{
						case 'q':
							ModeManager::AddChannelMode(new ChannelModeStatus(CMODE_OWNER, "CMODE_OWNER", 'q', chars[t]));
							continue;
						case 'a':
							ModeManager::AddChannelMode(new ChannelModeStatus(CMODE_PROTECT, "CMODE_PROTECT", 'a', chars[t]));
							continue;
						case 'o':
							ModeManager::AddChannelMode(new ChannelModeStatus(CMODE_OP, "CMODE_OP", 'o', chars[t]));
							continue;
						case 'h':
							ModeManager::AddChannelMode(new ChannelModeStatus(CMODE_HALFOP, "CMODE_HALFOP", 'h', chars[t]));
							continue;
						case 'v':
							ModeManager::AddChannelMode(new ChannelModeStatus(CMODE_VOICE, "CMODE_VOICE", 'v', chars[t]));
							continue;
					}
				}
			}
			else if (capab.find("MAXMODES=") != std::string::npos)
			{
				std::string maxmodes(capab.begin() + 9, capab.end());
				ircd->maxmodes = atoi(maxmodes.c_str());
			}
		}
	} else if (strcasecmp(av[0], "END") == 0) {
		if (!has_globopsmod) {
			send_cmd(NULL, "ERROR :m_globops is not loaded. This is required by Anope");
			quitmsg = "Remote server does not have the m_globops module loaded, and this is required.";
			quitting = 1;
			return MOD_STOP;
		}
		if (!has_servicesmod) {
			send_cmd(NULL, "ERROR :m_services_account.so is not loaded. This is required by Anope");
			quitmsg = "ERROR: Remote server does not have the m_services_account module loaded, and this is required.";
			quitting = 1;
			return MOD_STOP;
		}
		if (!has_hidechansmod) {
			send_cmd(NULL, "ERROR :m_hidechans.so is not loaded. This is required by Anope");
			quitmsg = "ERROR: Remote server does not have the m_hidechans module loaded, and this is required.";
			quitting = 1;
			return MOD_STOP;
		}
		if (!has_svsholdmod) {
			ircdproto->SendGlobops(OperServ, "SVSHOLD missing, Usage disabled until module is loaded.");
		}
		if (!has_chghostmod) {
			ircdproto->SendGlobops(OperServ, "CHGHOST missing, Usage disabled until module is loaded.");
		}
		if (!has_chgidentmod) {
			ircdproto->SendGlobops(OperServ, "CHGIDENT missing, Usage disabled until module is loaded.");
		}
		ircd->svshold = has_svsholdmod;
	}

	CapabParse(ac, av);

	return MOD_CONT;
}

int anope_event_endburst(const char *source, int ac, const char **av)
{
	NickAlias *na;
	User *u = prev_u_intro;
	Server *s = Server::Find(source ? source : "");

	if (!s)
	{
		throw new CoreException("Got ENDBURST without a source");
	}

	/* Check if the previously introduced user was Id'd for the nickgroup of the nick he s currently using.
	 * If not, validate the user. ~ Viper*/
	prev_u_intro = NULL;
	if (u) na = findnick(u->nick);
	if (u && !u->server->IsSynced() && (!na || na->nc != u->Account()))
	{
		validate_user(u);
		if (u->HasMode(UMODE_REGISTERED))
			u->RemoveMode(NickServ, UMODE_REGISTERED);
	}

	Alog() << "Processed ENDBURST for " << s->GetName();

	s->Sync(true);
	return MOD_CONT;
}

void moduleAddIRCDMsgs()
{
	Anope::AddMessage("ENDBURST", anope_event_endburst);
	Anope::AddMessage("436", anope_event_436);
	Anope::AddMessage("AWAY", anope_event_away);
	Anope::AddMessage("JOIN", anope_event_join);
	Anope::AddMessage("KICK", anope_event_kick);
	Anope::AddMessage("KILL", anope_event_kill);
	Anope::AddMessage("MODE", anope_event_mode);
	Anope::AddMessage("MOTD", anope_event_motd);
	Anope::AddMessage("NICK", anope_event_nick);
	Anope::AddMessage("UID", anope_event_uid);
	Anope::AddMessage("CAPAB", anope_event_capab);
	Anope::AddMessage("PART", anope_event_part);
	Anope::AddMessage("PING", anope_event_ping);
	Anope::AddMessage("TIME", anope_event_time);
	Anope::AddMessage("PRIVMSG", anope_event_privmsg);
	Anope::AddMessage("QUIT", anope_event_quit);
	Anope::AddMessage("SERVER", anope_event_server);
	Anope::AddMessage("SQUIT", anope_event_squit);
	Anope::AddMessage("RSQUIT", anope_event_rsquit);
	Anope::AddMessage("TOPIC", anope_event_topic);
	Anope::AddMessage("WHOIS", anope_event_whois);
	Anope::AddMessage("SVSMODE", anope_event_mode);
	Anope::AddMessage("FHOST", anope_event_chghost);
	Anope::AddMessage("CHGIDENT", anope_event_chgident);
	Anope::AddMessage("FNAME", anope_event_chgname);
	Anope::AddMessage("SETHOST", anope_event_sethost);
	Anope::AddMessage("SETIDENT", anope_event_setident);
	Anope::AddMessage("SETNAME", anope_event_setname);
	Anope::AddMessage("FJOIN", anope_event_fjoin);
	Anope::AddMessage("FMODE", anope_event_fmode);
	Anope::AddMessage("FTOPIC", anope_event_ftopic);
	Anope::AddMessage("OPERTYPE", anope_event_opertype);
	Anope::AddMessage("IDLE", anope_event_idle);
	Anope::AddMessage("METADATA", anope_event_metadata);
}

bool ChannelModeFlood::IsValid(const std::string &value)
{
	char *dp, *end;
	if (!value.empty() && value[0] != ':' && strtoul((value[0] == '*' ? value.c_str() + 1 : value.c_str()), &dp, 10) > 0 && *dp == ':' && *(++dp) && strtoul(dp, &end, 10) > 0 && !*end) return 1;
	else return 0;
}

class ProtoInspIRCd : public Module
{
 public:
	ProtoInspIRCd(const std::string &modname, const std::string &creator) : Module(modname, creator)
	{
		this->SetAuthor("Anope");
		this->SetVersion(VERSION_STRING);
		this->SetType(PROTOCOL);

		if (Config.Numeric)
			TS6SID = sstrdup(Config.Numeric);

		pmodule_ircd_version("InspIRCd 2.0");
		pmodule_ircd_var(myIrcd);
		pmodule_ircd_useTSMode(0);

		CapabType c[] = { CAPAB_NOQUIT, CAPAB_SSJ3, CAPAB_NICK2, CAPAB_VL, CAPAB_TLKEXT	};
		for (unsigned i = 0; i < 5; ++i)
			Capab.SetFlag(c[i]);

		pmodule_ircd_proto(&ircd_proto);
		moduleAddIRCDMsgs();

		ModuleManager::Attach(I_OnUserNickChange, this);
	}

	~ProtoInspIRCd()
	{
		delete [] TS6SID;
	}

	void OnUserNickChange(User *u, const std::string &)
	{
		u->RemoveModeInternal(ModeManager::FindUserModeByName(UMODE_REGISTERED));
	}
};

MODULE_INIT(ProtoInspIRCd)
