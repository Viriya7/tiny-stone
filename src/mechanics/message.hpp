#ifndef MESSAGE_HPP
#define MESSAGE_HPP

#include <string>
#include <vector>
#include <memory>
#include <winsock2.h>

class MessageManager {
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
    static void sendChat(SOCKET sock, const std::string& sender_name, const std::string& message) {
        std::string json_text = "[{\"text\":\"웃 \",\"color\":\"green\"},"
                                "{\"text\":\"" + sender_name + "\",\"color\":\"white\"},"
                                "{\"text\":\" : " + message + "\",\"color\":\"white\"}]";
        
        std::string payload = "";
        appendVarInt(payload, json_text.length());
        payload.append(json_text);
        payload.push_back(0x00);

        sendPacketRaw(sock, 0x0F, payload);
    }

    template <typename ClientContainer>
    static void broadcastChat(ClientContainer& clients, const std::string& sender_name, const std::string& message) {
        for (auto& client : clients) {
            if (client->active && client->connection_state == 3) {
                sendChat(client->socket, sender_name, message);
            }
        }
    }
};

#endif