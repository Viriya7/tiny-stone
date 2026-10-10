#ifndef SURVIVAL_HPP
#define SURVIVAL_HPP

#include <cstdint>
#include <winsock2.h>
#include <cstring>
#include <string>
#include <math.h>

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
    // Helper VarInt builder khusus internal survival
    static void appendVarInt(std::string& dest, int32_t val) {
        do {
            uint8_t temp = val & 0b01111111;
            val >>= 7;
            if (val != 0) temp |= 0b10000000;
            dest.push_back(temp);
        } while (val != 0);
    }

    // Helper pengiriman paket generik
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
    // Mengirim paket Update Health & Food ke klien (SPacketUpdateHealth / ID 0x3E di 1.12.2)
    static void updateIndicators(SOCKET sock, PlayerState& player) {
        std::string payload = "";
        
        // 1. Health (Float)
        uint32_t b_health; 
        std::memcpy(&b_health, &player.health, 4);
        for(int i = 3; i >= 0; i--) payload.push_back((b_health >> (i * 8)) & 0xFF);
        
        // 2. Food / Hunger Level (VarInt)
        appendVarInt(payload, player.food_level);
        
        // 3. Food Saturation (Float)
        uint32_t b_sat; 
        std::memcpy(&b_sat, &player.food_saturation, 4);
        for(int i = 3; i >= 0; i--) payload.push_back((b_sat >> (i * 8)) & 0xFF);

        // ID SPacketUpdateHealth di 1.12.2 adalah 0x3E
        sendPacketRaw(sock, 0x41, payload);
    }

    // Memproses pergerakan pemain dan menghitung penambahan exhaustion (kelelahan/lapar)
    static void processMovement(SOCKET sock, PlayerState& player, double newX, double newY, double newZ, bool onGround) {
        double dx = newX - player.x;
        double dy = newY - player.y;
        double dz = newZ - player.z;
        double distance = std::sqrt(dx * dx + dy * dy + dz * dz);

        // Hitung exhaustion berdasarkan jarak jalan (standar mekanik Minecraft)
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

    // Logika tick survival (regenerasi, kelaparan berkala, dll)
    static void tickSurvival(SOCKET sock, PlayerState& player) {
        // Contoh penanganan timer kelaparan/regenerasi per tick
        player.food_timer++;
        if (player.food_timer >= 80) { // Setiap 4 detik
            player.food_timer = 0;
            
            // Regenerasi alami jika hunger penuh (>= 18) dan health belum penuh
            if (player.food_level >= 18 && player.health < 20.0f && player.health > 0.0f) {
                player.health = std::min(20.0f, player.health + 1.0f);
                updateIndicators(sock, player);
            }
            // Kelaparan ekstrem jika food_level == 0
            else if (player.food_level == 0 && player.health > 1.0f) {
                player.health -= 1.0f;
                updateIndicators(sock, player);
            }
        }
    }
};

#endif