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

int get_weekday(void)
{
    time_t rawtime;
    struct tm *timeinfo;

    time(&rawtime);
    timeinfo = localtime(&rawtime); // 将时间转换为本地时间结构体

    return timeinfo->tm_wday;
}

int get_play_event(std::vector<std::string> &vec, std::string &s)
{
    int week_day = get_weekday();

    s = vec[0];
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
    std::string str_close = "close\n";

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

    if(v1[0].size() > 1) {
        return 1;
    }

    if(v1[0] == str_close) {
        return 0;
    }

    return 1;
}

int main(int argc, char *argv[])
{
    int ret = 0;
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
        vec.push_back(it.path().filename());
    }

    for (auto i: vec) {
        std::cout << i << std::endl;
    }

    int fd[2];

    ret = pipe(fd);

    if(ret==-1) {
        perror("pipe error\n");
        return -1;
    }

    while(true) {
        if(get_play_event(vec, s)) {
            pid_t id = fork();
            start = get_time_seconds();
            printf("start=%lu.\n", start);
            if(id == 0) {
                /* exit vlc */
                sprintf(cmd, "killall -9 vlc");
                system(cmd);
                /* run vlc */
                sprintf(cmd, "DISPLAY=0.0 vlc \"%s/%s\"  --play-and-exit  --fullscreen",  dir, s.c_str());
                printf("cmd=%s\n", cmd);
                system(cmd);
                break;
            } else if(id > 0) {
                do {
                    end = get_time_seconds();
                    if((end - start) > 3600) {
                        printf("timeout, delta=%luS\n", end - start);
                        sprintf(cmd, "killall -9 vlc");
                        system(cmd);
                        kill(id, 1);
                        break;
                    }
                    ret = waitpid(id, NULL, WNOHANG);
                    if(0 == ret) {
                        std::string stat_str = "";
                        int stat = get_audio_card_status("/proc/asound/card0/pcm0p/sub0/status", stat_str);
                        printf("\rwait=%luS status=%s", end - start, stat_str.c_str());
                        fflush(stdout);
                        if(0 == stat) {
                            sprintf(cmd, "killall -9 vlc");
                            system(cmd);
                            kill(id, 1);
                            printf("audio stop, kill pid=%d\n", id);
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
}
