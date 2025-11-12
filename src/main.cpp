#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <memory>
#include <queue>
#include <mutex>
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
#include "sys/stat.h"

extern "C" {
    typedef int (*callback_t)(int type, int code, int value);
    int getevent_main(int argc, char *argv[], callback_t callback);
};

typedef struct {
    int type;
    int code;
    int value;
}custom_event_t;

std::mutex share_mtx;
std::queue<custom_event_t> share_queue;

uint64_t get_time_seconds(void);

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

static bool IsVideo(const char *ext)
{
    bool isVideo = false;
    const char * formateList[] = {
         ".avi",".flv",".mpg",".mpeg",".mpe",".m1v",".m2v",".mpv2",".mp2v",".ts",".tp",".tpr",".pva",".pss",".mp4",".m4v",
         ".m4p",".m4b",".3gp",".3gpp",".3g2",".3gp2",".ogg",".mov",".rm",".ram",".rmvb",".rpm"
    };

    const int file_len = strlen(ext);

    for(const char* i :formateList) {
        int len = strlen(i);

	if(0 == strncmp(i, ext, len)) {
            isVideo = true;
            break;
	}
    }

    return isVideo;
}

int get_file_list(std::vector<std::string> & vec, const char *dir)
{
    std::filesystem::directory_iterator list(dir);	        //文件入口容器
    int count = 0;

    for (auto& it:list) {
        if(0 == strcmp(it.path().filename().c_str(), "play_idx")) {
            continue;
        }
        if(0 == strcmp(it.path().filename().c_str(), "play_dir")) {
            continue;
        }

        if(std::filesystem::is_directory(it.path())) {
	    count++;
            vec.push_back(it.path().filename());
        } else if ((std::filesystem::is_regular_file(it.path())) && (IsVideo(it.path().extension().c_str()))){
	    count++;
            vec.push_back(it.path().filename());
	}
    }

    std::sort(vec.begin(), vec.end());

    int j = 0;
    for (auto i: vec) {
        printf("[%02d]%s\n", j++, i.c_str());
    }

    printf("total count=%d\n", count);
    return 0;
}

int get_current_play_index(const char *dir, int *out_dir_idx, int max_dir, int *out_file_idx, int max_file)
{
    char full_path[256];
    char full_dir[256];
    int file_idx= 0;
    int dir_idx = 0;
    int len = 0;
    FILE *fp = NULL;

    snprintf(full_path, sizeof(full_path), "%s/%s", dir, "play_idx");
    snprintf(full_dir, sizeof(full_dir), "%s/%s", dir, "play_dir");

    if(0 == access(full_path, 0) && 0 == access(full_dir, 0)) {
        fp = fopen(full_path, "r");
        len = fscanf(fp, "%d",  &file_idx);
        fclose(fp);

        fp = fopen(full_dir, "r");
        len = fscanf(fp, "%d",  &dir_idx);
        fclose(fp);
    } else {
        printf("%s: create file1=%s file2=%s\n", __func__, full_dir, full_path);
        fp = fopen(full_path, "w+");
        fprintf(fp, "%d\n",  0);
        fclose(fp);
        file_idx = 0;

        fp = fopen(full_dir, "w+");
        fprintf(fp, "%d\n",  0);
        fclose(fp);
        dir_idx= 0;
    }

    printf("%s: load dir_idx=%d file_idx=%d\n", __func__, dir_idx, file_idx);

    if(out_file_idx) {
        *out_file_idx = file_idx;
    }

    if(out_dir_idx) {
        *out_dir_idx = dir_idx;
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
    int idx    = 0;
    int origin = 0;

    get_current_play_index(dir, nullptr, 0, &idx, max);

    origin = idx++;
    if(idx >= max) {
        printf("idx = %d is too big, roll back to 0\n", idx);
        idx = 0;
    }

    save_current_play_index(dir, &idx, max);

    printf("%s: idx: %d->%d\n", __func__, origin, idx);
    return 0;
}

int set_prev_play_index(const char *dir, int max)
{
    int idx = 0;
    int origin = 0;

    get_current_play_index(dir, nullptr, 0, &idx, max);

    origin = idx--;
    if(idx < 0) {
        printf("idx = %d is too small , roll back to max-1\n", idx);
        idx = max - 1;
    }
    save_current_play_index(dir, &idx, max);

    printf("%s: file_idx: %d->%d\n", __func__, origin, idx);
    return 0;
}

int save_current_play_dir(const char *dir, int *idx, int max)
{
    char full_path[256];
    int index = 0;

    snprintf(full_path, sizeof(full_path), "%s/%s", dir, "play_dir");

    FILE *fp = fopen(full_path, "w+");
    fprintf(fp, "%d\n",  *idx);
    fclose(fp);

    return 0;
}

int set_next_play_dir(const char *dir, int max)
{
    int idx    = 0;
    int origin = 0;

    get_current_play_index(dir, &idx, 0, nullptr, 0);

    origin = idx++;
    if(idx >= max) {
        printf("idx = %d is too big, roll back to 0\n", idx);
        idx = 0;
    }

    save_current_play_dir(dir, &idx, max);

    printf("%s: idx: %d->%d\n", __func__, origin, idx);
    return 0;
}

int enable_hdmi_output(int enable)
{
    FILE *fp = nullptr;
    char full_path[256];
    char rd_buffer[256];
    const char *value_str[] = {"off", "on"};
    const char *value_expect[] = {"disconnected", "connected"};
    const char *value = value_str[!!enable];
    int len = 0;

    std::chrono::system_clock::time_point start = std::chrono::system_clock::now();

    snprintf(full_path, sizeof(full_path), "%s/%s", "/sys/class/drm/card1-HDMI-A-3/", "status");

    /* read and check hdmi status */
    fp = fopen(full_path, "r");
    if (nullptr == fp) {
        printf("%s: open %s fail\n", __func__, full_path);
	return -1;
    }

    len = fscanf(fp, "%s",  rd_buffer);
    fclose(fp);

    if(strcmp(value_expect[!!enable], rd_buffer)) {
        fp = fopen(full_path, "w+");
        if (nullptr == fp) {
            printf("%s: open %s fail\n", __func__, full_path);
            return -2;
        }

        fprintf(fp, "%s\n",  value);
        fclose(fp);

        std::chrono::system_clock::time_point end = std::chrono::system_clock::now();
        auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count();

        printf("%s: read=%s len=%d expect=%s write=%s cost=%ldMs\n", __func__, rd_buffer, len, value_expect[!!enable], value, delta);
    }

    return 0;
}

int switch_read(int *sw1, int *sw2, int *sw3, int *sw4)
{
    char rd_buffer[512] = {0};
    uint8_t modbus_request[] = {0x01, 0x04, 0x00, 0x00,0x00, 0x04, 0xF1, 0xC9};
    uint8_t modbus_respond[] = {0x01, 0x04, 0x08, 0x00,0x01, 0x00, 0x00,0x00,0x00,0x00,0x00,0x34,0xCD};
    int baud = 9600;
    const char *port = "/dev/ttyUSB0";
    static bool keyboard_active = false;
    static bool modbus_active = false;
    static std::chrono::system_clock::time_point start = std::chrono::system_clock::now();

    int week_day = get_weekday();

    std::unique_lock<std::mutex> lck(share_mtx);
    if(share_queue.size() > 0) {
        /* update start */
        start = std::chrono::system_clock::now();
        custom_event_t ev = {0};
        do {
            ev = share_queue.front();
            share_queue.pop();
        }while(share_queue.size() > 0);
        printf("type=%d code=%d value=%d queue_len=%ld\n", ev.type, ev.code, ev.value, share_queue.size());

        /* this is the expect case */
        if(sw1) {
            *sw1 = 1;
            keyboard_active = true;
            enable_hdmi_output(1);
        }
        if(sw2) {
            *sw2 = ev.code == 105? 1: 0;
        }
        if(sw3) {
            *sw3 = ev.code == 106? 1: 0;
        }
        if(sw4) {
            *sw4 = ev.code == 15? 1: 0;
        }

        /* exit key */
        if (48 == ev.code) {
           std::chrono::seconds t1(1200); 
           start = std::chrono::system_clock::now() - t1; 
           printf("recv key=48, exit keyboard active\n");
        }
	return 0;
    } else if (keyboard_active) {
        /* calc delta */
        auto delta = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - start).count();
	if (delta > 1200ul) {
	    /* 20 minite = 1200 seconds  */
	    keyboard_active = false;
            enable_hdmi_output(0);
	} else {
            /* this is the keyboard acitve mode */
            *sw1 = 1;
            *sw2 = 0;
            *sw3 = 0;
            printf("keyboard active mode, delta=%ld remain=%02ld:%02ld\n", delta, (1200ul - delta)/60, (1200ul - delta)%60);
	    return 0;
	}
    }

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
	if (modbus_respond[4]) {
            if(!modbus_active) {
                enable_hdmi_output(1);
                modbus_active = true;
	    }
	} else {
            if(modbus_active) {
                enable_hdmi_output(0);
                modbus_active = false;
	    }
	}
    }
    if(sw2) {
        *sw2 = modbus_respond[6];
    }
    if(sw3) {
        *sw3 = modbus_respond[8];
    }

    //int switch_play = modbus_respond[10];
    //printf("recv=[%s] crc_recv(0x%x) == crc_calc(0x%x)\n", rd_buffer, crc_recv, crc_calc);
    return 0;
}

bool check_out_of_range(int idx, int max)
{
    if((idx >= 0) && idx < max) {
	  return false;
    }

    return true;
}

int get_play_event(std::vector<std::string> &vec_dir, std::string &s, const char *dir)
{
    char file_full_path[256];
    char dir_full_path[256];
    int switch_play = 0;
    int switch_next = 0;
    int switch_prev = 0;
    uint64_t start = 0;

    start = get_time_seconds();
    switch_read(&switch_play, &switch_next, &switch_prev, nullptr);

    if(!switch_play) {
        printf("switch_play=%d is not on, wait next(%ld)\n", switch_play, start);
        return 0;
    }

    int file_idx = 0;
    int dir_idx  = 0;
    int retry = 0;
    std::string splitc = "/";
    std::vector<std::string> vec_file;

    do {
        if(get_current_play_index(dir, &dir_idx, vec_dir.size(), &file_idx, 0)) {
            printf("get_next_play_index failed\n");
            return 0;
        }

	snprintf(dir_full_path, sizeof(dir_full_path), "%s/%s", dir, vec_dir[dir_idx].c_str());
        printf("%s: list dir=%s\n", __func__, dir_full_path);
        get_file_list(vec_file, dir_full_path);

	if(vec_file.size() == 0) {
             set_next_play_dir(dir, vec_dir.size());
	     continue;
	}

        if(check_out_of_range(file_idx, vec_file.size())) {
             printf("index=%d is out of range(0-%ld), load 0\n", file_idx, vec_file.size());
             file_idx = 0;
        }

	snprintf(file_full_path, sizeof(file_full_path), "%s/%s/%s", \
			dir, vec_dir[dir_idx].c_str(), vec_file[file_idx].c_str());
        if(0 == access(file_full_path, 0)) {
            printf("dir_idx=%d file_idx=%d file=%s\n", dir_idx, file_idx, file_full_path);
            break;
        }

        printf("file=%s access failed, try next\n", file_full_path);
        set_next_play_index(dir_full_path, vec_file.size());
    } while(++retry < 1000);

    s =  vec_dir[dir_idx] + splitc + vec_file[file_idx];
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

int onKeyEvent(int type, int code, int value)
{
    if(1 != type) {
        return 0;
    }
    std::unique_lock<std::mutex> lck(share_mtx);
    custom_event_t ev {type, code, value};

    if(value == 1) {
        share_queue.push(ev);
    }

    //printf("type=%d code=%d value=%d queue_len=%ld\n", type, code, value, share_queue.size());
    return 0;
}

int main(int argc, char *argv[])
{
    int ret = 0;
    int system_ret = 0;
    char cmd[1024] = {0};
    const char * root_dir = ".";

    uint64_t start, end;

    for(int i = 0; i < argc; i++) {
        if(0 == strcmp("-d", argv[i])) {
            if(i+1 >= argc) continue;
            root_dir = argv[i+1];
        }
    }

    int fd[2];
    std::string s;
    std::vector<std::string> vec;

    ret = pipe(fd);

    if(ret==-1) {
        perror("pipe error\n");
        return -1;
    }

    /* enable HDMI to fix VLC init fail */
    enable_hdmi_output(1);

    std::thread t([]() {
        char argv1[] = "main";
        char argv2[] = "/dev/input/event10";
        char *argv[2] = {argv1, argv2};
        getevent_main(2, argv, &onKeyEvent);
	while(1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5000));
        }
    });

    printf("%s: list dir=%s\n", __func__, root_dir);
    get_file_list(vec, root_dir);

    while(true) {
        if(get_play_event(vec, s, root_dir)) {
            pid_t id = fork();
            start = get_time_seconds();
            printf("start=%lu.\n", start);
            if(id == 0) {
                /* exit vlc */
                sprintf(cmd, "killall -9 vlc");
                system_ret = system(cmd);
                /* run vlc */
                sprintf(cmd, "DISPLAY=0.0 vlc \"%s/%s\"  --play-and-exit  --fullscreen",  root_dir, s.c_str());
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
                        int switch_next_dir = 0;
                        int need_kill = 0;

                        switch_read(&switch_play, &switch_next, &switch_prev, &switch_next_dir);
                        if(0 == switch_play) {
                            need_kill = 1;
                        } else if(switch_next) {
                            set_next_play_index(root_dir, vec.size());
                            need_kill = 1;
                        } else if(switch_prev) {
                            set_prev_play_index(root_dir, vec.size());
                            need_kill = 1;
                        } else if(switch_next_dir) {
                            set_next_play_dir(root_dir, vec.size());
                            need_kill = 1;
                        }

                        std::string stat_str = "";
                        int stat = get_audio_card_status("/proc/asound/card0/pcm3p/sub0/status", stat_str);
                        printf("wait=%05ds %s stat=%d\n", (int)(end - start), stat_str.c_str(), stat);

                        if((need_kill) || (0 == stat)) {
                            sprintf(cmd, "killall -9 vlc");
                            system_ret = system(cmd);
                            kill(id, 1);
                            printf("switch_play=%d audio_stat=%d kill pid=%d\n", switch_play, stat, id);

			    /* stat == 0 means play done */
			    if(0 == stat) {
                                set_next_play_index(root_dir, vec.size());
			    }
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

    printf("applicatino run done, system_ret=%d\n", system_ret);
}

