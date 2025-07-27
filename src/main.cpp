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

int main(int argc, char *argv[])
{
    int ret = 0;
    char cmd[1024] = {0};

    uint64_t start, end;


    std::string s;
    std::vector<std::string> vec;
    std::filesystem::directory_iterator list(".");	        //文件入口容器
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
            		sprintf(cmd, "DISPLAY=0.0 vlc \"%s\"  --play-and-exit  --fullscreen",  s.c_str());
            		printf("cmd=%s\n", cmd);
            		system(cmd);
			break;
		} else if(id > 0) {
			do {
			    end = get_time_seconds();
			    if((end - start) > 600) {
            		            printf("timeout, delta=%luS\n", end - start);
				    kill(id, 1);
				    break;
			    }
			    ret = waitpid(id, NULL, WNOHANG);
			    if(0 == ret) {
            		        printf("\rwait=%luS", end - start);
				fflush(stdout);
                                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			    } else if(id == ret){
            		            printf("play done, delta=%lu s\n", end - start);
			    }
			} while(ret != id);
		}
	}
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    }
}
