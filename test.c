#include "stdio.h"
#include "stdlib.h"
#include "dirent.h"

int main()
{
    DIR* p_dir = NULL;
    struct dirent* dir_e = NULL;

    p_dir = opendir("/");
    if(p_dir) {
        while(dir_e = readdir(p_dir)) {
            printf("%s ", dir_e->d_name);
        }
        printf("\n");
    }
    return 0;
}