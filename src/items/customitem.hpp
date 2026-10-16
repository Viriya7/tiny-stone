#ifndef CUSTOM_ITEM_HPP
#define CUSTOM_ITEM_HPP

#include <string>
#include <cstdint>

struct CustomItem {
    int16_t vanilla_item_id;
    int16_t metadata;
    std::string name;
};

class CustomItemManager {
public:
    static CustomItem getStoneChunk() {
        return {13, 0, "Stone Chunk"};
    }

    static CustomItem getPebble() {
        return {318, 0, "Pebble"};
    }
};

inline std::string createSetSlotPayload(int16_t slot_idx, const CustomItem& item, uint8_t count) {
    std::string payload = "";
    payload.push_back(0); 
    
    payload.push_back((slot_idx >> 8) & 0xFF);
    payload.push_back(slot_idx & 0xFF);
    
    if (count == 0 || item.vanilla_item_id <= 0) {
        payload.push_back(0xFF);
        payload.push_back(0xFF);
        return payload;
    }
    
    payload.push_back((item.vanilla_item_id >> 8) & 0xFF);
    payload.push_back(item.vanilla_item_id & 0xFF);
    
    payload.push_back(count);
    
    payload.push_back((item.metadata >> 8) & 0xFF);
    payload.push_back(item.metadata & 0xFF);
    
    if (!item.name.empty()) {
        payload.push_back(0x0A);
        payload.push_back(0x00);
        payload.push_back(0x00);
        
        payload.push_back(0x0A); 
        payload.push_back(0x00);
        payload.push_back(0x07);
        payload.append("display");
        
        payload.push_back(0x08);
        payload.push_back(0x00);
        payload.push_back(0x04);
        payload.append("Name");
        
        std::string name_str = item.name;
        uint16_t name_len = (uint16_t)name_str.length();
        payload.push_back((name_len >> 8) & 0xFF);
        payload.push_back(name_len & 0xFF);
        payload.append(name_str);
        
        payload.push_back(0x00);
        payload.push_back(0x00);
    } else {
        payload.push_back(0x00);
    }
    
    return payload;
}

#endif