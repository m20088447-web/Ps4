// OpenOrbis' libc headers do not declare nanosleep(), but libc++'s threading
// support (pulled in by <algorithm>/<memory>) references it. Declare it first.
#include <time.h>
extern "C" int nanosleep(const struct timespec *, struct timespec *);

#include <SDL.h>

#if __has_include(<orbis/Net.h>)
#include <orbis/Net.h>
#else
extern "C" int sceNetInit(void);
#endif
#include <orbis/NetCtl.h>
#include <orbis/Pad.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>

#include "avplayer_abi.h"
#include "ds4_controls.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

const int kWidth = 1920;
const int kHeight = 1080;
const uint16_t kDiscoveryPort = 39394;
const uint16_t kHttpPort = 8088;
const char kDiscoveryMessage[] = "PHONECAST_HLS/1";

enum Page {
    PAGE_HOME,
    PAGE_SETTINGS,
    PAGE_RECEIVER,
    PAGE_LINKS,
    PAGE_PLAYING
};

Page g_page = PAGE_HOME;
SDL_Window *g_window = nullptr;
SDL_Renderer *g_renderer = nullptr;
SDL_Texture *g_videoTexture = nullptr;
int32_t g_padHandle = -1;
bool g_padConnected = false;
uint32_t g_lastPadOpenAttempt = 0;
Ds4State g_ds4;
SceAvPlayerHandle g_player = nullptr;
int g_discoverySocket = -1;
int g_selected = 0;
int g_videoWidth = 0;
int g_videoHeight = 0;
int g_frames = 0;
int g_reportedFps = 0;
uint32_t g_fpsWindowStart = 0;
bool g_running = true;
std::string g_status = "Select Start Receiver to discover an Android sender.";
std::string g_phoneIp;
std::string g_probe;
std::string g_moduleInfo;
std::string g_detail;
struct LinkEntry {
    std::string name;
    std::string url;
};
std::vector<LinkEntry> g_links;
std::string g_linksSource;
int g_linkSelected = 0;
Page g_returnPage = PAGE_HOME;
SDL_AudioDeviceID g_audioDevice = 0;
int g_audioRate = 0;
int g_audioChannels = 0;
bool g_audioUnavailable = false;
uint32_t g_nextAttemptMs = 0;
std::string g_controllerName = "NO CONTROLLER CONNECTED";
std::vector<uint32_t> g_argb;

struct Rgb {
    uint8_t r, g, b;
};

void setColor(Rgb color) {
    SDL_SetRenderDrawColor(g_renderer, color.r, color.g, color.b, 255);
}

void fillRect(int x, int y, int width, int height, Rgb color) {
    SDL_Rect rect = {x, y, width, height};
    setColor(color);
    SDL_RenderFillRect(g_renderer, &rect);
}

uint8_t fontRow(char input, int row) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -:./?+";
    static const uint8_t font[][7] = {
        {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14},
        {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14},
        {7,2,2,2,18,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14},
        {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
        {17,17,17,17,17,10,4}, {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
        {14,17,17,15,1,1,14},
        {0,0,0,0,0,0,0}, {0,0,0,31,0,0,0}, {0,4,4,0,4,4,0},
        {0,0,0,0,0,12,12}, {1,2,4,8,16,0,0}, {14,17,1,2,4,0,4},
        {4,4,31,4,4,0,0}
    };
    char ch = input;
    if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    const char *found = strchr(alphabet, ch);
    if (!found) found = strchr(alphabet, '?');
    const int index = static_cast<int>(found - alphabet);
    return font[index][row];
}

void drawText(const std::string &text, int x, int y, int scale, Rgb color) {
    int cursor = x;
    for (char ch : text) {
        for (int row = 0; row < 7; ++row) {
            const uint8_t bits = fontRow(ch, row);
            for (int col = 0; col < 5; ++col) {
                if (bits & (1 << (4 - col)))
                    fillRect(cursor + col * scale, y + row * scale, scale, scale, color);
            }
        }
        cursor += 6 * scale;
    }
}

void drawPanel(int x, int y, int width, int height, bool selected) {
    fillRect(x, y, width, height, selected ? Rgb{16, 44, 61} : Rgb{13, 28, 42});
    fillRect(x, y, 5, height, selected ? Rgb{0, 217, 245} : Rgb{28, 62, 82});
}

std::string ps4IpAddress() {
    OrbisNetCtlInfo info;
    memset(&info, 0, sizeof(info));
    if (sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_IP_ADDRESS, &info) < 0 || info.ip_address[0] == '\0')
        return "Network IP unavailable";
    return info.ip_address;
}

void updateWindowTitle(const char *title) {
    if (g_window) SDL_SetWindowTitle(g_window, title);
}

void drawHome() {
    fillRect(0, 0, kWidth, kHeight, Rgb{7, 15, 24});
    fillRect(0, 0, kWidth, 10, Rgb{0, 217, 245});
    drawText("PHONECAST", 112, 98, 5, Rgb{0, 217, 245});
    drawText("ANDROID SCREEN MIRRORING", 118, 150, 2, Rgb{164, 190, 210});
    fillRect(112, 214, 1696, 2, Rgb{24, 54, 72});
    drawText("PS4 RECEIVER", 112, 268, 2, Rgb{232, 242, 250});

    const char *items[] = {"START RECEIVER", "PLAY LINK", "SETTINGS AND CONNECTION INFO", "EXIT"};
    for (int i = 0; i < 4; ++i) {
        const int y = 338 + i * 105;
        drawPanel(112, y, 820, 78, g_selected == i);
        drawText(items[i], 146, y + 27, 2, g_selected == i ? Rgb{245, 251, 255} : Rgb{162, 186, 203});
    }

    drawPanel(1010, 338, 798, 288, false);
    drawText("STATUS", 1044, 370, 2, Rgb{0, 217, 245});
    drawText(g_page == PAGE_RECEIVER ? "LISTENING FOR PHONE" : "READY", 1044, 415, 2, Rgb{232, 242, 250});
    drawText("DISCOVERY UDP PORT 39394", 1044, 466, 1, Rgb{153, 177, 196});
    drawText("HLS VIDEO PORT 8088", 1044, 494, 1, Rgb{153, 177, 196});
    drawText("SAME WI-FI NETWORK REQUIRED", 1044, 546, 1, Rgb{153, 177, 196});

    drawText("X  SELECT", 114, 795, 1, Rgb{128, 155, 177});
    drawText("CIRCLE  BACK", 280, 795, 1, Rgb{128, 155, 177});
    drawText("PS4 IP: " + ps4IpAddress(), 112, 892, 2, Rgb{0, 217, 245});
    drawText("CONTROLLER: " + g_controllerName, 112, 952, 1, Rgb{0, 217, 245});
    drawText("DPAD OR LEFT STICK  SELECT    CROSS  CONFIRM    CIRCLE  BACK", 112, 982, 1, Rgb{108, 133, 154});
}

void drawSettings() {
    fillRect(0, 0, kWidth, kHeight, Rgb{7, 15, 24});
    fillRect(0, 0, kWidth, 10, Rgb{0, 217, 245});
    drawText("SETTINGS AND CONNECTION INFO", 112, 98, 3, Rgb{0, 217, 245});
    drawPanel(112, 210, 1696, 460, false);
    drawText("PS4 IP ADDRESS", 154, 254, 2, Rgb{232, 242, 250});
    drawText(ps4IpAddress(), 154, 301, 2, Rgb{0, 217, 245});
    drawText("DISCOVERY: UDP 39394 BROADCAST", 154, 386, 2, Rgb{192, 211, 226});
    drawText("STREAM: HLS OVER HTTP PORT 8088", 154, 436, 2, Rgb{192, 211, 226});
    drawText("VIDEO: H.264 VIA PS4 AVPLAYER", 154, 486, 2, Rgb{192, 211, 226});
    drawText("AUDIO: DECODED BY AVPLAYER, PLAYED VIA SDL", 154, 536, 2, Rgb{192, 211, 226});
    drawText("CHOOSE RESOLUTION AND FPS IN THE ANDROID APP.", 154, 610, 1, Rgb{125, 153, 175});
    drawText("CONTROLLER: " + g_controllerName, 154, 644, 1, Rgb{0, 217, 245});
    drawText("PRESS CIRCLE TO RETURN", 114, 795, 1, Rgb{128, 155, 177});
}

void drawReceiver() {
    fillRect(0, 0, kWidth, kHeight, Rgb{7, 15, 24});
    fillRect(0, 0, kWidth, 10, Rgb{0, 217, 245});
    drawText("PHONECAST RECEIVER", 112, 110, 4, Rgb{0, 217, 245});
    drawPanel(112, 280, 1696, 390, false);
    drawText("WAITING FOR ANDROID SENDER", 160, 330, 2, Rgb{232, 242, 250});
    drawText("OPEN PHONECAST ON ANDROID AND START SCREEN SHARING.", 160, 400, 1, Rgb{178, 199, 216});
    drawText("ANDROID WILL REQUEST SCREEN CAPTURE PERMISSION.", 160, 434, 1, Rgb{178, 199, 216});
    drawText("PHONECAST USES LOCAL WI-FI; NO USB VIDEO IS ASSUMED.", 160, 468, 1, Rgb{178, 199, 216});
    drawText("PS4 IP: " + ps4IpAddress(), 160, 548, 2, Rgb{0, 217, 245});
    drawText("CIRCLE TO STOP LISTENING", 114, 795, 1, Rgb{128, 155, 177});
    drawText(g_status, 114, 875, 1, Rgb{117, 145, 167});
    if (!g_probe.empty()) drawText(g_probe, 114, 835, 1, Rgb{117, 145, 167});
    if (!g_detail.empty()) drawText("TRIES (URITYPE/SRCTYPE=CODE): " + g_detail.substr(0, 260), 114, 905, 1, Rgb{117, 145, 167});
    drawText(g_moduleInfo, 114, 935, 1, Rgb{117, 145, 167});
}

std::string trimText(const std::string &input) {
    size_t begin = 0;
    size_t end = input.size();
    while (begin < end && (input[begin] == ' ' || input[begin] == '\t' || input[begin] == '\r' || input[begin] == '\n')) ++begin;
    while (end > begin && (input[end - 1] == ' ' || input[end - 1] == '\t' || input[end - 1] == '\r' || input[end - 1] == '\n')) --end;
    return input.substr(begin, end - begin);
}

// Links come from a text file, one per line: "Name | http://..." or just "http://...".
// Lines starting with # are ignored. The first file that exists is used.
void loadLinks() {
    static const char *const kLinkFiles[] = {
        "/mnt/usb0/phonecast_links.txt", "/mnt/usb1/phonecast_links.txt",
        "/mnt/usb0/PhoneCast/links.txt", "/mnt/usb1/PhoneCast/links.txt",
        "/data/phonecast_links.txt", "/data/PhoneCast/links.txt",
    };
    g_links.clear();
    g_linksSource.clear();
    g_linkSelected = 0;
    for (const char *path : kLinkFiles) {
        FILE *file = fopen(path, "r");
        if (!file) continue;
        g_linksSource = path;
        char line[1024];
        while (fgets(line, sizeof(line), file) && g_links.size() < 200) {
            const std::string text = trimText(line);
            if (text.empty() || text[0] == '#') continue;
            LinkEntry entry;
            const size_t bar = text.find('|');
            if (bar != std::string::npos) {
                entry.name = trimText(text.substr(0, bar));
                entry.url = trimText(text.substr(bar + 1));
            } else {
                entry.url = text;
            }
            if (entry.url.compare(0, 7, "http://") != 0 && entry.url.compare(0, 8, "https://") != 0) continue;
            if (entry.name.empty()) entry.name = entry.url;
            g_links.push_back(entry);
        }
        fclose(file);
        if (!g_links.empty()) return;
    }
}

std::string clipText(const std::string &text, size_t maxChars) {
    return text.size() <= maxChars ? text : text.substr(0, maxChars - 2) + "..";
}

void drawLinks() {
    fillRect(0, 0, kWidth, kHeight, Rgb{7, 15, 24});
    fillRect(0, 0, kWidth, 10, Rgb{0, 217, 245});
    drawText("PLAY LINK", 112, 98, 4, Rgb{0, 217, 245});
    if (g_links.empty()) {
        drawPanel(112, 210, 1696, 460, false);
        drawText("NO LINKS FOUND", 154, 254, 2, Rgb{232, 242, 250});
        drawText("CREATE A TEXT FILE NAMED PHONECAST_LINKS.TXT IN THE ROOT OF THE USB DRIVE.", 154, 320, 1, Rgb{178, 199, 216});
        drawText("WRITE ONE LINK PER LINE, FOR EXAMPLE:", 154, 360, 1, Rgb{178, 199, 216});
        drawText("MY CHANNEL | HTTP://SERVER:8000/PATH/STREAM.M3U8", 154, 400, 1, Rgb{0, 217, 245});
        drawText("HTTP://192.168.1.5:8088/LIVE/PHONE/PHONE/1.TS", 154, 434, 1, Rgb{0, 217, 245});
        drawText("LINES STARTING WITH # ARE IGNORED. THEN OPEN THIS PAGE AGAIN.", 154, 484, 1, Rgb{178, 199, 216});
        drawText(g_linksSource.empty() ? "FILE: NOT FOUND ON USB OR /DATA" : "FILE: " + g_linksSource + " (NO VALID LINKS)",
                 154, 560, 1, Rgb{125, 153, 175});
    } else {
        const int visible = 6;
        const int count = static_cast<int>(g_links.size());
        const int first = g_linkSelected >= visible ? g_linkSelected - visible + 1 : 0;
        for (int i = 0; i < visible && first + i < count; ++i) {
            const int index = first + i;
            const int y = 200 + i * 86;
            drawPanel(112, y, 1696, 72, g_linkSelected == index);
            drawText(clipText(g_links[index].name, 70), 146, y + 12, 2, g_linkSelected == index ? Rgb{245, 251, 255} : Rgb{162, 186, 203});
            drawText(clipText(g_links[index].url, 130), 146, y + 46, 1, Rgb{125, 153, 175});
        }
        drawText("LINK " + std::to_string(g_linkSelected + 1) + " OF " + std::to_string(count) + "   FILE: " + g_linksSource,
                 114, 740, 1, Rgb{125, 153, 175});
    }
    drawText("X  PLAY    CIRCLE  BACK    DPAD OR LEFT STICK  SELECT", 114, 795, 1, Rgb{128, 155, 177});
    drawText(g_status, 114, 875, 1, Rgb{117, 145, 167});
    if (!g_detail.empty()) drawText("TRIES (URITYPE/SRCTYPE=CODE): " + clipText(g_detail, 260), 114, 905, 1, Rgb{117, 145, 167});
    drawText(g_moduleInfo, 114, 935, 1, Rgb{117, 145, 167});
    drawText("CONTROLLER: " + g_controllerName, 112, 975, 1, Rgb{0, 217, 245});
}

void stopPlayer() {
    if (g_audioDevice) {
        SDL_CloseAudioDevice(g_audioDevice);
        g_audioDevice = 0;
    }
    g_audioRate = g_audioChannels = 0;
    g_audioUnavailable = false;
    if (g_player) {
        sceAvPlayerStop(g_player);
        sceAvPlayerClose(g_player);
        g_player = nullptr;
    }
    if (g_videoTexture) {
        SDL_DestroyTexture(g_videoTexture);
        g_videoTexture = nullptr;
    }
    g_videoWidth = g_videoHeight = 0;
    g_argb.clear();
}

int alignedAllocate(void **ptr, size_t alignment, size_t size) {
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    size_t power = 1;
    while (power < alignment) power <<= 1;
    return posix_memalign(ptr, power, (size + power - 1) & ~(power - 1));
}

void *avAllocate(void *, uint32_t alignment, uint32_t size) {
    void *ptr = nullptr;
    if (alignedAllocate(&ptr, alignment, size) != 0) return nullptr;
    return ptr;
}

void avDeallocate(void *, void *ptr) {
    free(ptr);
}

void *avAllocateTexture(void *arg, uint32_t alignment, uint32_t size) {
    return avAllocate(arg, alignment, size);
}

void avDeallocateTexture(void *arg, void *ptr) {
    avDeallocate(arg, ptr);
}

// Fetch the playlist ourselves so the screen can show whether the phone's HLS
// server is reachable and what it serves (separate from AvPlayer problems).
std::string probePlaylist(const std::string &phoneIp) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return "PLAYLIST TEST: NO SOCKET";
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(kHttpPort);
    inet_pton(AF_INET, phoneIp.c_str(), &address.sin_addr);

    std::string result;
    int rc = connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address));
    if (rc < 0 && errno != EINPROGRESS) {
        char text[96];
        snprintf(text, sizeof(text), "PLAYLIST TEST: CONNECT FAILED (ERRNO %d)", errno);
        close(fd);
        return text;
    }
    fd_set writeSet;
    FD_ZERO(&writeSet);
    FD_SET(fd, &writeSet);
    timeval timeout{2, 0};
    if (select(fd + 1, nullptr, &writeSet, nullptr, &timeout) <= 0) {
        close(fd);
        return "PLAYLIST TEST: PHONE NOT REACHABLE ON PORT 8088 (TIMEOUT)";
    }
    int socketError = 0;
    socklen_t errorLength = sizeof(socketError);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError, &errorLength);
    if (socketError != 0) {
        char text[96];
        snprintf(text, sizeof(text), "PLAYLIST TEST: CONNECT REFUSED (ERRNO %d)", socketError);
        close(fd);
        return text;
    }
    if (flags >= 0) fcntl(fd, F_SETFL, flags);
    timeval ioTimeout{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &ioTimeout, sizeof(ioTimeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &ioTimeout, sizeof(ioTimeout));

    const std::string request = "GET /phonecast.m3u8 HTTP/1.0\r\nHost: " + phoneIp + "\r\n\r\n";
    send(fd, request.c_str(), request.size(), 0);
    std::string response;
    char buffer[512];
    while (response.size() < 4096) {
        const ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        response.append(buffer, static_cast<size_t>(n));
    }
    close(fd);

    int segments = 0;
    for (size_t pos = response.find("#EXTINF"); pos != std::string::npos; pos = response.find("#EXTINF", pos + 1)) ++segments;
    const bool isPlaylist = response.find("#EXTM3U") != std::string::npos;
    char text[128];
    if (response.empty()) {
        snprintf(text, sizeof(text), "PLAYLIST TEST: CONNECTED BUT EMPTY REPLY");
    } else {
        std::string statusLine = response.substr(0, response.find('\r'));
        for (char &c : statusLine) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
        snprintf(text, sizeof(text), "PLAYLIST TEST: %.40s, %s, %d SEGMENTS", statusLine.c_str(),
                 isPlaylist ? "VALID M3U8" : "NOT M3U8", segments);
    }
    return text;
}

// Opens any http(s) URL with the PS4 AvPlayer and switches to the playing page.
bool openSource(const std::string &sourceUrl, bool looksLikeHls, const std::string &label, Page returnPage) {
    stopPlayer();
    SceAvPlayerInitData init;
    memset(&init, 0, sizeof(init));
    init.memoryReplacement.allocate = avAllocate;
    init.memoryReplacement.deallocate = avDeallocate;
    init.memoryReplacement.allocateTexture = avAllocateTexture;
    init.memoryReplacement.deallocateTexture = avDeallocateTexture;
    init.debugLevel = PHONECAST_AVPLAYER_DEBUG_NONE;
    init.basePriority = 160;
    init.numOutputVideoFrameBuffers = 4;
    init.autoStart = 1;
    init.defaultLanguage = "en";

    g_player = sceAvPlayerInit(&init);
    if (!g_player) {
        g_status = "SceAvPlayer initialization failed.";
        return false;
    }
    // AvPlayer may keep the URL pointer, so give it storage that outlives this call.
    static char url[512];
    snprintf(url, sizeof(url), "%s", sourceUrl.c_str());

    // The exact AddSourceEx argument values differ between documentation and
    // firmware, so try the plain AddSource (if the toolchain stub has it) and
    // then each known uriType/sourceType combination until one is accepted.
    int32_t result = -1;
    int32_t firstResult = -1;
    int usedUriType = -1;
    int usedSourceType = -1;
    bool usedSimple = false;
    int attempts = 0;
    g_detail.clear();
    if (sceAvPlayerAddSource) {
        ++attempts;
        result = sceAvPlayerAddSource(g_player, url);
        usedSimple = result >= 0;
    }
    static const int kUriTypes[] = {0, 1, 2};
    static const int kHlsFirst[] = {PHONECAST_AVPLAYER_SOURCE_HLS, 0, 3, 2, 1, 4, 5, 6, 7, 9};
    static const int kOtherFirst[] = {0, 1, PHONECAST_AVPLAYER_SOURCE_HLS, 2, 3, 4, 5, 6, 7, 9};
    const int *kSourceTypes = looksLikeHls ? kHlsFirst : kOtherFirst;
    for (size_t u = 0; result < 0 && u < sizeof(kUriTypes) / sizeof(kUriTypes[0]); ++u) {
        for (size_t t = 0; result < 0 && t < 10; ++t) {
            SceAvPlayerSourceDetails details;
            memset(&details, 0, sizeof(details));
            details.uri.name = url;
            details.uri.length = static_cast<uint32_t>(strlen(url));
            details.sourceType = kSourceTypes[t];
            ++attempts;
            result = sceAvPlayerAddSourceEx(g_player, kUriTypes[u], &details);
            if (attempts == 1 || (attempts == 2 && sceAvPlayerAddSource)) firstResult = result;
            printf("AddSourceEx uriType=%d sourceType=%d -> 0x%08x\n", kUriTypes[u], kSourceTypes[t],
                   static_cast<unsigned>(result));
            {
                char piece[40];
                snprintf(piece, sizeof(piece), "%d/%d=%X ", kUriTypes[u], kSourceTypes[t],
                         static_cast<unsigned>(result) & 0xFFFFu);
                g_detail += piece;
            }
            if (result >= 0) {
                usedUriType = kUriTypes[u];
                usedSourceType = kSourceTypes[t];
            }
        }
    }
    if (result < 0) {
        char message[160];
        snprintf(message, sizeof(message),
                 "AvPlayer rejected the stream (first 0x%08x, last 0x%08x, %d tries).",
                 static_cast<unsigned>(firstResult), static_cast<unsigned>(result), attempts);
        g_status = message;
        printf("%s\n", message);
        stopPlayer();
        return false;
    }
    char how[64];
    if (usedSimple) snprintf(how, sizeof(how), "AddSource");
    else snprintf(how, sizeof(how), "Ex type %d/%d", usedUriType, usedSourceType);
    g_phoneIp = label;
    g_status = "Opening " + label + " (" + how + ")";
    g_returnPage = returnPage;
    g_page = PAGE_PLAYING;
    g_fpsWindowStart = SDL_GetTicks();
    g_frames = 0;
    return true;
}

bool beginPlayer(const std::string &phoneIp) {
    g_probe = probePlaylist(phoneIp);
    printf("%s\n", g_probe.c_str());
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%u/phonecast.m3u8", phoneIp.c_str(), kHttpPort);
    const bool opened = openSource(url, true, phoneIp, PAGE_HOME);
    if (!opened) g_nextAttemptMs = SDL_GetTicks() + 8000;  // do not hammer the player every second
    return opened;
}

void playLink(const LinkEntry &entry) {
    const bool hls = entry.url.find(".m3u8") != std::string::npos;
    openSource(entry.url, hls, clipText(entry.name, 60), PAGE_LINKS);
}

bool initDiscovery() {
    if (g_discoverySocket >= 0) return true;
    g_discoverySocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_discoverySocket < 0) {
        g_status = "Could not create UDP discovery socket.";
        return false;
    }
    int reuse = 1;
    setsockopt(g_discoverySocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    const int flags = fcntl(g_discoverySocket, F_GETFL, 0);
    if (flags >= 0) fcntl(g_discoverySocket, F_SETFL, flags | O_NONBLOCK);

    sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(kDiscoveryPort);
    if (bind(g_discoverySocket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
        g_status = "Could not bind UDP discovery port 39394.";
        close(g_discoverySocket);
        g_discoverySocket = -1;
        return false;
    }
    return true;
}

void checkForAndroid() {
    if (g_discoverySocket < 0 || g_page != PAGE_RECEIVER) return;
    char message[64];
    sockaddr_in source;
    socklen_t sourceLength = sizeof(source);
    const ssize_t received = recvfrom(
        g_discoverySocket, message, sizeof(message) - 1, 0,
        reinterpret_cast<sockaddr *>(&source), &sourceLength
    );
    if (received <= 0) return;
    message[received] = '\0';
    if (strncmp(message, kDiscoveryMessage, sizeof(kDiscoveryMessage) - 1) != 0) return;

    char ip[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &source.sin_addr, ip, sizeof(ip))) return;
    if (static_cast<int32_t>(SDL_GetTicks() - g_nextAttemptMs) < 0) return;
    g_status = std::string("Found Android sender: ") + ip;
    beginPlayer(ip);
}

void convertNv12ToArgb(const uint8_t *yPlane, const uint8_t *uvPlane, int width, int height, int pitch) {
    if (width <= 0 || height <= 0 || pitch < width) return;
    if (g_videoWidth != width || g_videoHeight != height) {
        g_videoWidth = width;
        g_videoHeight = height;
        g_argb.resize(static_cast<size_t>(width) * height);
        if (g_videoTexture) SDL_DestroyTexture(g_videoTexture);
        g_videoTexture = SDL_CreateTexture(
            g_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height
        );
    }
    for (int y = 0; y < height; ++y) {
        const uint8_t *yRow = yPlane + static_cast<size_t>(y) * pitch;
        const uint8_t *uvRow = uvPlane + static_cast<size_t>(y >> 1) * pitch;
        uint32_t *out = g_argb.data() + static_cast<size_t>(y) * width;
        for (int x = 0; x < width; ++x) {
            const int luma = yRow[x] - 16;
            const int u = static_cast<int>(uvRow[x & ~1]) - 128;
            const int v = static_cast<int>(uvRow[(x & ~1) + 1]) - 128;
            const int c = std::max(0, luma);
            int r = (298 * c + 409 * v + 128) >> 8;
            int g = (298 * c - 100 * u - 208 * v + 128) >> 8;
            int b = (298 * c + 516 * u + 128) >> 8;
            r = std::max(0, std::min(255, r));
            g = std::max(0, std::min(255, g));
            b = std::max(0, std::min(255, b));
            out[x] = 0xFF000000u | (static_cast<uint32_t>(r) << 16) |
                     (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
        }
    }
    if (g_videoTexture)
        SDL_UpdateTexture(g_videoTexture, nullptr, g_argb.data(), width * static_cast<int>(sizeof(uint32_t)));
}

bool openAudio(int rate, int channels) {
    if (g_audioDevice && g_audioRate == rate && g_audioChannels == channels) return true;
    if (g_audioUnavailable) return false;
    if (g_audioDevice) {
        SDL_CloseAudioDevice(g_audioDevice);
        g_audioDevice = 0;
    }
    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        printf("SDL audio init failed: %s\n", SDL_GetError());
        g_audioUnavailable = true;
        return false;
    }
    SDL_AudioSpec want;
    SDL_AudioSpec have;
    memset(&want, 0, sizeof(want));
    memset(&have, 0, sizeof(have));
    want.freq = rate;
    want.format = AUDIO_S16SYS;
    want.channels = static_cast<Uint8>(channels);
    want.samples = 1024;
    g_audioDevice = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (g_audioDevice == 0) {
        printf("SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        g_audioUnavailable = true;
        return false;
    }
    g_audioRate = rate;
    g_audioChannels = channels;
    SDL_PauseAudioDevice(g_audioDevice, 0);
    printf("Audio output opened: %d Hz, %d channels\n", rate, channels);
    return true;
}

// AvPlayer stalls when its decoded audio is never consumed, so always drain it.
void pumpAudio() {
    if (!g_player || !sceAvPlayerGetAudioData) return;
    alignas(16) uint8_t info[512];
    for (int i = 0; i < 8; ++i) {
        memset(info, 0, sizeof(info));
        if (!sceAvPlayerGetAudioData(g_player, info)) break;
        void *data = nullptr;
        uint16_t channels = 0;
        uint32_t rate = 0;
        uint32_t size = 0;
        memcpy(&data, info, sizeof(data));
        memcpy(&channels, info + 24, sizeof(channels));
        memcpy(&rate, info + 28, sizeof(rate));
        memcpy(&size, info + 32, sizeof(size));
        if (!data || size == 0 || channels == 0 || channels > 8 || rate < 8000 || rate > 192000) break;
        if (!openAudio(static_cast<int>(rate), static_cast<int>(channels))) break;
        // Keep delay bounded: drop audio when more than about a second is already queued.
        const uint32_t queued = SDL_GetQueuedAudioSize(g_audioDevice);
        if (queued < rate * channels * 2) SDL_QueueAudio(g_audioDevice, data, size);
    }
}

void updatePlayer() {
    if (!g_player || !sceAvPlayerIsActive(g_player)) return;
    pumpAudio();
    SceAvPlayerFrameInfoEx frame;
    memset(&frame, 0, sizeof(frame));
    sceAvPlayerGetVideoDataEx(g_player, &frame);  // fills pData only when a new frame is ready
    if (!frame.pData) return;
    const int width = static_cast<int>(frame.details.video.width);
    const int height = static_cast<int>(frame.details.video.height);
    int pitch = static_cast<int>(frame.details.video.pitch);
    if (pitch <= 0) pitch = width;
    if (width <= 0 || height <= 0) return;

    const uint8_t *luma = static_cast<const uint8_t *>(frame.pData);
    const uint8_t *chroma = luma + static_cast<size_t>(pitch) * height;
    convertNv12ToArgb(luma, chroma, width, height, pitch);
    ++g_frames;
    const uint32_t now = SDL_GetTicks();
    if (now - g_fpsWindowStart >= 1000) {
        g_reportedFps = g_frames;
        g_frames = 0;
        g_fpsWindowStart = now;
    }
}

void drawVideo() {
    fillRect(0, 0, kWidth, kHeight, Rgb{0, 0, 0});
    if (!g_videoTexture || g_videoWidth <= 0 || g_videoHeight <= 0) {
        drawText("OPENING STREAM", 112, 112, 3, Rgb{0, 217, 245});
        drawText(g_status, 112, 180, 1, Rgb{215, 229, 239});
        const bool active = g_player && sceAvPlayerIsActive(g_player);
        drawText(std::string("AVPLAYER: ") + (active ? "ACTIVE, WAITING FOR VIDEO" : "NOT ACTIVE") +
                 "   SOURCE: " + g_phoneIp, 112, 230, 1, Rgb{153, 177, 196});
        drawText("CIRCLE TO RETURN TO MENU", 112, 980, 1, Rgb{125, 153, 175});
        return;
    }
    const float aspect = static_cast<float>(g_videoWidth) / static_cast<float>(g_videoHeight);
    int w = kWidth;
    int h = static_cast<int>(w / aspect);
    if (h > kHeight) {
        h = kHeight;
        w = static_cast<int>(h * aspect);
    }
    SDL_Rect target = {(kWidth - w) / 2, (kHeight - h) / 2, w, h};
    SDL_RenderCopy(g_renderer, g_videoTexture, nullptr, &target);
    fillRect(36, 34, 348, 50, Rgb{7, 15, 24});
    drawText("PHONECAST  " + std::to_string(g_reportedFps) + " FPS", 52, 50, 1, Rgb{0, 217, 245});
}

void navigate(int direction) {
    if (g_page == PAGE_HOME) {
        g_selected = (g_selected + direction + 4) % 4;
    } else if (g_page == PAGE_LINKS && !g_links.empty()) {
        const int count = static_cast<int>(g_links.size());
        g_linkSelected = (g_linkSelected + direction + count) % count;
    }
}

void activateSelection();

// Native PS4 pad access (libScePad). This is the console's own controller API,
// so the DualShock 4 works without SDL's joystick layer.
void closePad() {
    if (g_padHandle >= 0) scePadClose(g_padHandle);
    g_padHandle = -1;
    g_padConnected = false;
    g_controllerName = "NO CONTROLLER CONNECTED";
}

bool openPad() {
    if (g_padHandle >= 0) return true;
    int32_t userId = 0;
    if (sceUserServiceGetInitialUser(&userId) < 0) {
        printf("sceUserServiceGetInitialUser failed\n");
        return false;
    }
    const int32_t handle = scePadOpen(userId, 0, 0, nullptr);
    if (handle < 0) {
        printf("scePadOpen failed: 0x%08x\n", static_cast<unsigned>(handle));
        return false;
    }
    g_padHandle = handle;
    g_padConnected = false;
    printf("Opened PS4 pad handle %d for user %d\n", static_cast<int>(handle), static_cast<int>(userId));
    return true;
}

void applyControllerAction(Ds4Action action) {
    switch (action) {
        case Ds4Action::Previous:
            navigate(-1);
            break;
        case Ds4Action::Next:
            navigate(1);
            break;
        case Ds4Action::Confirm:
            activateSelection();
            break;
        case Ds4Action::Back:
            if (g_page == PAGE_SETTINGS || g_page == PAGE_LINKS) {
                g_page = PAGE_HOME;
            } else if (g_page == PAGE_RECEIVER) {
                stopPlayer();
                g_page = PAGE_HOME;
            } else if (g_page == PAGE_PLAYING) {
                stopPlayer();
                g_page = g_returnPage;
            }
            break;
        case Ds4Action::Options:
            if (g_page == PAGE_PLAYING) {
                stopPlayer();
                g_page = g_returnPage;
            }
            break;
        case Ds4Action::None:
            break;
    }
}

void pollPad() {
    const uint32_t now = SDL_GetTicks();
    if (g_padHandle < 0) {
        // Retry about once per second in case the user service was not ready.
        if (now - g_lastPadOpenAttempt < 1000) return;
        g_lastPadOpenAttempt = now;
        if (!openPad()) return;
    }

    OrbisPadData data;
    memset(&data, 0, sizeof(data));
    const int32_t result = scePadReadState(g_padHandle, &data);
    if (result < 0 || !data.connected) {
        if (g_padConnected) printf("DualShock 4 disconnected\n");
        g_padConnected = false;
        g_controllerName = "NO CONTROLLER CONNECTED";
        return;
    }

    if (!g_padConnected) {
        g_padConnected = true;
        g_controllerName = "DUALSHOCK 4 CONNECTED";
        // Ignore buttons already held at connect time (e.g. the Cross press
        // that launched the app) so they cannot trigger a phantom Confirm.
        ds4Reset(g_ds4, data.buttons);
        printf("DualShock 4 connected\n");
        return;
    }

    Ds4Action actions[4];
    const int count = ds4Poll(g_ds4, data.buttons, data.leftStick.x, data.leftStick.y, now, actions);
    for (int i = 0; i < count; ++i) applyControllerAction(actions[i]);
}

void activateSelection() {
    if (g_page == PAGE_HOME) {
        if (g_selected == 0) {
            if (initDiscovery()) {
                g_page = PAGE_RECEIVER;
                g_status = "Listening for PHONECAST_HLS/1 advertisements.";
            }
        } else if (g_selected == 1) {
            loadLinks();
            g_status = "Select a link and press X to play it.";
            g_page = PAGE_LINKS;
        } else if (g_selected == 2) {
            g_page = PAGE_SETTINGS;
        } else {
            g_running = false;
        }
    } else if (g_page == PAGE_LINKS) {
        if (!g_links.empty()) {
            g_status = "Opening link...";
            playLink(g_links[g_linkSelected]);
        }
    }
}

void handleInput() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) g_running = false;
        if (event.type == SDL_KEYDOWN) {
            if (event.key.keysym.sym == SDLK_UP) navigate(-1);
            else if (event.key.keysym.sym == SDLK_DOWN) navigate(1);
            else if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_x) activateSelection();
            else if (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_BACKSPACE) {
                if (g_page == PAGE_SETTINGS || g_page == PAGE_LINKS) g_page = PAGE_HOME;
                else if (g_page == PAGE_RECEIVER) {
                    stopPlayer();
                    g_page = PAGE_HOME;
                } else if (g_page == PAGE_PLAYING) {
                    stopPlayer();
                    g_page = g_returnPage;
                } else g_running = false;
            }
        }
    }
    pollPad();
}

} // namespace

int main(int, char **) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    // The AvPlayer streams http:// sources through the console's network modules, so load
    // them first (names are detected at build time; missing names are simply skipped).
    {
        char info[200];
        int written = snprintf(info, sizeof(info), "MODULES");
#ifdef HAVE_SYSMODULE_NET
        written += snprintf(info + written, sizeof(info) - written, " NET %08X", static_cast<unsigned>(sceSysmoduleLoadModule(ORBIS_SYSMODULE_NET)));
#else
        written += snprintf(info + written, sizeof(info) - written, " NET -");
#endif
#ifdef HAVE_SYSMODULE_SSL
        written += snprintf(info + written, sizeof(info) - written, " SSL %08X", static_cast<unsigned>(sceSysmoduleLoadModule(ORBIS_SYSMODULE_SSL)));
#else
        written += snprintf(info + written, sizeof(info) - written, " SSL -");
#endif
#ifdef HAVE_SYSMODULE_HTTP
        written += snprintf(info + written, sizeof(info) - written, " HTTP %08X", static_cast<unsigned>(sceSysmoduleLoadModule(ORBIS_SYSMODULE_HTTP)));
#else
        written += snprintf(info + written, sizeof(info) - written, " HTTP -");
#endif
        written += snprintf(info + written, sizeof(info) - written, " AV %08X", static_cast<unsigned>(sceSysmoduleLoadModule(ORBIS_SYSMODULE_AV_PLAYER)));
        g_moduleInfo = info;
        printf("%s\n", info);
    }
    sceNetInit();
    sceNetCtlInit();

    // Controller input uses the native pad API. The user service must be up
    // first. An "already initialized" result is fine, so errors are ignored.
    sceUserServiceInitialize(nullptr);
    scePadInit();

    // SDL_INIT_JOYSTICK is deliberately not used: SDL's joystick layer would
    // compete with scePadOpen for the same controller.
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    openPad();
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    g_window = SDL_CreateWindow(
        "PhoneCast", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        kWidth, kHeight, SDL_WINDOW_FULLSCREEN
    );
    if (!g_window) {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    g_renderer = SDL_CreateRenderer(g_window, -1, 0);
    if (!g_renderer) {
        printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return 1;
    }
    SDL_RenderSetLogicalSize(g_renderer, kWidth, kHeight);
    while (g_running) {
        handleInput();
        if (g_page == PAGE_RECEIVER) checkForAndroid();
        if (g_page == PAGE_PLAYING) updatePlayer();

        if (g_page == PAGE_HOME) drawHome();
        else if (g_page == PAGE_SETTINGS) drawSettings();
        else if (g_page == PAGE_RECEIVER) drawReceiver();
        else if (g_page == PAGE_LINKS) drawLinks();
        else drawVideo();

        SDL_RenderPresent(g_renderer);
        SDL_Delay(g_page == PAGE_PLAYING ? 8 : 16);
    }

    stopPlayer();
    if (g_discoverySocket >= 0) close(g_discoverySocket);
    closePad();
    if (g_renderer) SDL_DestroyRenderer(g_renderer);
    if (g_window) SDL_DestroyWindow(g_window);
    SDL_Quit();
    sceNetCtlTerm();
    return 0;
}

