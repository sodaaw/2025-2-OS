#include <iostream>
#include <vector>
#include <deque>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <algorithm>
#include <errno.h>

// 1. 시스템 상수 및 설정
#define CHILD_COUNT 10       // 생성할 자식 프로세스 수
#define T_QUANTUM 3          // Time Quantum (Time Slice)
#define SIM_LIMIT 10000      // 시뮬레이션 종료 시간 (Tick)
#define Q_KEY 12345          // 메시지 큐 식별자 키

// 2. 프로토콜 및 데이터 구조

// 부모(커널) -> 자식(유저)에게 명령
enum ParentCommand {
    CMD_EXECUTE_TICK = 1, // 1 tick 실행 명령
    CMD_TERMINATE = -1    // 시뮬레이션 종료 명령
};

// 자식(유저) -> 부모(커널) 응답
enum ChildResponse {
    RESP_TICK_DONE = 0,     // 1 tick 실행 완료 (CPU Burst 남음)
    RESP_IO_REQUEST = 1     // I/O 요청 발생 (Block 필요)
};

// IPC 메시지 버퍼 구조체
struct IpcMsg {
    long mtype;       // 메시지 타겟 (PID)
    int command;      // 명령어 또는 상태 데이터
};

// 프로세스 제어 블록 (PCB)
struct ProcInfo {
    pid_t pid;          // 프로세스 ID
    int cpu_time;       // 남은 CPU Burst Time
    int io_time;        // 남은 I/O Wait Time
    bool is_waiting;    // Blocked 상태 여부
};

// 3. 전역 변수
int mq_id;                      // 메시지 큐 ID
int sys_tick = 0;               // 시스템 현재 시간 (Tick)
volatile sig_atomic_t alarm_triggered = 0; // 타이머 시그널 플래그
int q_counter = 0;              // 현재 프로세스의 남은 Time Quantum

// 4. 로깅 함수
// 현재 시스템 상태(실행 중인 프로세스, Ready/Wait 큐)를 파일, 콘솔에 기록함
void write_log(FILE* fp, int tick, ProcInfo* running_proc, std::deque<pid_t>& r_q, const std::vector<ProcInfo>& p_table) {
    char header_buf[256];
    snprintf(header_buf, sizeof(header_buf), "\n--- Time Tick T: %d ---\n", tick);
    printf("%s", header_buf);
    fprintf(fp, "%s", header_buf);
    
    // Running Process 정보 출력
    printf("[ Running Process ]\n");
    fprintf(fp, "[ Running Process ]\n");
    if (running_proc) {
        printf("PID: %-5d | Remaining CPU: %-5d | Time Quantum Left: %d\n", running_proc->pid, running_proc->cpu_time, q_counter);
        fprintf(fp, "PID: %-5d | Remaining CPU: %-5d | Time Quantum Left: %d\n", running_proc->pid, running_proc->cpu_time, q_counter);
    } else {
        printf("IDLE (CPU 쉬는 중)\n");
        fprintf(fp, "IDLE (CPU 쉬는 중)\n");
    }

    // Ready Queue 정보 출력
    printf("\n[ Ready Queue: %zu Processes ]\n", r_q.size());
    fprintf(fp, "\n[ Ready Queue: %zu Processes ]\n", r_q.size());
    printf("%-10s | %-10s | %-10s\n", "PID", "CPU Left", "Wait Status");
    fprintf(fp, "%-10s | %-10s | %-10s\n", "PID", "CPU Left", "Wait Status");
    printf("----------------------------------\n");
    fprintf(fp, "----------------------------------\n");

    for (pid_t pid : r_q) {
        const ProcInfo* p = nullptr;
        for (const auto& proc : p_table) {
            if (proc.pid == pid) {
                p = &proc;
                break;
            }
        }
        if (p) {
            printf("%-10d | %-10d | %-10s\n", p->pid, p->cpu_time, "READY");
            fprintf(fp, "%-10d | %-10d | %-10s\n", p->pid, p->cpu_time, "READY");
        }
    }
    
    // Wait Queue 정보 출력
    printf("\n[ Wait Queue (I/O): %zu Processes ]\n", std::count_if(p_table.begin(), p_table.end(), [](const ProcInfo& p){ return p.is_waiting; }));
    fprintf(fp, "\n[ Wait Queue (I/O): %zu Processes ]\n", std::count_if(p_table.begin(), p_table.end(), [](const ProcInfo& p){ return p.is_waiting; }));
    printf("%-10s | %-10s | %-10s\n", "PID", "I/O Left", "CPU Next");
    fprintf(fp, "%-10s | %-10s | %-10s\n", "PID", "I/O Left", "CPU Next");
    printf("----------------------------------\n");
    fprintf(fp, "----------------------------------\n");

    for (const auto& p : p_table) {
        if (p.is_waiting) {
            printf("%-10d | %-10d | %-10d\n", p.pid, p.io_time, p.cpu_time);
            fprintf(fp, "%-10d | %-10d | %-10d\n", p.pid, p.io_time, p.cpu_time);
        }
    }
}

// 5. 시그널 핸들러
void on_timer_tick(int signum) {
    alarm_triggered = 1; // 타이머 인터럽트 flag 
}

// 6. 자식 프로세스 
void run_user_process() {
    srand(getpid() * time(NULL));
    int my_cpu_burst = (rand() % 10) + 1;
    IpcMsg pkt;

    while (1) {
        // 스케줄러 명령 대기 (Blocking)
        if (msgrcv(mq_id, &pkt, sizeof(int), getpid(), 0) == -1) {
            if (errno != EIDRM) perror("msgrcv failed");
            exit(0);
        }
        
        // 종료 명령 수신 시 루프를 탈출하게끔
        if (pkt.command == ParentCommand::CMD_TERMINATE) break;

        // 1 tick 작업 수행
        my_cpu_burst--;
        
        pkt.mtype = getppid(); 

        // CPU 작업 완료 여부 확인
        if (my_cpu_burst <= 0) {
            // I/O 작업 요청
            int io_duration = (rand() % 10) + 5;
            pkt.command = io_duration;           
            my_cpu_burst = (rand() % 10) + 1;    
        } 
        else {
            // 작업 미완료 (Tick 소진)
            pkt.command = ChildResponse::RESP_TICK_DONE; 
        }

        // 스케줄러에게 응답 전송
        if (msgsnd(mq_id, &pkt, sizeof(int), 0) == -1) {
            perror("msgsnd failed");
            exit(1);
        }
    }
    exit(0);
}

// 7. I/O 완료 처리 루틴
void handle_io_completion(std::vector<ProcInfo>& p_table, std::deque<pid_t>& ready_q) {
    for (auto& p : p_table) {
        if (p.is_waiting) {
            p.io_time--; // I/O 시간 감소
            
            if (p.io_time <= 0) {
                // I/O 완료 후 Ready Queue로 이동
                p.is_waiting = false;
                p.io_time = 0;
                ready_q.push_back(p.pid);
            }
        }
    }
}

// 8. 메인 함수 (Kernel, Scheduler)
int main() {
    // 1. 메시지 큐 초기화
    mq_id = msgget(Q_KEY, IPC_CREAT | 0666);
    if (mq_id == -1) { perror("msgget failed"); return 1; }

    std::vector<ProcInfo> p_table; 
    std::deque<pid_t> ready_q;     

    // 2. 프로세스 생성
    for (int i = 0; i < CHILD_COUNT; ++i) {
        pid_t pid = fork();
        if (pid == 0) { 
            run_user_process(); 
        }
        else if (pid > 0) { 
            // PCB 초기화 및 Ready Queue 등록
            ProcInfo p = {pid, (rand() % 10) + 1, 0, false};
            p_table.push_back(p);
            ready_q.push_back(pid);
        } else { 
            perror("fork failed"); return 1; 
        }
    }

    // 3. 타이머 및 시그널 핸들러 설정
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &on_timer_tick;
    sigaction(SIGALRM, &sa, NULL);

    struct itimerval timer;
    // 10ms 간격으로 설정 (1 Tick)
    timer.it_value.tv_sec = 0; timer.it_value.tv_usec = 10000;
    timer.it_interval.tv_sec = 0; timer.it_interval.tv_usec = 10000;
    setitimer(ITIMER_REAL, &timer, NULL);

    // 4. 로그 파일 설정
    FILE* log_fp = fopen("schedule_dump.txt", "w");
    if (!log_fp) { perror("file open failed"); return 1; }

    pid_t running_pid = -1; // 현재 실행 중인 프로세스 ID

    // 5. 메인 스케줄링
    while (sys_tick < SIM_LIMIT) {
        pause(); // 시그널 대기 (Idle)
        
        if (alarm_triggered) { 
            alarm_triggered = 0;
            sys_tick++; 

            // 1. I/O 완료 프로세스 처리
            handle_io_completion(p_table, ready_q);

            // 2. 스케줄링 결정 (Dispatch)
            if (running_pid == -1 && !ready_q.empty()) {
                running_pid = ready_q.front();
                ready_q.pop_front();
                q_counter = T_QUANTUM; // Time Quantum 할당
            }
            
            ProcInfo* curr_proc_info = nullptr; 

            // 3. 프로세스 실행
            if (running_pid != -1) {
                for (auto& p : p_table) { 
                    if (p.pid == running_pid) { 
                        curr_proc_info = &p; 
                        break; 
                    } 
                }

                if (curr_proc_info) {
                    IpcMsg pkt;
                    pkt.mtype = running_pid;
                    pkt.command = ParentCommand::CMD_EXECUTE_TICK; 
                    
                    // 실행 명령 전송 및 응답 대기 (IPC Handshake)
                    msgsnd(mq_id, &pkt, sizeof(int), 0);
                    // 부모는 자기자신(getpid) 앞으로 온 메시지를 받아야 함
                    msgrcv(mq_id, &pkt, sizeof(int), getpid(), 0);
                    
                    curr_proc_info->cpu_time--;
                    q_counter--;

                    // 응답 처리
                    if (pkt.command > ChildResponse::RESP_TICK_DONE) {
                        // Case 1 - I/O 요청 (Block)
                        curr_proc_info->is_waiting = true;
                        curr_proc_info->io_time = pkt.command;
                        // 다음 실행을 위해 CPU Burst 시간 재설정
                        curr_proc_info->cpu_time = (rand() % 10) + 1; 

                        running_pid = -1; // CPU 해제
                    } 
                    else {
                        // Case 2 - Time Quantum 만료된 경우 (Preemption)
                        if (q_counter <= 0) { 
                            ready_q.push_back(running_pid); 
                            running_pid = -1;
                        } 
                        // Case 3 - CPU Burst 완료된 경우 (즉시 재할당)
                        else if (curr_proc_info->cpu_time <= 0) { 
                            curr_proc_info->cpu_time = (rand() % 10) + 1;
                        }
                    }
                }
            } 
            
            // 4. 로깅
            write_log(log_fp, sys_tick, curr_proc_info, ready_q, p_table); 
        }
    }
    
    // 6. 시뮬레이션 종료
    fclose(log_fp);
    IpcMsg pkt; 
    pkt.command = ParentCommand::CMD_TERMINATE; 
    
    for (auto& p : p_table) { 
        pkt.mtype = p.pid; 
        msgsnd(mq_id, &pkt, sizeof(int), 0); 
        waitpid(p.pid, NULL, 0); 
    }
    
    msgctl(mq_id, IPC_RMID, NULL); 
    printf("Simulation Completed.\n");
    
    return 0;
}