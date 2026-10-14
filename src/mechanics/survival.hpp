#ifndef SURVIVAL_HPP
#define SURVIVAL_HPP

#include <cstdint>
#include <winsock2.h>
#include <cstring>
#include <string>
#include <cmath>
#include <algorithm>

struct PlayerState {
    float health = 20.0f;
    int food_level = 20;
    float food_saturation = 5.0f;
    float food_exhaustion_level = 0.0f;
    int food_timer = 0;
    
    float thirst = 100.0f;
    int thirst_timer = 0;

    double x = 0.5, y = 64.0, z = 0.5;
    bool on_ground = true;
    double highest_y = 64.0;
    bool boss_bar_initialized = false;
};

class SurvivalManager {
private:
    static void appendVarInt(std::string& dest, int32_t val) {
        do {
            uint8_t temp = val & 0b01111111;
            val >>= 7;
            if (val != 0) temp |= 0b10000000;
            dest.push_back(temp);
        } while (val != 0);
    }

    static void appendFloat(std::string& dest, float val) {
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&val);
        for (int i = 3; i >= 0; --i) {
            dest.push_back(ptr[i]);
        }
    }

    static void sendPacketRaw(SOCKET sock, int32_t packet_id, const std::string& data) {
        std::string temp_id_varint = "";
        appendVarInt(temp_id_varint, packet_id);
        int32_t total_length = temp_id_varint.length() + data.length();
        std::string packet = "";
        appendVarInt(packet, total_length);
        packet.append(temp_id_varint);
        packet.append(data);
        send(sock, packet.c_str(), packet.length(), 0);
    }

public:
    static void updateIndicators(SOCKET sock, PlayerState& player) {
        std::string payload = "";
        uint32_t b_health; 
        std::memcpy(&b_health, &player.health, 4);
        for(int i = 3; i >= 0; i--) payload.push_back((b_health >> (i * 8)) & 0xFF);
        appendVarInt(payload, player.food_level);
        uint32_t b_sat; 
        std::memcpy(&b_sat, &player.food_saturation, 4);
        for(int i = 3; i >= 0; i--) payload.push_back((b_sat >> (i * 8)) & 0xFF);
        sendPacketRaw(sock, 0x41, payload);
    }

    static void updateThirstDisplay(SOCKET sock, PlayerState& player) {
        std::string json_text = "{\"text\":\"Thirst\",\"color\":\"aqua\",\"bold\":true}";
        
        std::string uuid = "\x12\x34\x56\x78\x90\xab\xcd\xef\x12\x34\x56\x78\x90\xab\xcd\xef";
        float health_ratio = std::clamp(player.thirst / 100.0f, 0.0f, 1.0f);

        if (!player.boss_bar_initialized) {
            std::string payload = "";
            payload.append(uuid);
            appendVarInt(payload, 0);

            appendVarInt(payload, json_text.length());
            payload.append(json_text);

            appendFloat(payload, health_ratio);

            appendVarInt(payload, 1);
            appendVarInt(payload, 0);
            payload.push_back(0);

            sendPacketRaw(sock, 0x0C, payload);
            player.boss_bar_initialized = true;
        } else {
            std::string payload_health = "";
            payload_health.append(uuid);
            appendVarInt(payload_health, 2);
            appendFloat(payload_health, health_ratio);
            sendPacketRaw(sock, 0x0C, payload_health);

            std::string payload_title = "";
            payload_title.append(uuid);
            appendVarInt(payload_title, 3);
            appendVarInt(payload_title, json_text.length());
            payload_title.append(json_text);
            sendPacketRaw(sock, 0x0C, payload_title);
        }
    }

    static void processMovement(SOCKET sock, PlayerState& player, double newX, double newY, double newZ, bool onGround) {
        double dx = newX - player.x;
        double dy = newY - player.y;
        double dz = newZ - player.z;
        double total_dist = std::sqrt(dx * dx + dy * dy + dz * dz);

        if (onGround) {
            if (!player.on_ground) {
                double fall_distance = player.highest_y - newY;
                if (fall_distance > 3.0) {
                    float damage = (float)(fall_distance - 3.0);
                    player.health = std::max(0.0f, player.health - damage);
                    updateIndicators(sock, player);
                }
                player.highest_y = newY;
            } else {
                player.highest_y = newY;
            }
        } else {
            if (newY > player.highest_y) {
                player.highest_y = newY;
            }
        }

        if (total_dist > 0.01) {
            float exhaustion_rate = 0.01f;
            player.food_exhaustion_level += exhaustion_rate * (float)total_dist;
            
            if (player.food_exhaustion_level >= 4.0f) {
                player.food_exhaustion_level -= 4.0f;
                if (player.food_saturation > 0.0f) {
                    player.food_saturation = std::max(0.0f, player.food_saturation - 1.0f);
                } else if (player.food_level > 0) {
                    player.food_level--;
                    updateIndicators(sock, player);
                }
            }
        }

        player.x = newX;
        player.y = newY;
        player.z = newZ;
        player.on_ground = onGround;
    }

    static void tickSurvival(SOCKET sock, PlayerState& player) {
        player.food_timer++;
        if (player.food_timer >= 80) {
            player.food_timer = 0;
            if (player.food_level >= 18 && player.health < 20.0f && player.health > 0.0f) {
                player.health = std::min(20.0f, player.health + 1.0f);
                updateIndicators(sock, player);
            } else if (player.food_level == 0 && player.health > 1.0f) {
                player.health -= 1.0f;
                updateIndicators(sock, player);
            }
        }

        player.thirst_timer++;
        if (player.thirst_timer >= 1200) {
            player.thirst_timer = 0;
            if (player.thirst > 0.0f) {
                player.thirst = std::max(0.0f, player.thirst - 2.0f);
                updateThirstDisplay(sock, player);
            }
            if (player.thirst <= 0.0f && player.health > 1.0f) {
                player.health -= 1.0f;
                updateIndicators(sock, player);
            }
        }
    }
};

#endif