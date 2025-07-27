#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <algorithm>
#include <functional>
#include <filesystem>
#include <fstream>
#include <string.h>

#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include "serial/serial.h"

int get_weekday(void)
{
    time_t rawtime;
    struct tm *timeinfo;

    time(&rawtime);
    timeinfo = localtime(&rawtime); // 将时间转换为本地时间结构体

    return timeinfo->tm_wday;
}

const uint16_t crctalbeabs[] = {
    0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401,
    0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400
};

uint16_t crc16tablefast(uint8_t *ptr, uint16_t len)
{
    uint16_t crc = 0xffff;
    uint16_t i;
    uint8_t ch;

    for (i = 0; i < len; i++) {
        ch = *ptr++;
        crc = crctalbeabs[(ch ^ crc) & 15] ^ (crc >> 4);
        crc = crctalbeabs[((ch >> 4) ^ crc) & 15] ^ (crc >> 4);
    }

    return crc;
}

int get_current_play_index(const char *dir, int *idx, int max)
{
    char full_path[256];
    int index = 0;

    snprintf(full_path, sizeof(full_path), "%s/%s", dir, "play_idx");
    if(0 == access(full_path, 0)) {
        FILE *fp = fopen(full_path, "r");
        int len = fscanf(fp, "%d",  &index);
        fclose(fp);
    } else {
        FILE *fp = fopen(full_path, "w+");
        fprintf(fp, "%d\n",  0);
        fclose(fp);
        index = 0;
    }

    if(idx) {
        *idx = index;
    }

    return 0;
}

int save_current_play_index(const char *dir, int *idx, int max)
{
    char full_path[256];
    int index = 0;

    snprintf(full_path, sizeof(full_path), "%s/%s", dir, "play_idx");

    FILE *fp = fopen(full_path, "w+");
    fprintf(fp, "%d\n",  *idx);
    fclose(fp);

    return 0;
}

int set_next_play_index(const char *dir, int max)
{
    int idx = 0;
    get_current_play_index(dir, &idx, max);

    idx++;
    if(idx >= max) {
        printf("idx = %d is too big, roll back to 0\n", idx);
        idx = 0;
    }

    save_current_play_index(dir, &idx, max);
    return 0;
}

int set_prev_play_index(const char *dir, int max)
{
    int idx = 0;
    get_current_play_index(dir, &idx, max);
    idx--;
    if(idx < 0) {
        printf("idx = %d is too small , roll back to max-1\n", idx);
        idx = max - 1;
    }
    save_current_play_index(dir, &idx, max);
    return 0;
}

int switch_read(int *sw1, int *sw2, int *sw3)
{
    char rd_buffer[512] = {0};
    uint8_t modbus_request[] = {0x01, 0x04, 0x00, 0x00,0x00, 0x04, 0xF1, 0xC9};
    uint8_t modbus_respond[] = {0x01, 0x04, 0x08, 0x00,0x01, 0x00, 0x00,0x00,0x00,0x00,0x00,0x34,0xCD};
    int baud = 9600;
    const char *port = "/dev/ttyUSB0";

    int week_day = get_weekday();

    serial::Serial my_serial;

    my_serial.setPort(port);
    my_serial.setBaudrate(baud);

    my_serial.setTimeout(serial::Timeout::max(), 1000, 0, 1000, 0);
    my_serial.open();

    if(!my_serial.isOpen()) {
        printf("port %s is open failed, check permission\n", port);
        return 0;
    }

    size_t bytes_wrote = my_serial.write(modbus_request, sizeof(modbus_request));

    memset(modbus_respond, 0x00, sizeof(modbus_respond));

    auto result = my_serial.read(modbus_respond, sizeof(modbus_respond));

    my_serial.close();

    char *ptr = rd_buffer;
    int len = 0;
    for(int i = 0; i < result; i++) {
        len = snprintf(ptr, sizeof(rd_buffer) - (ptr - &rd_buffer[0]), "%02x ", modbus_respond[i]);
        ptr += len;
    }

    if(result <= 2) {
        printf("recv len=%ld is not expoect\n", result);
        return 0;
    }

    uint16_t crc_recv = crc16tablefast(modbus_respond, result -2);
    uint16_t crc_calc = modbus_respond[result -2] + 0x0100*modbus_respond[result -1];
    if(crc_recv != crc_calc) {
        printf("recv=[%s] crc_recv(0x%x) != crc_calc(0x%x)\n", rd_buffer, crc_recv, crc_calc);
        return 0;
    }

    /* this is the expect case */
    if(sw1) {
        *sw1 = modbus_respond[4];
    }
    if(sw2) {
        *sw1 = modbus_respond[6];
    }
    if(sw3) {
        *sw1 = modbus_respond[8];
    }

    //int switch_play = modbus_respond[10];
    return 0;
}

int get_play_event(std::vector<std::string> &vec, std::string &s, const char *dir)
{
    int switch_play = 0;
    int switch_next = 0;
    int switch_prev = 0;

    switch_read(&switch_play, &switch_next, &switch_prev);

    if(!switch_play) {
        printf("switch_play=%d is not on, wait next\n", switch_play);
        return 0;
    }

    int idx = 0;
    int retry = 0;
    do {
        if(get_current_play_index(dir, &idx, vec.size())) {
            printf("get_next_play_index failed\n");
            return 0;
        }

        if(0 == access(vec[idx].c_str(), 0)) {
            break;
        }

        printf("file=%s access failed, try next\n", vec[idx].c_str());
        set_next_play_index(dir, vec.size());
    } while(++retry < 100);

    s = vec[idx];
    return 1;
}

uint64_t get_time_seconds(void)
{
    struct timespec start;

    clock_gettime(CLOCK_MONOTONIC, &start);

    return  start.tv_sec + ((start.tv_nsec) / 1e9);
}

int get_audio_card_status(const char *path, std::string & out_str)
{
    static int close_count = 0;
    std::string str_close = "closed";

    std::ifstream infile;
    infile.open(path, std::ios::in);
    if (!infile.is_open()) {
        std::cout << "读取文件失败" << std::endl;
        return 1;
    }

    std::string s;
    std::vector<std::string> v1;
    while (std::getline(infile, s)) {
        v1.push_back(s);
    }
    infile.close();

    out_str = v1[0];

    if(v1[0] == str_close) {
        /* no audio stream now */
        if(++close_count > 5) {
            return 0;
        }
        return 1;
    }

    /* in playing in this case */
    close_count  = 0;
    return 1;
}

int main(int argc, char *argv[])
{
    int ret = 0;
    int system_ret = 0;
    char cmd[1024] = {0};
    const char * dir = ".";

    uint64_t start, end;

    for(int i = 0; i < argc; i++) {
        if(0 == strcmp("-d", argv[i])) {
            if(i+1 >= argc) continue;
            dir = argv[i+1];
        }
    }

    std::string s;
    std::vector<std::string> vec;
    std::filesystem::directory_iterator list(dir);	        //文件入口容器
    //
    for (auto& it:list) {
        if(0 == strcmp(it.path().filename().c_str(), "play_idx")) {
            continue;
        }
        vec.push_back(it.path().filename());
    }
    std::sort(vec.begin(), vec.end());

    int j = 0;
    for (auto i: vec) {
        printf("[%02d]%s\n", j++, i.c_str());
    }

    int fd[2];

    ret = pipe(fd);

    if(ret==-1) {
        perror("pipe error\n");
        return -1;
    }

    while(true) {
        if(get_play_event(vec, s, dir)) {
            pid_t id = fork();
            start = get_time_seconds();
            printf("start=%lu.\n", start);
            if(id == 0) {
                /* exit vlc */
                sprintf(cmd, "killall -9 vlc");
                system_ret = system(cmd);
                /* run vlc */
                sprintf(cmd, "DISPLAY=0.0 vlc \"%s/%s\"  --play-and-exit  --fullscreen",  dir, s.c_str());
                printf("cmd=%s\n", cmd);
                system_ret = system(cmd);
                break;
            } else if(id > 0) {
                do {
                    end = get_time_seconds();
                    if((end - start) > 3600) {
                        printf("timeout, delta=%luS\n", end - start);
                        sprintf(cmd, "killall -9 vlc");
                        system_ret = system(cmd);
                        kill(id, 1);
                        break;
                    }
                    ret = waitpid(id, NULL, WNOHANG);
                    if(0 == ret) {
                        int switch_play = 0;
                        int switch_next = 0;
                        int switch_prev = 0;
                        int need_kill = 0;

                        switch_read(&switch_play, &switch_next, &switch_prev);
                        if(0 == switch_play) {
                            need_kill = 1;
                        } else if(switch_next) {
                            set_next_play_index(dir, vec.size());
                            need_kill = 1;
                        } else if(switch_prev) {
                            set_prev_play_index(dir, vec.size());
                            need_kill = 1;
                        }

                        std::string stat_str = "";
                        int stat = get_audio_card_status("/proc/asound/card0/pcm0p/sub0/status", stat_str);
                        printf("wait=%lus %s stat=%d\n", end - start, stat_str.c_str(), stat);

                        if((need_kill) || (0 == stat)) {
                            sprintf(cmd, "killall -9 vlc");
                            system_ret = system(cmd);
                            kill(id, 1);
                            printf("switch_play=%d audio_stat=%d kill pid=%d\n", switch_play, stat, id);
                            break;
                        }

                        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                    } else if(id == ret) {
                        printf("play done, delta=%lu s\n", end - start);
                    }
                } while(ret != id);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    }

    printf("done, system_ret=%d\n", system_ret);
}

