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
    
    double x = 0.5, y = 64.0, z = 0.5;
    bool on_ground = true;
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

    static void processMovement(SOCKET sock, PlayerState& player, double newX, double newY, double newZ, bool onGround) {
        double dx = newX - player.x;
        double dy = newY - player.y;
        double dz = newZ - player.z;
        double distance = std::sqrt(dx * dx + dy * dy + dz * dz);

        if (distance > 0.01) {
            player.food_exhaustion_level += 0.01f * (float)distance;
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
            }
            else if (player.food_level == 0 && player.health > 1.0f) {
                player.health -= 1.0f;
                updateIndicators(sock, player);
            }
        }
    }
};

#endif