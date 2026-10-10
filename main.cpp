#include <iostream>
#include <cstring>
#include <vector>
#include <string>
#include <ctime>
#include <cstdio>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>
#include <sstream>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "src/worldgenerator.hpp"
#include "src/survival.hpp"
#include "src/inventory.hpp"

#pragma comment(lib, "ws2_32.lib")

#define PORT 25565
#define BUFFER_SIZE 8192

std::atomic<bool> server_running(true);

void log_msg(const std::string& message) {
    std::time_t now = std::time(nullptr);
    std::tm* local_time = std::localtime(&now);
    char time_buf[9];
    std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S", local_time);
    std::cout << "[Tiny-stone] " << time_buf << " : " << message << "\n";
}

int32_t readVarInt(const char*& cursor, const char* end) {
    int32_t numRead = 0;
    int32_t result = 0;
    uint8_t read;
    do {
        if (cursor >= end) return -1;
        read = *cursor++;
        int32_t value = (read & 0b01111111);
        result |= (value << (7 * numRead));
        numRead++;
        if (numRead > 5) return -1;
    } while ((read & 0b10000000) != 0);
    return result;
}

void appendVarInt(std::string& dest, int32_t val) {
    do {
        uint8_t temp = val & 0b01111111;
        val >>= 7;
        if (val != 0) temp |= 0b10000000;
        dest.push_back(temp);
    } while (val != 0);
}

void sendPacket(SOCKET sock, int32_t packet_id, const std::string& data) {
    std::string temp_id_varint = "";
    appendVarInt(temp_id_varint, packet_id);

    int32_t total_length = temp_id_varint.length() + data.length();

    std::string packet = "";
    appendVarInt(packet, total_length);
    packet.append(temp_id_varint);
    packet.append(data);

    send(sock, packet.c_str(), packet.length(), 0);
}

void sendBlockChange(SOCKET sock, int x, int y, int z, int32_t block_state_id) {
    std::string payload = "";
    int64_t encoded_pos = ((x & 0x3FFFFFFLL) << 38) | ((y & 0xFFFLL) << 26) | (z & 0x3FFFFFFLL);
    for(int i = 7; i >= 0; i--) payload.push_back((encoded_pos >> (i * 8)) & 0xFF);
    appendVarInt(payload, block_state_id);
    sendPacket(sock, 0x0B, payload); // 0x0B = SPacketBlockChange
}

struct ClientSession {
    SOCKET socket;
    int connection_state;
    std::string username;
    PlayerState player;
    PlayerInventory inventory;
    bool active;
    std::chrono::steady_clock::time_point last_keep_alive;
};

std::vector<std::shared_ptr<ClientSession>> clients;
std::mutex clients_mutex;

// --- COMMAND CONSOLE THREAD ---
void consoleInputThread() {
    std::string line;
    while (server_running && std::getline(std::cin, line)) {
        if (line.empty()) continue;

        std::stringstream ss(line);
        std::string cmd;
        ss >> cmd;

        if (cmd == "hurt") {
            std::string target_name;
            float amount;
            if (ss >> target_name >> amount) {
                std::lock_guard<std::mutex> lock(clients_mutex);
                bool found = false;
                for (auto& client : clients) {
                    if (!client->active || client->connection_state != 3) continue;
                    if (client->username == target_name) {
                        client->player.health = std::max(0.0f, client->player.health - amount);
                        SurvivalManager::updateIndicators(client->socket, client->player);
                        log_msg("Berhasil mengurangi health " + target_name + " sebesar " + std::to_string(amount) + ". Sisa HP: " + std::to_string(client->player.health));
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    log_msg("Pemain '" + target_name + "' tidak ditemukan atau offline.");
                }
            } else {
                log_msg("Format salah! Penggunaan: hurt <player_name> <amount>");
            }
        } 
        else if (cmd == "hunger") {
            std::string target_name;
            int amount;
            if (ss >> target_name >> amount) {
                std::lock_guard<std::mutex> lock(clients_mutex);
                bool found = false;
                for (auto& client : clients) {
                    if (!client->active || client->connection_state != 3) continue;
                    if (client->username == target_name) {
                        client->player.food_level = std::max(0, std::min(20, amount));
                        SurvivalManager::updateIndicators(client->socket, client->player);
                        log_msg("Berhasil mengubah hunger " + target_name + " menjadi " + std::to_string(client->player.food_level));
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    log_msg("Pemain '" + target_name + "' tidak ditemukan atau offline.");
                }
            } else {
                log_msg("Format salah! Penggunaan: hunger <player_name> <amount>");
            }
        } 
        else {
            log_msg("Perintah tidak dikenal. Perintah tersedia: hurt, hunger");
        }
    }
}

// --- VANILLA MINECRAFT TICK LOOP (20 TPS / 50ms per tick) ---
void serverTickLoop() {
    int64_t world_age = 0;
    int64_t time_of_day = 6000;

    const std::chrono::milliseconds tick_duration(50);
    auto next_tick_time = std::chrono::steady_clock::now() + tick_duration;

    while (server_running) {
        auto now = std::chrono::steady_clock::now();
        if (now < next_tick_time) {
            std::this_thread::sleep_for(next_tick_time - now);
        }
        next_tick_time += tick_duration;

        world_age++;
        time_of_day = (time_of_day + 1) % 24000;

        std::lock_guard<std::mutex> lock(clients_mutex);
        for (auto& client : clients) {
            if (!client->active || client->connection_state != 3) continue;

            SurvivalManager::tickSurvival(client->socket, client->player);

            auto current_time = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(current_time - client->last_keep_alive).count();

            if (duration >= 5) {
                std::string ka_payload = "";
                int64_t random_id = 42;
                for (int i = 7; i >= 0; i--) ka_payload.push_back((random_id >> (i * 8)) & 0xFF);
                
                sendPacket(client->socket, 0x1F, ka_payload);
                client->last_keep_alive = current_time;
            }

            if (world_age % 20 == 0) {
                std::string time_payload = "";
                auto push_long = [&](int64_t val) {
                    for(int i = 7; i >= 0; i--) time_payload.push_back((val >> (i * 8)) & 0xFF);
                };

                push_long(world_age);     
                push_long(time_of_day);   

                sendPacket(client->socket, 0x47, time_payload);
            }
        }
    }
}

void handleClient(SOCKET client_socket) {
    auto session = std::make_shared<ClientSession>();
    session->socket = client_socket;
    session->connection_state = 0;
    session->active = true;
    session->last_keep_alive = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.push_back(session);
    }

    char buffer[BUFFER_SIZE];

    while (session->active) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);
        if (bytes_received <= 0) {
            session->active = false;
            break;
        }

        const char* cursor = buffer;
        const char* end = buffer + bytes_received;

        while (cursor < end) {
            int32_t pkt_len = readVarInt(cursor, end);
            if (pkt_len <= 0 || cursor + pkt_len > end) break;

            const char* packet_end = cursor + pkt_len;
            int32_t pkt_id = readVarInt(cursor, packet_end);

            if (session->connection_state == 0) { // Handshake
                if (pkt_id == 0x00) {
                    int32_t protocol_version = readVarInt(cursor, packet_end);
                    int32_t addr_len = readVarInt(cursor, packet_end);
                    if (addr_len > 0) cursor += addr_len;
                    if (cursor + 2 <= packet_end) cursor += 2;
                    int32_t next_state = readVarInt(cursor, packet_end);
                    session->connection_state = next_state; 
                    log_msg("Handshake received. Protocol: " + std::to_string(protocol_version));
                }
            } 
            else if (session->connection_state == 1) { // Status / MOTD
                if (pkt_id == 0x00) {
                    std::string json_resp = 
                        "{\"version\":{\"name\":\"1.12.2\",\"protocol\":340},"
                        "\"players\":{\"max\":10,\"online\":1,\"sample\":[]},"
                        "\"description\":{\"text\":\"Tiny Stoneblock 1.12.2\"}}";
                    std::string payload = "";
                    appendVarInt(payload, json_resp.length());
                    payload.append(json_resp);
                    sendPacket(client_socket, 0x00, payload);
                } 
                else if (pkt_id == 0x01) { // Ping
                    std::string payload = "";
                    if (packet_end - cursor >= 8) payload.append(cursor, 8);
                    else { for(int i = 0; i < 8; i++) payload.push_back(0); }
                    sendPacket(client_socket, 0x01, payload);
                    session->active = false;
                }
            } 
            else if (session->connection_state == 2) { // Login State
                if (pkt_id == 0x00) { // Login Start
                    int32_t name_len = readVarInt(cursor, packet_end);
                    std::string pname(cursor, name_len);
                    session->username = pname;
                    log_msg("Player logging in: " + pname);

                    std::string login_success = "";
                    std::string uuid_str = "00000000-0000-0000-0000-000000000000";
                    appendVarInt(login_success, uuid_str.length());
                    login_success.append(uuid_str);
                    appendVarInt(login_success, pname.length());
                    login_success.append(pname);
                    sendPacket(client_socket, 0x02, login_success);

                    session->connection_state = 3; 
                    log_msg("Switched to Play State.");

                    std::string join_payload = "";
                    join_payload.append("\x00\x00\x00\x01", 4); 
                    join_payload.push_back(0x00);                
                    join_payload.append("\x00\x00\x00\x00", 4); 
                    join_payload.push_back(0x01);                
                    join_payload.push_back(10);                  
                    
                    std::string level_type = "default";
                    appendVarInt(join_payload, level_type.length());
                    join_payload.append(level_type);
                    
                    join_payload.push_back(0x00);                
                    sendPacket(client_socket, 0x23, join_payload);

                    std::string diff_payload = "";
                    diff_payload.push_back(0x01); 
                    sendPacket(client_socket, 0x0D, diff_payload);

                    std::string abilities_payload = "";
                    abilities_payload.push_back(0x00); 
                    uint32_t fly_spd; float f_fly = 0.05f; std::memcpy(&fly_spd, &f_fly, 4);
                    for(int i = 3; i >= 0; i--) abilities_payload.push_back((fly_spd >> (i * 8)) & 0xFF);
                    uint32_t walk_spd; float f_walk = 0.1f; std::memcpy(&walk_spd, &f_walk, 4);
                    for(int i = 3; i >= 0; i--) abilities_payload.push_back((walk_spd >> (i * 8)) & 0xFF);
                    sendPacket(client_socket, 0x2C, abilities_payload);

                    std::string spawn_pos_payload = "";
                    int64_t spawn_x = 0, spawn_y = 64, spawn_z = 0;
                    int64_t encoded_pos = ((spawn_x & 0x3FFFFFF) << 38) | ((spawn_y & 0xFFF) << 26) | (spawn_z & 0x3FFFFFF);
                    for(int i = 7; i >= 0; i--) spawn_pos_payload.push_back((encoded_pos >> (i * 8)) & 0xFF);
                    sendPacket(client_socket, 0x46, spawn_pos_payload);

                    log_msg("Sending spawn chunks...");
                    for (int cx = -1; cx <= 1; cx++) {
                        for (int cz = -1; cz <= 1; cz++) {
                            sendPacket(client_socket, 0x20, WorldGenerator::getOrGenerateChunk(cx, cz));
                        }
                    }

                    SurvivalManager::updateIndicators(client_socket, session->player);

                    std::string pos = "";
                    auto push_double = [&](double val) {
                        uint64_t b; std::memcpy(&b, &val, 8);
                        for(int i = 7; i >= 0; i--) pos.push_back((b >> (i * 8)) & 0xFF);
                    };
                    auto push_float = [&](float val) {
                        uint32_t b; std::memcpy(&b, &val, 4);
                        for(int i = 3; i >= 0; i--) pos.push_back((b >> (i * 8)) & 0xFF);
                    };

                    push_double(0.5);   
                    push_double(64.0);  
                    push_double(0.5);   
                    push_float(0.0f);   
                    push_float(0.0f);   
                    pos.push_back(0x00); 
                    appendVarInt(pos, 123); 

                    sendPacket(client_socket, 0x2F, pos);
                    log_msg("Player spawned successfully.");
                }
            }
            else if (session->connection_state == 3) { // Play State Handler
                if (pkt_id == 0x00) { // Teleport Confirm
                    int32_t teleport_id = readVarInt(cursor, packet_end);
                    log_msg("Teleport confirmed for ID: " + std::to_string(teleport_id));
                }
                else if (pkt_id == 0x0B) { // Keep-Alive Response
                    // Klien membalas Keep-Alive
                }
                else if (pkt_id == 0x0C || pkt_id == 0x0D || pkt_id == 0x0E) { // Position / Look / Position & Look
                    const char* p_cursor = cursor;
                    auto readDouble = [&](const char*& c) {
                        uint64_t val = 0;
                        for(int i = 7; i >= 0; i--) val |= ((uint64_t)(uint8_t)*c++ << (i * 8));
                        double d; std::memcpy(&d, &val, 8);
                        return d;
                    };

                    double newX = readDouble(p_cursor);
                    double newY = readDouble(p_cursor);
                    double newZ = readDouble(p_cursor);

                    if (pkt_id == 0x0E) p_cursor += 8; 
                    bool onGround = (*p_cursor++ != 0);

                    SurvivalManager::processMovement(client_socket, session->player, newX, newY, newZ, onGround);
                }
                else if (pkt_id == 0x14) { // Player Digging (0x14) - Hancur Block
                    int32_t status = readVarInt(cursor, packet_end);
                    
                    uint64_t enc_pos = 0;
                    for(int i = 7; i >= 0; i--) enc_pos = (enc_pos << 8) | (uint8_t)*cursor++;
                    
                    int x = (int)(enc_pos >> 38);
                    int y = (int)((enc_pos >> 26) & 0xFFF);
                    int z = (int)(enc_pos & 0x3FFFFFF);
                    if (x >= 0x2000000) x -= 0x4000000;
                    if (y >= 0x800) y -= 0x1000;
                    if (z >= 0x2000000) z -= 0x4000000;

                    if (status == 2) { // Finished digging
                        WorldGenerator::setBlock(x, y, z, 0);
                        sendBlockChange(client_socket, x, y, z, 0);
                        log_msg("Block broken at: " + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z));
                    }
                }
                else if (pkt_id == 0x1F) { // Player Block Placement (0x1F) - Pasang Block
                    uint64_t enc_pos = 0;
                    for(int i = 7; i >= 0; i--) enc_pos = (enc_pos << 8) | (uint8_t)*cursor++;
                    
                    int x = (int)(enc_pos >> 38);
                    int y = (int)((enc_pos >> 26) & 0xFFF);
                    int z = (int)(enc_pos & 0x3FFFFFF);
                    if (x >= 0x2000000) x -= 0x4000000;
                    if (y >= 0x800) y -= 0x1000;
                    if (z >= 0x2000000) z -= 0x4000000;

                    int32_t face = readVarInt(cursor, packet_end);
                    int32_t hand = readVarInt(cursor, packet_end);

                    if (face == 0) y--;
                    else if (face == 1) y++;
                    else if (face == 2) z--;
                    else if (face == 3) z++;
                    else if (face == 4) x--;
                    else if (face == 5) x++;

                    int32_t placed_block_id = 1; // Stone
                    WorldGenerator::setBlock(x, y, z, placed_block_id);
                    sendBlockChange(client_socket, x, y, z, placed_block_id);
                    log_msg("Block placed at: " + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z));
                }
                else if (pkt_id == 0x07) { // Click Window (0x07)
                    uint8_t window_id = *cursor++;
                    int16_t slot_idx; std::memcpy(&slot_idx, cursor, 2); cursor += 2;
                    slot_idx = ((slot_idx >> 8) & 0xFF) | ((slot_idx & 0xFF) << 8);

                    uint8_t button = *cursor++;
                    int16_t action_number; std::memcpy(&action_number, cursor, 2); cursor += 2;
                    action_number = ((action_number >> 8) & 0xFF) | ((action_number & 0xFF) << 8);

                    int32_t mode = readVarInt(cursor, packet_end);

                    bool item_present = (*cursor++ != 0);
                    if (item_present && cursor + 5 <= packet_end) {
                        int16_t item_id = (cursor[0] << 8) | cursor[1]; cursor += 2;
                        uint8_t count = *cursor++;
                        int16_t damage = (cursor[0] << 8) | cursor[1]; cursor += 2;
                        
                        if (window_id == 0 && slot_idx >= 0 && slot_idx < 46) {
                            session->inventory.slots[slot_idx] = {true, item_id, count, damage};
                        }
                    }
                    log_msg("Click Window [ID: " + std::to_string(window_id) + "] slot: " + std::to_string(slot_idx));
                }
                else if (pkt_id == 0x08) { // Close Window (0x08)
                    uint8_t window_id = *cursor++;
                    log_msg("Window closed [ID: " + std::to_string(window_id) + "]");
                }
            }

            cursor = packet_end;
        }
    }

    closesocket(client_socket);
    
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        for (auto it = clients.begin(); it != clients.end(); ++it) {
            if ((*it)->socket == client_socket) {
                clients.erase(it);
                break;
            }
        }
    }
    log_msg("Connection closed.");
}

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        log_msg("WSAStartup failed!");
        return 1;
    }

    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        log_msg("Socket creation failed!");
        WSACleanup();
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
        log_msg("Bind failed on port 25565!");
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    if (listen(server_fd, 5) == SOCKET_ERROR) {
        log_msg("Listen failed!");
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    log_msg("tiny-stone 1.12.2 Stoneblock active on port " + std::to_string(PORT));

    std::thread tick_thread(serverTickLoop);
    tick_thread.detach();

    std::thread console_thread(consoleInputThread);
    console_thread.detach();

    while (server_running) {
        int addrlen = sizeof(address);
        SOCKET client_socket = accept(server_fd, (struct sockaddr*)&address, &addrlen);
        if (client_socket == INVALID_SOCKET) continue;

        log_msg("Client connected.");
        std::thread client_thread(handleClient, client_socket);
        client_thread.detach();
    }

    closesocket(server_fd);
    WSACleanup();
    return 0;
}