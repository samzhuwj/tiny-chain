/*
 * Copyright (c) 2016-2017 metaverse core developers (see MVS-AUTHORS).
 * Copyright (C) 2013, 2016 Swirly Cloud Limited.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program; if
 * not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */
#include <exception>
#include <functional> //hash
#include <list>

#include <metaverse/mgbubble/RestServ.hpp>
#include <metaverse/mgbubble/exception/Instances.hpp>
#include <metaverse/mgbubble/utility/Stream_buf.hpp>

#include <tinychain/commands.hpp>

namespace mgbubble{

thread_local OStream RestServ::out_;
thread_local Tokeniser<'/'> RestServ::uri_;
thread_local int RestServ::state_ = 0;

void RestServ::reset(HttpMessage& data) noexcept
{
    state_ = 0;

    const auto method = data.method();
    if (method == "GET") {
      state_ |= MethodGet;
    } else if (method == "POST") {
      state_ |= MethodPost;
    } else if (method == "PUT") {
      state_ |= MethodPut;
    } else if (method == "DELETE") {
      state_ |= MethodDelete;
    }

    auto uri = data.uri();
    // Remove leading slash.
    if (uri.front() == '/') {
      uri.remove_prefix(1);
    }
    uri_.reset(uri);
}

void RestServ::httpStatic(mg_connection& nc, HttpMessage data)
{
    mg_serve_http(&nc, data.get(), httpoptions_);
}

void RestServ::websocketBroadcast(mg_connection& nc, const char* msg, size_t len) 
{
    mg_connection* iter;

    for (iter = mg_next(nc.mgr, nullptr); iter != nullptr; iter = mg_next(nc.mgr, iter))
    {
      mg_send_websocket_frame(iter, WEBSOCKET_OP_TEXT, msg, len);
    }
}

void RestServ::websocketSend(mg_connection* nc, const char* msg, size_t len) 
{
    mg_send_websocket_frame(nc, WEBSOCKET_OP_TEXT, msg, len);
}


// --------------------- websocket interface -----------------------
void RestServ::websocketSend(mg_connection& nc, WebsocketMessage ws) 
{
    //process here
    std::stringstream sout;
    try{
        ws.data_to_arg();
        tinychain::commands cmd{ws.vargv(), chain_};
        Json::Value ret;
        cmd.exec(ret);

        sout<<ret.toStyledString();

    } catch(std::exception& e) {
        sout << e.what();
    }

    websocketSend(&nc, sout.str().c_str(), sout.str().size());
}

// --------------------- json rpc interface -----------------------
void RestServ::httpRpcRequest(mg_connection& nc, HttpMessage data)
{
    reset(data);

    StreamBuf buf{nc.send_mbuf};
    out_.rdbuf(&buf);
    out_.reset(200, "OK");
    try {
        if (uri_.empty() || uri_.top() != "rpc") {
            throw ForbiddenException{"URI not support"};
        }

        //process here
        data.data_to_arg();
        tinychain::commands cmd{data.vargv(), chain_};
        Json::Value ret;
        cmd.exec(ret);

        out_<<ret.toStyledString();

    } catch (const std::exception& e) {
        out_ << e.what();
    } 

    out_.setContentLength(); 
}

// --------------------- Restful-api interface -----------------------
void RestServ::httpRequest(mg_connection& nc, HttpMessage data)
{

    reset(data);

    StreamBuf buf{nc.send_mbuf};
    out_.rdbuf(&buf);
    out_.reset(200, "OK");

    try {
        if (uri_.empty() || uri_.top() != "api") {
            throw ForbiddenException{"URI not support"};
        }
        uri_.pop();

        state_|= MatchUri;
        state_|= MatchMethod;

    } catch (const std::exception& e) {
        out_ << e.what();
    }

    out_.setContentLength(); 
}

std::shared_ptr<Session> RestServ::push_session(HttpMessage data)
{
    char user[64]{0x00}, pass[64]{0x00};
    auto ul = mg_get_http_var(&(data.get()->body), "user", user, sizeof(user));
    auto pl = mg_get_http_var(&(data.get()->body), "pass", pass, sizeof(pass));

    auto s = std::make_shared<Session>();

    s->created = s->last_used = mg_time();
    s->user = std::string(user, ul);
    s->pass = std::string(pass, pl);

    s->id = std::hash<std::shared_ptr<Session>>()(s);
    std::string&& seed = std::string(user) + std::to_string(s->id);
    s->id = std::hash<std::string>()(seed);

    session_list_.push_back(s);

    return s;
}

std::shared_ptr<Session> RestServ::get_from_session_list(HttpMessage data)
{
    mg_str* cookie_header = mg_get_http_header(data.get(), "cookie");;
    if (cookie_header == nullptr) 
        return nullptr;

    char ssid[32]{0x00};
    if (!mg_http_parse_header(cookie_header, SESSION_COOKIE_NAME, ssid, sizeof(ssid)))
        return nullptr;

    auto sid = std::stoul(ssid, nullptr, 10);

    auto ret = std::find_if(session_list_.begin(), session_list_.end(), [&sid](std::shared_ptr<Session> p){
            return sid == p->id;
            });

    if (ret == session_list_.end())
        return nullptr;

    (*ret)->last_used = mg_time();
    return *ret;
}

bool RestServ::remove_from_session_list(HttpMessage data)
{
    mg_str* cookie_header = mg_get_http_header(data.get(), "cookie");;
    if (cookie_header == nullptr) 
        return false;

    char ssid[32]{0x00};
    if (!mg_http_parse_header(cookie_header, SESSION_COOKIE_NAME, ssid, sizeof(ssid)))
        return false;

    auto sid = std::stoul(ssid, nullptr, 10);

    for (auto iter = session_list_.begin(); iter != session_list_.end(); ++iter)
    {
        if ( (*iter)->id == sid )
        {
            iter = session_list_.erase(iter);
        }
    }

    return true;
}

bool RestServ::check_sessions()
{
    auto threshold = mg_time() - SESSION_TTL;

    for (auto iter = session_list_.begin(); iter != session_list_.end(); ++iter)
    {
        if ( (*iter)->last_used < threshold )
        {
            iter = session_list_.erase(iter);
        }
    }
    return true;
}

bool RestServ::user_auth(mg_connection& nc, HttpMessage data)
{
    char user[64]{0x00}, pass[64]{0x00};
    auto ul = mg_get_http_var(&(data.get()->body), "user", user, sizeof(user));
    auto pl = mg_get_http_var(&(data.get()->body), "pass", pass, sizeof(pass));

    try{
        if (ul > 0 && pl > 0){
            //TODO Check valid
        }else{
        }

    } catch(std::exception& e){
        out_ << e.what();
        return false;
    }

    return true;
}

}// mgbubble

