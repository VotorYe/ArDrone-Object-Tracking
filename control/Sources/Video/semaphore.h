#ifndef SEMAPHORE_
#define SEMAPHORE_

#include <unistd.h>  
#include <sys/types.h>  
#include <sys/stat.h>  
#include <fcntl.h>  
#include <stdlib.h>  
#include <stdio.h>  
#include <string.h>  
#include <sys/sem.h>


union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

int set_semvalue(int semid) {
    // 初始化信号量，在使用信号量之前必须这样做？
    union semun sem_union;

    sem_union.val = 1;
    if (semctl(semid, 0, SETVAL, sem_union) == -1)
        return 0;
    return 1;
}

// 删除信号量
void del_semaphore(int semid) {
    union semun sem_union;

    // 第二个参数是信号集，信号量个数为1时传入0代表
    if (semctl(semid, 0, IPC_RMID, sem_union) == -1) {
        fprintf(stderr, "Fail to delete semaphore\n");
        exit(EXIT_FAILURE);
    }
}

int semaphore_P(int sem_id) {
    struct sembuf sem_b;
    sem_b.sem_num = 0;
    sem_b.sem_op = -1;
    sem_b.sem_flg = SEM_UNDO;

    // 第三个参数表示操作的信号量个数
    // 当信号量为0时，无法进行p操作，会被挂起。
    if (semop(sem_id, &sem_b, 1) == -1) {
        fprintf(stderr, "semaphore_P fail\n");
        return 0;
    }
    return 1;
} 

int semaphore_V(int sem_id) {
    struct sembuf sem_b;
    sem_b.sem_num = 0;
    sem_b.sem_op = 1;
    sem_b.sem_flg = SEM_UNDO;

    if (semop(sem_id, &sem_b, 1) == -1) {
        fprintf(stderr, "semaphore_V fail\n");
        return 0;
    }
    return 1;
}
#endif