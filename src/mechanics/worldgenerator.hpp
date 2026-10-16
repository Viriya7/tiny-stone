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
    inline static std::map<int64_t, int32_t> g_custom_block_states;

    static int64_t encodeBlockPos(int x, int y, int z) {
        return ((x & 0x3FFFFFFLL) << 38) | ((y & 0xFFFLL) << 26) | (z & 0x3FFFFFFLL);
    }

public:
    static void setBlock(int x, int y, int z, int32_t block_id) {
        int64_t pos_key = encodeBlockPos(x, y, z);
        g_custom_block_states[pos_key] = block_id;

        int chunkX = x >> 4;
        int chunkZ = z >> 4;

        std::string filepath = "./world_data/chunk_" + std::to_string(chunkX) + "_" + std::to_string(chunkZ) + ".dat";
        if (fs::exists(filepath)) {
            fs::remove(filepath);
        }
    }

    static int32_t getBlock(int x, int y, int z) {
        int64_t pos_key = encodeBlockPos(x, y, z);
        if (g_custom_block_states.find(pos_key) != g_custom_block_states.end()) {
            return g_custom_block_states[pos_key];
        }

        if (y == 0) return 7;

        int spawnX = 0, spawnY = 64, spawnZ = 0;
        double radius = 3.7;

        if (y >= spawnY) {
            double distSq = std::pow(x - spawnX, 2) + std::pow(y - spawnY, 2) + std::pow(z - spawnZ, 2);
            if (distSq <= (radius * radius)) {
                return 0;
            }
        }

        if (y < 128) {
            return 1;
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

        auto push_int = [&](int32_t val) {
            payload.push_back((val >> 24) & 0xFF);
            payload.push_back((val >> 16) & 0xFF);
            payload.push_back((val >> 8) & 0xFF);
            payload.push_back(val & 0xFF);
        };
        push_int(chunkX);
        push_int(chunkZ);

        payload.push_back(0x01);
        appendVarInt(payload, 255);

        std::string chunk_data_buffer = "";

        for (int sec = 0; sec < 8; sec++) {
            int32_t raw_blocks[4096];
            int idx = 0;
            for (int y = 0; y < 16; y++) {
                for (int z = 0; z < 16; z++) {
                    for (int x = 0; x < 16; x++) {
                        int globalX = (chunkX * 16) + x;
                        int globalY = (sec * 16) + y;
                        int globalZ = (chunkZ * 16) + z;
                        raw_blocks[idx++] = getBlock(globalX, globalY, globalZ);
                    }
                }
            }

            std::vector<int32_t> palette;
            palette.push_back(0);
            for (int i = 0; i < 4096; i++) {
                int32_t b = raw_blocks[i];
                if (std::find(palette.begin(), palette.end(), b) == palette.end()) {
                    palette.push_back(b);
                }
            }

            int bitsPerBlock = 4;
            if (palette.size() > 16) bitsPerBlock = 5;
            if (palette.size() > 32) bitsPerBlock = 6;
            if (palette.size() > 64) bitsPerBlock = 7;
            if (palette.size() > 128) bitsPerBlock = 8;
            if (bitsPerBlock > 8) bitsPerBlock = 8;

            chunk_data_buffer.push_back((uint8_t)bitsPerBlock);

            appendVarInt(chunk_data_buffer, palette.size());
            for (int32_t b : palette) {
                appendVarInt(chunk_data_buffer, b << 4);
            }

            uint8_t block_indices[4096];
            for (int i = 0; i < 4096; i++) {
                int32_t b = raw_blocks[i];
                auto it = std::find(palette.begin(), palette.end(), b);
                block_indices[i] = (it != palette.end()) ? (uint8_t)std::distance(palette.begin(), it) : 0;
            }

            int itemsPerLong = 64 / bitsPerBlock;
            int numLongs = (4096 + itemsPerLong - 1) / itemsPerLong;

            appendVarInt(chunk_data_buffer, numLongs);

            for (int l = 0; l < numLongs; l++) {
                uint64_t long_val = 0;
                for (int i = 0; i < itemsPerLong; i++) {
                    int blockIdx = l * itemsPerLong + i;
                    if (blockIdx < 4096) {
                        uint64_t val = block_indices[blockIdx] & ((1ULL << bitsPerBlock) - 1);
                        long_val |= (val << (i * bitsPerBlock));
                    }
                }
                for (int i = 7; i >= 0; i--) {
                    chunk_data_buffer.push_back((long_val >> (i * 8)) & 0xFF);
                }
            }

            std::string block_light(2048, (char)0x00);
            chunk_data_buffer.append(block_light);

            std::string sky_light(2048, (char)0xFF);
            chunk_data_buffer.append(sky_light);
        }

        std::string biomes(256, (char)1); 
        chunk_data_buffer.append(biomes);

        appendVarInt(payload, chunk_data_buffer.length());
        payload.append(chunk_data_buffer);

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