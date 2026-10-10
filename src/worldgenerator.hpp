#ifndef WORLDGENERATOR_HPP
#define WORLDGENERATOR_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <map>

namespace fs = std::filesystem;

class WorldGenerator {
private:
    // Peta global untuk menyimpan modifikasi blok (Breaking & Placing)
    inline static std::map<int64_t, int32_t> g_custom_block_states;

    static int64_t encodeBlockPos(int x, int y, int z) {
        return ((x & 0x3FFFFFFLL) << 38) | ((y & 0xFFFLL) << 26) | (z & 0x3FFFFFFLL);
    }

public:
    // Mengatur blok baru (Hancur / Pasang) dan menghapus cache file .dat agar chunk di-generate ulang
    static void setBlock(int x, int y, int z, int32_t block_id) {
        int64_t pos_key = encodeBlockPos(x, y, z);
        g_custom_block_states[pos_key] = block_id;

        // Tentukan koordinat chunk berdasarkan posisi blok
        int chunkX = x >> 4;
        int chunkZ = z >> 4;

        // Hapus file cache .dat chunk tersebut agar server membuat ulang dengan data terbaru
        std::string filepath = "./world_data/chunk_" + std::to_string(chunkX) + "_" + std::to_string(chunkZ) + ".dat";
        if (fs::exists(filepath)) {
            fs::remove(filepath);
        }
    }

    // Mendapatkan ID blok pada koordinat tertentu (memeriksa modifikasi kustom terlebih dahulu)
    static int32_t getBlock(int x, int y, int z) {
        int64_t pos_key = encodeBlockPos(x, y, z);
        if (g_custom_block_states.find(pos_key) != g_custom_block_states.end()) {
            return g_custom_block_states[pos_key];
        }

        // Fallback ke generator prosedural default (Kubah & Stone)
        if (y == 0) return 7; // Bedrock

        int spawnX = 0, spawnY = 64, spawnZ = 0;
        double radius = 3.7;

        if (y >= spawnY) {
            double distSq = std::pow(x - spawnX, 2) + std::pow(y - spawnY, 2) + std::pow(z - spawnZ, 2);
            if (distSq <= (radius * radius)) {
                return 0; // Air
            }
        }

        if (y < 128) {
            return 1; // Stone
        }

        return 0;
    }

    static uint8_t getTerrainAt(int x, int y, int z) {
        int32_t block = getBlock(x, y, z);
        return (block > 255) ? 1 : (uint8_t)block;
    }

    static void appendVarInt(std::string& dest, int32_t val) {
        do {
            uint8_t temp = val & 0b01111111;
            val >>= 7;
            if (val != 0) temp |= 0b10000000;
            dest.push_back(temp);
        } while (val != 0);
    }

    static std::string computeChunk(int32_t chunkX, int32_t chunkZ) {
        std::string payload = "";

        // Chunk X & Z (Int32 Big Endian)
        auto push_int = [&](int32_t val) {
            payload.push_back((val >> 24) & 0xFF);
            payload.push_back((val >> 16) & 0xFF);
            payload.push_back((val >> 8) & 0xFF);
            payload.push_back(val & 0xFF);
        };
        push_int(chunkX);
        push_int(chunkZ);

        payload.push_back(0x01); // Ground-Up Continuous (true)
        appendVarInt(payload, 255); // Bitmask 8 Section Pertama (Y=0..127)

        std::string chunk_data_buffer = "";

        // Generate 8 Section (Y=0 sampai 127)
        for (int sec = 0; sec < 8; sec++) {
            chunk_data_buffer.push_back(4); // Bits Per Block = 4

            // Palette Definition
            appendVarInt(chunk_data_buffer, 3);   // Palette Length = 3
            appendVarInt(chunk_data_buffer, 0);   // Index 0: Air (0:0)
            appendVarInt(chunk_data_buffer, 16);  // Index 1: Stone (1:0)
            appendVarInt(chunk_data_buffer, 112); // Index 2: Bedrock (7:0)

            appendVarInt(chunk_data_buffer, 256); // Data Array Length

            uint8_t block_indices[4096];
            int idx = 0;

            // Loop ordering: Y -> Z -> X
            for (int y = 0; y < 16; y++) {
                for (int z = 0; z < 16; z++) {
                    for (int x = 0; x < 16; x++) {
                        int globalX = (chunkX * 16) + x;
                        int globalY = (sec * 16) + y;
                        int globalZ = (chunkZ * 16) + z;

                        // Ambil ID blok dengan memperhitungkan modifikasi breaking/placing
                        int32_t current_block = getBlock(globalX, globalY, globalZ);
                        
                        uint8_t p_index = 0;
                        if (current_block == 1) p_index = 1;      // Stone
                        else if (current_block == 7) p_index = 2; // Bedrock
                        else p_index = 0;                         // Air

                        block_indices[idx++] = p_index;
                    }
                }
            }

            // Pack indices into 64-bit Longs (16 blocks per long)
            for (int l = 0; l < 256; l++) {
                uint64_t long_val = 0;
                for (int i = 0; i < 16; i++) {
                    uint8_t val = block_indices[l * 16 + i] & 0x0F;
                    long_val |= ((uint64_t)val << (i * 4));
                }
                // Write Long Big-Endian
                for (int i = 7; i >= 0; i--) {
                    chunk_data_buffer.push_back((long_val >> (i * 8)) & 0xFF);
                }
            }

            // Block Light
            std::string block_light(2048, (char)0x00);
            chunk_data_buffer.append(block_light);

            // Sky Light
            std::string sky_light(2048, (char)0xFF);
            chunk_data_buffer.append(sky_light);
        }

        // Biomes Data (256 bytes, Plains = 1)
        std::string biomes(256, (char)1); 
        chunk_data_buffer.append(biomes);

        // Append Data Length & Data Buffer
        appendVarInt(payload, chunk_data_buffer.length());
        payload.append(chunk_data_buffer);

        // Block Entities Count = 0
        appendVarInt(payload, 0); 

        return payload;
    }

    static std::string getOrGenerateChunk(int32_t chunkX, int32_t chunkZ) {
        std::string dir = "./world_data";
        if (!fs::exists(dir)) fs::create_directories(dir);

        std::string filepath = dir + "/chunk_" + std::to_string(chunkX) + "_" + std::to_string(chunkZ) + ".dat";

        if (fs::exists(filepath)) {
            std::ifstream file(filepath, std::ios::binary);
            if (file) {
                return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            }
        }

        std::string chunk_data = computeChunk(chunkX, chunkZ);
        std::ofstream file(filepath, std::ios::binary);
        if (file) {
            file.write(chunk_data.data(), chunk_data.size());
        }
        return chunk_data;
    }
};

#endif