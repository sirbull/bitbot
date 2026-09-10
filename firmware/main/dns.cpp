#include "bitbot.h"
#include <array>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <lwip/sockets.h>

namespace bitbot {
static std::atomic<bool> running{false};
static EventGroupHandle_t stopped = nullptr;
// Only answer a single, uncompressed IN/A question. Every read/write is bounded.
static size_t Answer(uint8_t* packet, size_t size, size_t capacity) {
    if (size < 17 || size + 16 > capacity || (packet[2] & 0xf8) || packet[4] != 0 || packet[5] != 1 || packet[6] || packet[7] || packet[8] || packet[9] || packet[10] || packet[11]) return 0;
    size_t end = 12, name_length = 0;
    while (end < size && packet[end]) {
        const size_t length = packet[end];
        if (length > 63 || end + length + 1 >= size) return 0;
        name_length += length + 1;
        if (name_length > 254) return 0;
        end += length + 1;
    }
    if (end + 5 != size || packet[end+1] != 0 || packet[end+2] != 1 || packet[end+3] != 0 || packet[end+4] != 1) return 0;
    const uint8_t tail[] = {0xc0,0x0c,0,1,0,1,0,0,0,30,0,4,192,168,4,1};
    memcpy(packet + size, tail, sizeof(tail));
    packet[2] = 0x81; packet[3] = 0x80; packet[7] = 1;
    return size + sizeof(tail);
}
void StartDns() {
    if (running.exchange(true)) return;
    if (!stopped) stopped = xEventGroupCreate();
    xEventGroupClearBits(stopped, BIT0);
    const auto created = xTaskCreate([](void*) {
        int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in address = {}; address.sin_family = AF_INET; address.sin_port = htons(53); address.sin_addr.s_addr = inet_addr("192.168.4.1");
        timeval timeout = {}; timeout.tv_usec = 200000;
        if (fd >= 0 && bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            while (running.load()) {
                std::array<uint8_t, 528> packet = {};
                sockaddr_in client = {}; socklen_t length = sizeof(client);
                int size = recvfrom(fd, packet.data(), 512, 0, reinterpret_cast<sockaddr*>(&client), &length);
                if (size > 0 && (ntohl(client.sin_addr.s_addr) & 0xffffff00U) == 0xc0a80400U) {
                    size_t answer = Answer(packet.data(), size, packet.size());
                    if (answer) sendto(fd, packet.data(), answer, 0, reinterpret_cast<sockaddr*>(&client), length);
                }
            }
        }
        if (fd >= 0) close(fd);
        running.store(false); xEventGroupSetBits(stopped, BIT0); vTaskDelete(nullptr);
    }, "bitbot_dns", 3072, nullptr, 2, nullptr);
    if (created != pdPASS) { running.store(false); xEventGroupSetBits(stopped, BIT0); }
}
void StopDns() {
    running.store(false);
    if (stopped) xEventGroupWaitBits(stopped, BIT0, pdFALSE, pdTRUE, portMAX_DELAY);
}
}
