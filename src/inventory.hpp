#ifndef INVENTORY_HPP
#define INVENTORY_HPP

#include <cstdint>

struct ItemStack {
    bool present = false;
    int16_t item_id = 0;
    uint8_t count = 0;
    int16_t damage = 0;
};

class PlayerInventory {
public:
    // Standar Minecraft 1.12.2 Inventory Slots (0 - 45)
    ItemStack slots[46];

    PlayerInventory() {
        for (int i = 0; i < 46; i++) {
            slots[i] = {false, 0, 0, 0};
        }
    }

    ItemStack& getSlot(int slot_index) {
        if (slot_index >= 0 && slot_index < 46) {
            return slots[slot_index];
        }
        static ItemStack empty_slot{false, 0, 0, 0};
        return empty_slot;
    }

    void setSlot(int slot_index, ItemStack item) {
        if (slot_index >= 0 && slot_index < 46) {
            slots[slot_index] = item;
        }
    }
};

#endif