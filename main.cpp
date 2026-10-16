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
#include <fstream>
#include <filesystem>
#include <unordered_set>
#include <unordered_map>
#include <tuple>
#include <cmath>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "src/mechanics/worldgenerator.hpp"
#include "src/mechanics/survival.hpp"
#include "src/mechanics/inventory.hpp"
#include "src/mechanics/message.hpp"
#include "src/items/customblock.hpp"
#include "src/items/customitem.hpp"

#pragma comment(lib, "ws2_32.lib")

#define PORT 25565
#define BUFFER_SIZE 8192

namespace fs = std::filesystem;
std::atomic<bool> server_running(true);

struct TupleHash {
    template <class T1, class T2, class T3>
    size_t operator()(const std::tuple<T1, T2, T3>& t) const {
        auto h1 = std::hash<T1>{}(std::get<0>(t));
        auto h2 = std::hash<T2>{}(std::get<1>(t));
        auto h3 = std::hash<T3>{}(std::get<2>(t));
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

class WorldPersistenceManager {
private:
    inline static std::unordered_set<std::tuple<int, int, int>, TupleHash> placed_stones;
    inline static std::unordered_map<std::tuple<int, int, int>, int, TupleHash> modified_blocks;

public:
    static void addCustomBlock(int x, int y, int z) {
        placed_stones.insert({x, y, z});
        saveWorldData();
    }

    static bool removeCustomBlock(int x, int y, int z) {
        auto it = placed_stones.find({x, y, z});
        if (it != placed_stones.end()) {
            placed_stones.erase(it);
            saveWorldData();
            return true;
        }
        return false;
    }

    static bool isCustomBlock(int x, int y, int z) {
        return placed_stones.count({x, y, z}) > 0;
    }

    static void setModifiedBlock(int x, int y, int z, int block_id) {
        modified_blocks[{x, y, z}] = block_id;
        WorldGenerator::setBlock(x, y, z, block_id);
        saveWorldData();
    }

    static int getBlock(int x, int y, int z) {
        auto it = modified_blocks.find({x, y, z});
        if (it != modified_blocks.end()) {
            return it->second;
        }
        return WorldGenerator::getBlock(x, y, z);
    }

    static const std::unordered_map<std::tuple<int, int, int>, int, TupleHash>& getModifiedBlocks() {
        return modified_blocks;
    }

    static void saveWorldData() {
        std::string dir = "./world_data";
        if (!fs::exists(dir)) fs::create_directories(dir);
        
        std::ofstream file(dir + "/world_state.dat", std::ios::binary);
        if (!file) return;

        size_t mod_size = modified_blocks.size();
        file.write(reinterpret_cast<const char*>(&mod_size), sizeof(mod_size));
        for (auto& pair : modified_blocks) {
            int x = std::get<0>(pair.first);
            int y = std::get<1>(pair.first);
            int z = std::get<2>(pair.first);
            int id = pair.second;
            file.write(reinterpret_cast<const char*>(&x), sizeof(x));
            file.write(reinterpret_cast<const char*>(&y), sizeof(y));
            file.write(reinterpret_cast<const char*>(&z), sizeof(z));
            file.write(reinterpret_cast<const char*>(&id), sizeof(id));
        }

        size_t stone_size = placed_stones.size();
        file.write(reinterpret_cast<const char*>(&stone_size), sizeof(stone_size));
        for (auto& pos : placed_stones) {
            int x = std::get<0>(pos);
            int y = std::get<1>(pos);
            int z = std::get<2>(pos);
            file.write(reinterpret_cast<const char*>(&x), sizeof(x));
            file.write(reinterpret_cast<const char*>(&y), sizeof(y));
            file.write(reinterpret_cast<const char*>(&z), sizeof(z));
        }
    }

    static void loadWorldData() {
        std::string filepath = "./world_data/world_state.dat";
        if (!fs::exists(filepath)) return;

        std::ifstream file(filepath, std::ios::binary);
        if (!file) return;

        size_t mod_size;
        file.read(reinterpret_cast<char*>(&mod_size), sizeof(mod_size));
        for (size_t i = 0; i < mod_size; ++i) {
            int x, y, z, id;
            file.read(reinterpret_cast<char*>(&x), sizeof(x));
            file.read(reinterpret_cast<char*>(&y), sizeof(y));
            file.read(reinterpret_cast<char*>(&z), sizeof(z));
            file.read(reinterpret_cast<char*>(&id), sizeof(id));
            modified_blocks[{x, y, z}] = id;
            WorldGenerator::setBlock(x, y, z, id);
        }

        size_t stone_size;
        file.read(reinterpret_cast<char*>(&stone_size), sizeof(stone_size));
        for (size_t i = 0; i < stone_size; ++i) {
            int x, y, z;
            file.read(reinterpret_cast<char*>(&x), sizeof(x));
            file.read(reinterpret_cast<char*>(&y), sizeof(y));
            file.read(reinterpret_cast<char*>(&z), sizeof(z));
            placed_stones.insert({x, y, z});
        }
    }
};

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
    sendPacket(sock, 0x0B, payload);
}

void savePlayerData(const std::string& username, const PlayerState& player, const PlayerInventory& inventory) {
    std::string dir = "./player_data";
    if (!fs::exists(dir)) fs::create_directories(dir);
    std::string filepath = dir + "/" + username + ".dat";
    std::ofstream file(filepath, std::ios::binary);
    if (file) {
        file.write(reinterpret_cast<const char*>(&player.x), sizeof(player.x));
        file.write(reinterpret_cast<const char*>(&player.y), sizeof(player.y));
        file.write(reinterpret_cast<const char*>(&player.z), sizeof(player.z));
        file.write(reinterpret_cast<const char*>(&player.health), sizeof(player.health));
        file.write(reinterpret_cast<const char*>(&player.food_level), sizeof(player.food_level));
        file.write(reinterpret_cast<const char*>(&player.thirst), sizeof(player.thirst));
        
        for (int i = 0; i < 46; ++i) {
            file.write(reinterpret_cast<const char*>(&inventory.slots[i].present), sizeof(inventory.slots[i].present));
            file.write(reinterpret_cast<const char*>(&inventory.slots[i].item_id), sizeof(inventory.slots[i].item_id));
            file.write(reinterpret_cast<const char*>(&inventory.slots[i].count), sizeof(inventory.slots[i].count));
            file.write(reinterpret_cast<const char*>(&inventory.slots[i].metadata), sizeof(inventory.slots[i].metadata));
        }
    }
}

bool loadPlayerData(const std::string& username, PlayerState& player, PlayerInventory& inventory) {
    std::string filepath = "./player_data/" + username + ".dat";
    if (fs::exists(filepath)) {
        std::ifstream file(filepath, std::ios::binary);
        if (file) {
            file.read(reinterpret_cast<char*>(&player.x), sizeof(player.x));
            file.read(reinterpret_cast<char*>(&player.y), sizeof(player.y));
            file.read(reinterpret_cast<char*>(&player.z), sizeof(player.z));
            file.read(reinterpret_cast<char*>(&player.health), sizeof(player.health));
            file.read(reinterpret_cast<char*>(&player.food_level), sizeof(player.food_level));
            file.read(reinterpret_cast<char*>(&player.thirst), sizeof(player.thirst));
            player.highest_y = player.y;

            for (int i = 0; i < 46; ++i) {
                file.read(reinterpret_cast<char*>(&inventory.slots[i].present), sizeof(inventory.slots[i].present));
                file.read(reinterpret_cast<char*>(&inventory.slots[i].item_id), sizeof(inventory.slots[i].item_id));
                file.read(reinterpret_cast<char*>(&inventory.slots[i].count), sizeof(inventory.slots[i].count));
                file.read(reinterpret_cast<char*>(&inventory.slots[i].metadata), sizeof(inventory.slots[i].metadata));
            }
            return true;
        }
    }
    return false;
}

int16_t findEmptySlot(const PlayerInventory& inv) {
    for (int i = 36; i <= 43; ++i) {
        if (!inv.slots[i].present || inv.slots[i].count == 0) {
            return i;
        }
    }
    for (int i = 35; i >= 9; --i) {
        if (!inv.slots[i].present || inv.slots[i].count == 0) {
            return i;
        }
    }
    return -1;
}

struct ClientSession {
    SOCKET socket;
    int connection_state;
    std::string username;
    PlayerState player;
    PlayerInventory inventory;
    bool active;
    bool world_blocks_synced; // Flag untuk memastikan sinkronisasi dunia terkirim setelah client siap
    std::chrono::steady_clock::time_point last_keep_alive;
};

std::vector<std::shared_ptr<ClientSession>> clients;
std::mutex clients_mutex;

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
            if (world_age % 20 == 0) {
                SurvivalManager::updateThirstDisplay(client->socket, client->player);
            }
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
    session->world_blocks_synced = false;
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
            if (session->connection_state == 0) {
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
            else if (session->connection_state == 1) {
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
                else if (pkt_id == 0x01) {
                    std::string payload = "";
                    if (packet_end - cursor >= 8) payload.append(cursor, 8);
                    else { for(int i = 0; i < 8; i++) payload.push_back(0); }
                    sendPacket(client_socket, 0x01, payload);
                    session->active = false;
                }
            } 
            else if (session->connection_state == 2) {
                if (pkt_id == 0x00) {
                    int32_t name_len = readVarInt(cursor, packet_end);
                    std::string pname(cursor, name_len);
                    session->username = pname;
                    log_msg("Player logging in: " + pname);

                    if (!loadPlayerData(pname, session->player, session->inventory)) {
                        session->player.x = 0.5;
                        session->player.y = 64.0;
                        session->player.z = 0.5;
                        session->player.health = 20.0f;
                        session->player.food_level = 20;
                        session->player.thirst = 100.0f;
                        for(int i = 0; i < 46; ++i) {
                            session->inventory.slots[i] = {false, 0, 0, 0};
                        }
                    }

                    std::string login_success = "";
                    std::string uuid_str = "00000000-0000-0000-0000-000000000000";
                    appendVarInt(login_success, uuid_str.length());
                    login_success.append(uuid_str);
                    appendVarInt(login_success, pname.length());
                    login_success.append(pname);
                    sendPacket(client_socket, 0x02, login_success);
                    session->connection_state = 3;
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
                    int64_t spawn_x = (int64_t)session->player.x, spawn_y = (int64_t)session->player.y, spawn_z = (int64_t)session->player.z;
                    int64_t encoded_pos = ((spawn_x & 0x3FFFFFF) << 38) | ((spawn_y & 0xFFF) << 26) | (spawn_z & 0x3FFFFFF);
                    for(int i = 7; i >= 0; i--) spawn_pos_payload.push_back((encoded_pos >> (i * 8)) & 0xFF);
                    sendPacket(client_socket, 0x46, spawn_pos_payload);
                    log_msg("Sending spawn chunks...");
                    int player_chunk_x = (int)session->player.x >> 4;
                    int player_chunk_z = (int)session->player.z >> 4;
                    for (int cx = player_chunk_x - 1; cx <= player_chunk_x + 1; cx++) {
                        for (int cz = player_chunk_z - 1; cz <= player_chunk_z + 1; cz++) {
                            sendPacket(client_socket, 0x20, WorldGenerator::getOrGenerateChunk(cx, cz));
                        }
                    }

                    SurvivalManager::updateIndicators(client_socket, session->player);
                    SurvivalManager::updateThirstDisplay(client_socket, session->player);
                    
                    for (int i = 0; i < 46; ++i) {
                        if (session->inventory.slots[i].present && session->inventory.slots[i].count > 0) {
                            CustomItem dummy_item;
                            dummy_item.vanilla_item_id = session->inventory.slots[i].item_id;
                            dummy_item.metadata = session->inventory.slots[i].metadata;
                            if (dummy_item.vanilla_item_id == 4) dummy_item.name = "Stone Chunk";
                            else if (dummy_item.vanilla_item_id == 318) dummy_item.name = "Pebble";
                            else dummy_item.name = "";

                            sendPacket(client_socket, 0x16, createSetSlotPayload(i, dummy_item, session->inventory.slots[i].count));
                        }
                    }

                    std::string pos = "";
                    auto push_double = [&](double val) {
                        uint64_t b; std::memcpy(&b, &val, 8);
                        for(int i = 7; i >= 0; i--) pos.push_back((b >> (i * 8)) & 0xFF);
                    };
                    auto push_float = [&](float val) {
                        uint32_t b; std::memcpy(&b, &val, 4);
                        for(int i = 3; i >= 0; i--) pos.push_back((b >> (i * 8)) & 0xFF);
                    };
                    push_double(session->player.x);   
                    push_double(session->player.y);  
                    push_double(session->player.z);   
                    push_float(0.0f);   
                    push_float(0.0f);   
                    pos.push_back(0x00); 
                    appendVarInt(pos, 123); 
                    sendPacket(client_socket, 0x2F, pos);
                    log_msg("Player spawned in (" + std::to_string((int)session->player.x) + " " + std::to_string((int)session->player.y) + " " + std::to_string((int)session->player.z) + ")");
                }
            }
            else if (session->connection_state == 3) {
                if (pkt_id == 0x00) {
                    int32_t teleport_id = readVarInt(cursor, packet_end);
                }
                else if (pkt_id == 0x0B) {
                }
                else if (pkt_id == 0x0C || pkt_id == 0x0D || pkt_id == 0x0E) {
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
                    
                    // Sinkronisasi dunia/balok dimodifikasi setelah klien pertama kali mengirim movement (chunk sudah siap)
                    if (!session->world_blocks_synced) {
                        for (auto& pair : WorldPersistenceManager::getModifiedBlocks()) {
                            int wx = std::get<0>(pair.first);
                            int wy = std::get<1>(pair.first);
                            int wz = std::get<2>(pair.first);
                            int wid = pair.second;
                            sendBlockChange(client_socket, wx, wy, wz, wid);
                        }
                        session->world_blocks_synced = true;
                    }

                    SurvivalManager::processMovement(client_socket, session->player, newX, newY, newZ, onGround);

                    int below_x = (int)std::floor(newX);
                    int below_y = (int)std::floor(newY - 0.01);
                    int below_z = (int)std::floor(newZ);

                    if (WorldPersistenceManager::isCustomBlock(below_x, below_y, below_z)) {
                        WorldPersistenceManager::setModifiedBlock(below_x, below_y, below_z, 3);
                        sendBlockChange(client_socket, below_x, below_y, below_z, 3);
                        WorldPersistenceManager::removeCustomBlock(below_x, below_y, below_z);
                    }
                }
                else if (pkt_id == 0x02) {
                    int32_t msg_len = readVarInt(cursor, packet_end);
                    if (msg_len > 0 && cursor + msg_len <= packet_end) {
                        std::string chat_msg(cursor, msg_len);
                        cursor += msg_len;
                        MessageManager::broadcastChat(clients, session->username, chat_msg);
                        log_msg("[Chat] " + session->username + " : " + chat_msg);
                    }
                }
                else if (pkt_id == 0x14) {
                    int32_t status = readVarInt(cursor, packet_end);
                    
                    uint64_t encoded_pos = 0;
                    for(int i = 7; i >= 0; i--) encoded_pos = (encoded_pos << 8) | (uint8_t)*cursor++;
                    int x = (int)(encoded_pos >> 38);
                    int y = (int)((encoded_pos >> 26) & 0xFFF);
                    int z = (int)(encoded_pos & 0x3FFFFFF);
                    if (x >= 0x2000000) x -= 0x4000000;
                    if (y >= 0x800) y -= 0x1000;
                    if (z >= 0x2000000) z -= 0x4000000;
                    
                    int32_t face = readVarInt(cursor, packet_end);

                    if (status == 2) {
                        int target_block = WorldPersistenceManager::getBlock(x, y, z);
                        
                        int vanilla_stone_id = CustomBlockManager::getVanillaBlockId(CustomBlockType::VANILLA_STONE);
                        int stone_chunk_block_id = CustomBlockManager::getVanillaBlockId(CustomBlockType::STONE_CHUNK_BLOCK);

                        if (target_block == vanilla_stone_id) {
                            WorldPersistenceManager::setModifiedBlock(x, y, z, stone_chunk_block_id);
                            sendBlockChange(client_socket, x, y, z, stone_chunk_block_id);

                            CustomItem item = CustomItemManager::getStoneChunk();
                            int16_t slot = findEmptySlot(session->inventory);
                            if (slot != -1) {
                                session->inventory.slots[slot] = {true, item.vanilla_item_id, 3, item.metadata};
                                sendPacket(client_socket, 0x16, createSetSlotPayload(slot, item, 3));
                            }
                        }
                        else if (target_block == stone_chunk_block_id) {
                            WorldPersistenceManager::setModifiedBlock(x, y, z, 0);
                            sendBlockChange(client_socket, x, y, z, 0);

                            CustomItem item = CustomItemManager::getPebble();
                            int16_t slot = findEmptySlot(session->inventory);
                            if (slot != -1) {
                                session->inventory.slots[slot] = {true, item.vanilla_item_id, 4, item.metadata};
                                sendPacket(client_socket, 0x16, createSetSlotPayload(slot, item, 4));
                            }
                        }
                    }
                }
                else if (pkt_id == 0x1F) {
                    uint64_t encoded_pos = 0;
                    for(int i = 7; i >= 0; i--) encoded_pos = (encoded_pos << 8) | (uint8_t)*cursor++;
                    int x = (int)(encoded_pos >> 38);
                    int y = (int)((encoded_pos >> 26) & 0xFFF);
                    int z = (int)(encoded_pos & 0x3FFFFFF);
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

                    int16_t found_slot = -1;
                    for (int i = 36; i <= 43; ++i) {
                        if (session->inventory.slots[i].present && session->inventory.slots[i].item_id == 4 && session->inventory.slots[i].count > 0) {
                            found_slot = i;
                            break;
                        }
                    }
                    if (found_slot == -1) {
                        for (int i = 9; i <= 35; ++i) {
                            if (session->inventory.slots[i].present && session->inventory.slots[i].item_id == 4 && session->inventory.slots[i].count > 0) {
                                found_slot = i;
                                break;
                            }
                        }
                    }

                    if (found_slot != -1) {
                        session->inventory.slots[found_slot].count--;
                        CustomItem item = CustomItemManager::getStoneChunk();
                        if (session->inventory.slots[found_slot].count <= 0) {
                            session->inventory.slots[found_slot] = {false, 0, 0, 0};
                            sendPacket(client_socket, 0x16, createSetSlotPayload(found_slot, item, 0));
                        } else {
                            sendPacket(client_socket, 0x16, createSetSlotPayload(found_slot, item, session->inventory.slots[found_slot].count));
                        }

                        int stone_chunk_block_id = CustomBlockManager::getVanillaBlockId(CustomBlockType::STONE_CHUNK_BLOCK);
                        WorldPersistenceManager::setModifiedBlock(x, y, z, stone_chunk_block_id);
                        sendBlockChange(client_socket, x, y, z, stone_chunk_block_id);
                        WorldPersistenceManager::addCustomBlock(x, y, z);
                    }
                }
                else if (pkt_id == 0x07) {
                    uint8_t window_id = *cursor++;
                    int16_t slot_idx; std::memcpy(&slot_idx, cursor, 2); cursor += 2;
                    slot_idx = ((slot_idx >> 8) & 0xFF) | ((slot_idx & 0xFF) << 8);
                    uint8_t button = *cursor++;
                    int16_t action_number; std::memcpy(&action_number, cursor, 2); cursor += 2;
                    action_number = ((action_number >> 8) & 0xFF) | ((action_number & 0xFF) << 8);
                    int32_t mode = readVarInt(cursor, packet_end);

                    if (cursor + 2 <= packet_end) {
                        int16_t item_id; std::memcpy(&item_id, cursor, 2); cursor += 2;
                        item_id = ((item_id >> 8) & 0xFF) | ((item_id & 0xFF) << 8);

                        if (window_id == 0 && slot_idx >= 0 && slot_idx < 46) {
                            if (item_id == -1 || item_id == 0) {
                                session->inventory.slots[slot_idx] = {false, 0, 0, 0};
                            } else {
                                if (cursor + 3 <= packet_end) {
                                    uint8_t count = *cursor++;
                                    int16_t damage; std::memcpy(&damage, cursor, 2); cursor += 2;
                                    damage = ((damage >> 8) & 0xFF) | ((damage & 0xFF) << 8);
                                    session->inventory.slots[slot_idx] = {true, item_id, count, damage};
                                }
                            }
                        }
                    }
                }
                else if (pkt_id == 0x08) {
                    uint8_t window_id = *cursor++;
                }
            }
            cursor = packet_end;
        }
    }

    if (!session->username.empty()) {
        savePlayerData(session->username, session->player, session->inventory);
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
    WorldPersistenceManager::loadWorldData();
    log_msg("World state loaded from disk.");

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