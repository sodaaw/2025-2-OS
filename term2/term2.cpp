#include <iostream>
#include <vector>
#include <deque>
#include <algorithm>
#include <string>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <signal.h>

using namespace std;

// 1. Configuration & Constants
#define PROCESS_COUNT 10
#define SIMULATION_TICKS 10000 // 과제 요구사항: 10000 ticks 충족
#define PAGE_SIZE 4096        
#define PHY_MEM_SIZE (512 * 4096) // 512 Frames (2MB)
#define TOTAL_FRAMES (PHY_MEM_SIZE / PAGE_SIZE)
#define ACCESS_PER_TICK 10     

// IPC Keys
#define KEY_Q1 11111
#define KEY_Q2 22222

// 출력용 이모지와 멘트
const string P_EMOJIS[10] = {"🐶", "🐱", "🐭", "🐹", "🐰", "🦊", "🐻", "🐼", "🐨", "🐯"};
const vector<string> BATTLE_QUOTES = {
    "비켜! 이 땅은 이제 제 겁니다. (｀Δ´)!",       
    "방 빼! 월세 밀렸어! (¬‿¬ )",                 
    "미안하지만 내가 좀 급해서.... (｡•́︿•̀｡)",       
    "여긴 이제 내 영역이야! ٩(◕‿◕｡)۶",            
    "Swap Out 되신 걸 환영합니다. ( ◡‿◡ *)",       
    "메모리 부족? 난 아닌데? ┐(￣∀￣)┌",           
    "저리 가! 너무 좁잖아! ヽ( `д´*)ノ",            
    "FIFO 법칙에 의해 퇴거 조치합니다. (￣^￣)ゞ"   
};

// 2. Data Structures (Logic)

struct PageEntry {
    bool valid;
    bool is_swapped; // Swap 영역에 있는지 여부
    int frame_number;
};

// [Logic Enhancement] Reverse Mapping (Frame -> Owner)
// Physical 프레임이 누구 것인지 추적하기 위한것 (Replacement 시 Invalid 처리를 위해 필요함)
struct FrameInfo {
    int pid;      // 소유자 PID (logical_pid)
    int page_idx; // VA
};

struct MsgBuf {
    long mtype; int pid; int cpu_burst; int io_burst;
    int access_pages[ACCESS_PER_TICK]; 
};

struct PCB {
    int pid; int logical_pid; 
    int cpu_burst; int io_burst; int io_remaining;
    int page_fault_count; // 통계 출력하기 위함
    int swap_count;       // 통계 출력하기 위함
    vector<PageEntry> page_table; 
};

// 3. Global Variables (Kernel State)

// Memory Management
deque<int> free_frame_list;         // 초기 빈 프레임 리스트
deque<int> active_frames_queue;     // [FIFO Logic] 할당된 프레임 순서 관리
FrameInfo frame_table[TOTAL_FRAMES];// [Reverse Map] 프레임별 소유자 정보

// System Statistics
long total_page_faults = 0;
long total_swap_outs = 0;
long total_access = 0;

// IPC & Log
int q1_id, q2_id; 
FILE* log_fp; 

// Visualization State
int last_victim_frame = -1; 
string last_battle_log = "System Initialized... Waiting for Requests.";

// 4. Helper Functions

void log_event(int tick, int pid, string msg) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "[Tick %d] [P%d %s] %s\n", tick, pid, P_EMOJIS[pid].c_str(), msg.c_str());
    fprintf(log_fp, "%s", buffer);
}

// System Dashboard & Visual Map
void print_system_status(int tick, PCB* pcb_table[]) {
    system("clear"); 

    // 1. Calculate Stats
    int used_frames = active_frames_queue.size();
    double mem_usage = (double)used_frames / TOTAL_FRAMES * 100.0;
    
    // 2. Top Header (System Monitor Style)
    printf("┌──────────────────────────────────────────────────────────────┐\n");
    printf("│ OS SYSTEM MONITOR (Term Project #2)                          │\n");
    printf("├─────────────────┬──────────────────────┬─────────────────────┤\n");
    printf("│ Tick: %-9d │ Mem: %3d/%-3d (%4.1f%%) │ P.Faults: %-8ld  │\n", 
            tick, used_frames, TOTAL_FRAMES, mem_usage, total_page_faults);
    printf("│ Swap Outs: %-5ld│ FIFO Queue: %-7lu  │ Access: %-9ld   │\n",
            total_swap_outs, active_frames_queue.size(), total_access);
    printf("└─────────────────┴──────────────────────┴─────────────────────┘\n");

    // 3. Visual Memory Map
    printf("\n==== Physical Memory Map (FIFO) ====\n");
    for (int i = 0; i < TOTAL_FRAMES; i++) {
        if (i == last_victim_frame) cout << "💥"; 
        else if (frame_table[i].pid == -1) cout << "⬜"; 
        else cout << P_EMOJIS[frame_table[i].pid];
        
        if ((i + 1) % 32 == 0) cout << endl; 
    }

    // 4. Process Dashboard (Detailed)
    printf("\n==== Process Status Board ====\n");
    printf("%-4s %-4s %-10s %-10s %-20s\n", "PID", "Sym", "CPU-Burst", "Faults", "Memory Share");
    printf("------------------------------------------------------------\n");
    
    // Count frames per process
    int counts[PROCESS_COUNT] = {0};
    for(int i=0; i<TOTAL_FRAMES; i++) {
        if(frame_table[i].pid != -1) counts[frame_table[i].pid]++;
    }

    for(int i=0; i<PROCESS_COUNT; i++) {
        PCB* p = pcb_table[i];
        string bar = "";
        int bars = counts[i] / 4; 
        for(int b=0; b<bars; b++) bar += "█";
        
        printf("P%-3d %s  %-10d %-10d %-3d frames %s\n", 
            i, P_EMOJIS[i].c_str(), p->cpu_burst, p->page_fault_count, counts[i], bar.c_str());
    }

    // 5. Battle Log
    printf("------------------------------------------------------------\n");
    printf("%s\n", last_battle_log.c_str());
    printf("------------------------------------------------------------\n");

    last_victim_frame = -1; // Reset effect
}

// 5. User Process
void run_child(int logic_pid) {
    MsgBuf msg;
    srand(time(NULL) + logic_pid * 100);
    int cpu_burst = rand() % 20 + 5; 
    int io_burst = rand() % 10 + 2;  

    while (true) {
        if (msgrcv(q1_id, &msg, sizeof(MsgBuf) - sizeof(long), logic_pid + 1, 0) == -1) exit(1);

        for (int i = 0; i < ACCESS_PER_TICK; i++) msg.access_pages[i] = rand() % 200; 
        msg.cpu_burst = cpu_burst;
        msg.io_burst = io_burst;
        msg.pid = logic_pid;
        msg.mtype = 999; 

        if (msgsnd(q2_id, &msg, sizeof(MsgBuf) - sizeof(long), 0) == -1) exit(1);

        cpu_burst--;
        if (cpu_burst <= 0) cpu_burst = rand() % 20 + 5;
    }
}

// 6. Kernel Process 
void run_kernel() {
    deque<PCB*> run_queue;
    deque<PCB*> wait_queue;
    PCB* pcb_table[PROCESS_COUNT];

    // Initialization
    for (int i = 0; i < TOTAL_FRAMES; i++) {
        free_frame_list.push_back(i);
        frame_table[i] = {-1, -1}; // No owner
    }

    sleep(1); 

    for (int i = 0; i < PROCESS_COUNT; i++) {
        PCB* pcb = new PCB();
        pcb->logical_pid = i;
        pcb->page_fault_count = 0;
        pcb->swap_count = 0;
        pcb->page_table.resize(200, {false, false, -1}); 
        run_queue.push_back(pcb);
        pcb_table[i] = pcb;
    }

    int tick = 0;
    MsgBuf msg;

    while (tick < SIMULATION_TICKS) {
        // Handle IO
        int wq_size = wait_queue.size();
        while(wq_size--) {
            PCB* proc = wait_queue.front(); wait_queue.pop_front();
            proc->io_remaining--;
            if (proc->io_remaining <= 0) run_queue.push_back(proc);
            else wait_queue.push_back(proc);
        }

        if (!run_queue.empty()) {
            PCB* current_proc = run_queue.front(); run_queue.pop_front();

            // Handshake
            msg.mtype = current_proc->logical_pid + 1;
            msgsnd(q1_id, &msg, sizeof(MsgBuf) - sizeof(long), 0);
            msgrcv(q2_id, &msg, sizeof(MsgBuf) - sizeof(long), 999, 0);

            for (int i = 0; i < ACCESS_PER_TICK; i++) {
                total_access++;
                int page_idx = msg.access_pages[i];
                PageEntry& entry = current_proc->page_table[page_idx];

                if (entry.valid) {
                    // Hit
                    char buf[100];
                    snprintf(buf, sizeof(buf), "Access VA:%d -> PA:%d (Hit)", page_idx, entry.frame_number);
                    log_event(tick, current_proc->logical_pid, buf);
                } else {
                    // Page Fault
                    total_page_faults++;
                    current_proc->page_fault_count++;
                    
                    int allocated_frame = -1;
                    bool replacement_occurred = false;
                    int victim_pid = -1;

                    // 1. Try to get free frame
                    if (!free_frame_list.empty()) {
                        allocated_frame = free_frame_list.front();
                        free_frame_list.pop_front();
                    } 
                    // 2. FIFO Replacement (Senior's Logic)
                    else {
                        replacement_occurred = true;
                        total_swap_outs++;
                        
                        // FIFO: Pop from front (Oldest)
                        allocated_frame = active_frames_queue.front();
                        active_frames_queue.pop_front();

                        // Invalidate Old Owner
                        FrameInfo& info = frame_table[allocated_frame];
                        victim_pid = info.pid;
                        if (victim_pid != -1) {
                            pcb_table[victim_pid]->page_table[info.page_idx].valid = false;
                            pcb_table[victim_pid]->page_table[info.page_idx].is_swapped = true;
                            pcb_table[victim_pid]->swap_count++;
                        }
                        last_victim_frame = allocated_frame; // For Visuals
                    }

                    // 3. Update Tables
                    entry.frame_number = allocated_frame;
                    entry.valid = true;
                    entry.is_swapped = false;
                    
                    // Add to FIFO Queue (Newest is at back)
                    active_frames_queue.push_back(allocated_frame);
                    
                    // Update Reverse Map
                    frame_table[allocated_frame] = {current_proc->logical_pid, page_idx};

                    // 4. Logging & Effects
                    if (replacement_occurred) {
                        string quote = BATTLE_QUOTES[rand() % BATTLE_QUOTES.size()];
                        string vic_emoji = (victim_pid != -1) ? P_EMOJIS[victim_pid] : "👻";
                        char lbuf[256];
                        snprintf(lbuf, sizeof(lbuf), "⚔️ [FIFO SWAP] %s P%d replaces %s P%d: \"%s\"", 
                            P_EMOJIS[current_proc->logical_pid].c_str(), current_proc->logical_pid,
                            vic_emoji.c_str(), victim_pid, quote.c_str());
                        last_battle_log = string(lbuf);
                        log_event(tick, current_proc->logical_pid, "Page Fault & Swap Out P" + to_string(victim_pid));
                    } else {
                        log_event(tick, current_proc->logical_pid, "Page Fault & New Alloc");
                    }
                }
            }
            
            current_proc->cpu_burst = msg.cpu_burst;
            run_queue.push_back(current_proc); 
        }
        
        if(tick % 5 == 0) print_system_status(tick, pcb_table);
        tick++;
        usleep(100000); // 0.1s delay
    }
}

// 7. Main
int main() {
    q1_id = msgget(KEY_Q1, IPC_CREAT | 0666);
    q2_id = msgget(KEY_Q2, IPC_CREAT | 0666);
    msgctl(q1_id, IPC_RMID, NULL); msgctl(q2_id, IPC_RMID, NULL);
    q1_id = msgget(KEY_Q1, IPC_CREAT | 0666); q2_id = msgget(KEY_Q2, IPC_CREAT | 0666);

    log_fp = fopen("vm_final_dump.txt", "w");
    if (!log_fp) { perror("fopen"); return 1; }

    pid_t pids[PROCESS_COUNT];
    cout << "OS Simulation Starting..." << endl;

    for (int i = 0; i < PROCESS_COUNT; i++) {
        pids[i] = fork();
        if (pids[i] == 0) { run_child(i); exit(0); }
    }

    run_kernel();

    for (int i = 0; i < PROCESS_COUNT; i++) kill(pids[i], SIGKILL);
    msgctl(q1_id, IPC_RMID, NULL); msgctl(q2_id, IPC_RMID, NULL);
    fclose(log_fp);

    cout << "\nSimulation Completed. Log saved to 'vm_final_dump.txt'" << endl;
    return 0;
}