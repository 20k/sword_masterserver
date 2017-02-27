#include <iostream>
#include <net/shared.hpp>

#include <vector>
#include <map>
#include "server.hpp"
#include "network_messages.hpp"

#include <iostream>
#include <iomanip>
#include <ctime>

using namespace std;

std::vector<tcp_sock> sockets;

void cleanup()
{
    for(auto& i : sockets)
        closesocket(i.get());
}

struct udp_serv_info
{
    int32_t player_count = 0;
    int32_t port_num = atoi(GAMESERVER_PORT);
};

struct udp_game_server
{
    sockaddr_storage store;
    sf::Clock timeout_time;

    udp_serv_info info;

    constexpr static float timeout_s = 3;
};

bool contains(std::vector<udp_game_server>& servers, sockaddr_storage& store)
{
    for(auto& i : servers)
    {
        if(i.store == store)
            return true;
    }

    return false;
}

udp_serv_info process_ping(byte_fetch& fetch)
{
    if(fetch.ptr.size() < sizeof(int32_t)*2)
        return {};

    int32_t player_count = fetch.get<int32_t>();
    int32_t port_num = fetch.get<int32_t>();

    udp_serv_info info;

    info.player_count = player_count;
    info.port_num = port_num;

    return info;
}

void receive_pings(std::vector<udp_game_server>& servers)
{
    static udp_sock host;
    static bool init = false;

    if(!init)
    {
        host = udp_host(MASTER_PORT);
        init = true;

        printf("Registerd udp on port %s\n", host.get_host_port().c_str());
    }

    if(!sock_readable(host))
        return;

    sockaddr_storage store;

    auto data = udp_receive_from(host, &store);

    if(!contains(servers, store))
    {
        printf("New server: IP %s Port %s\n", get_addr_ip(store).c_str(), get_addr_port(store).c_str());

        servers.push_back({store, sf::Clock()});
    }

    if(data.size() <= 0)
        return;

    byte_fetch fetch;
    fetch.ptr.swap(data);

    udp_serv_info info = process_ping(fetch);

    //printf("%i %i\n", info.player_count, info.port_num);

    for(int i=0; i<servers.size(); i++)
    {
        if(servers[i].store == store)
        {
            servers[i].timeout_time.restart();

            servers[i].info = info;
        }
    }
}

void process_timeouts(std::vector<udp_game_server>& servers)
{
    for(int i=0; i<servers.size(); i++)
    {
        auto serv = servers[i];

        if(serv.timeout_time.getElapsedTime().asSeconds() > serv.timeout_s)
        {
            printf("timeout gameserver\n");

            servers.erase(servers.begin() + i);
            i--;
            continue;
        }
    }
}

std::vector<char> get_udp_client_response(std::vector<udp_game_server>& servers)
{
    byte_vector vec;

    vec.push_back(canary_start);
    vec.push_back(message::CLIENTRESPONSE);

    int32_t server_nums = servers.size();

    vec.push_back(server_nums);

    for(int i=0; i<server_nums; i++)
    {
        udp_game_server serv = servers[i];

        std::string ip = get_addr_ip(serv.store);
        int32_t len = ip.length();

        vec.push_back(len);
        vec.push_string<std::string>(ip.c_str(), len);

        uint32_t port = serv.info.port_num;

        vec.push_back(port);
    }

    vec.push_back(canary_end);

    return vec.ptr;
}

int main()
{
    tcp_sock sockfd = tcp_host(MASTER_PORT);

    ///incase of an unclean exit.
    atexit(cleanup);

    //master_server master;

    std::vector<udp_game_server> udp_serverlist;

    ///I think we have to keepalive the connections
    while(1)
    {
        receive_pings(udp_serverlist);
        process_timeouts(udp_serverlist);

        tcp_sock new_fd = conditional_accept(sockfd);

        ///really... we want to wait for something
        ///to identify as a server, or client
        if(new_fd.valid())
        {
            sockets.push_back(new_fd);
        }

        //master.cull_dead();

        //master.tick_all();

        for(int i=0; i<sockets.size(); i++)
        {
            tcp_sock fd = sockets[i];

            if(sock_readable(fd))
            {
                auto data = tcp_recv(fd);

                //if(data.size() == 0 || fd.invalid())
                if(fd.invalid())
                {
                    printf("boo, a client disconnected!\n");

                    fd.close();

                    sockets.erase(sockets.begin() + i);
                    i--;
                    continue;
                }

                byte_fetch fetch;
                fetch.ptr.swap(data);

                while(!fetch.finished())
                {
                    int32_t found_canary = fetch.get<int32_t>();

                    while(found_canary != canary_start && !fetch.finished())
                    {
                        found_canary = fetch.get<int32_t>();
                    }

                    int32_t type = fetch.get<int32_t>();

                    /*if(type == message::GAMESERVER)
                    {
                        ///the port the server is hosting on, NOT COMMUNICATING WITH ME
                        uint32_t server_port = fetch.get<uint32_t>();

                        int32_t found_end = fetch.get<int32_t>();

                        if(found_end != canary_end)
                            continue;

                        game_server serv = master.server_from_sock(fd, server_port);
                        master.add_server(serv);

                        printf("adding new gameserver\n");

                        sockets.erase(sockets.begin() + i);
                        i--;
                        continue;
                    }*/

                    if(type == message::CLIENT)
                    {
                        auto t = std::time(nullptr);
                        auto tm = *std::localtime(&t);

                        printf("client ping\n");
                        std::cout << std::put_time(&tm, "%d-%m-%Y %H-%M-%S") << std::endl;

                        int32_t found_end = fetch.get<int32_t>();

                        if(found_end != canary_end)
                            continue;

                        //tcp_send(fd, master.get_client_response());

                        tcp_send(fd, get_udp_client_response(udp_serverlist));

                        fd.close();

                        sockets.erase(sockets.begin() + i);
                        i--;
                        continue;
                    }
                }
            }
        }

        sf::sleep(sf::milliseconds(1));
    }

    closesocket(sockfd.get());

    for(auto& i : sockets)
        closesocket(i.get());

    return 0;
}
