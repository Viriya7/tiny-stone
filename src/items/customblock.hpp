#ifndef CUSTOM_BLOCK_HPP
#define CUSTOM_BLOCK_HPP

#include <cstdint>

enum class CustomBlockType {
    VANILLA_STONE,
    STONE_CHUNK_BLOCK
};

class CustomBlockManager {
public:
    static int getVanillaBlockId(CustomBlockType type) {
        switch (type) {
            case CustomBlockType::VANILLA_STONE:
                return 1;
            case CustomBlockType::STONE_CHUNK_BLOCK:
                return 13;
            default:
                return 0;
        }
    }
};

#endif