#include "stdio.h"
#include "unistd.h"

int main()
{
    printf("...\n");
    sleep(5);
    int pid = fork();
    if(pid == -1) {
        printf("failed!\n");
    }
    if(pid != 0) {
        printf("I am father, pid is %d\n", getpid());
        sleep(5);
        return 0;
    } else {
        printf("I am child, pid is %d\n", getpid());
        sleep(5);
        return 0;
    }
}