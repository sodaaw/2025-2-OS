# OS Term Project
## Term 1: 프로세스 스케줄러 시뮬레이션

Round-Robin 스케줄링 알고리즘을 구현한 프로세스 스케줄러 시뮬레이터입니다.

### 주요 기능

- 10개의 자식 프로세스 생성 및 관리
- Time Quantum 3을 사용한 Round-Robin 스케줄링
- I/O 요청 처리 및 Wait Queue 관리
- 메시지 큐를 통한 부모-자식 프로세스 간 통신
- 10ms 단위의 타이머 기반 Tick 시뮬레이션
- Ready Queue와 Wait Queue 상태 로깅

### 실행 방법

```bash
cd term1
g++ -o term1 term1.cpp
./term1
```

실행 후 `schedule_dump.txt` 파일에 스케줄링 로그가 저장됩니다.

## Term 2: 가상 메모리 관리 시뮬레이션

FIFO 페이지 교체 알고리즘을 사용한 가상 메모리 관리 시스템 시뮬레이터입니다.

### 주요 기능

- 10개의 프로세스에 대한 가상 메모리 관리
- 페이지 폴트 처리 및 페이지 할당
- FIFO 알고리즘을 사용한 페이지 교체
- 물리 메모리 512 프레임 (2MB) 관리
- Tick당 10회의 메모리 접근 시뮬레이션
- 실시간 시스템 상태 모니터링 출력

### 실행 방법

```bash
cd term2
g++ -o term2 term2.cpp
./term2
```

실행 후 `vm_final_dump.txt` 파일에 가상 메모리 관리 로그가 저장됩니다.

## 공통 사항

- C++로 작성되었으며 Linux/Unix 환경에서 실행됩니다
- 메시지 큐를 사용한 프로세스 간 통신
- 시뮬레이션은 10000 ticks 동안 실행됩니다

