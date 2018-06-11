/**
 * Copyright (c) 2011-2015 libbitcoin developers (see AUTHORS)
 * Copyright (c) 2016-2018 metaverse core developers (see MVS-AUTHORS)
 *
 * This file is part of metaverse-explorer.
 *
 * metaverse-explorer is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License with
 * additional permissions to the one published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version. For more information see LICENSE.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <metaverse/mgbubble/MongooseCli.hpp>
#include <jsoncpp/json/json.h>

/**
 * Invoke this program with the raw arguments provided on the command line.
 * All console input and output streams for the application originate here.
 * @param argc  The number of elements in the argv array.
 * @param argv  The array of arguments, including the process.
 * @return      The numeric result to return via console exit.
 */
using namespace mgbubble::cli;

void my_impl(const http_message* hm)
{
    auto&& reply = std::string(hm->body.p, hm->body.len);
    std::cout << reply << std::endl;
}

int main(int argc, char* argv[])
{
    std::string url{"127.0.0.1:8000/rpc"};

    // HTTP request call commands
    HttpReq req(url, 3000, reply_handler(my_impl));

    Json::Value jsonvar;
    Json::Value jsonopt;
    jsonvar["jsonrpc"] = "2.0";
    jsonvar["id"] = 1;
    jsonvar["method"] = (argc > 1) ? argv[1] : "help";
    jsonvar["params"] = Json::arrayValue;

    if (argc > 2)
    {
        for (int i = 2; i < argc; i++)
        {
            jsonvar["params"].append(argv[i]);
        }
    }

    req.post(jsonvar.toStyledString());
    return 0;
}
