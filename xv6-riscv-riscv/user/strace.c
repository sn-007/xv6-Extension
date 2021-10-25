#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    strace(12);

    // int pid;
    // if (argc < 2)
    // {
    //     fprintf(1, "wrong usage\n");
    //     exit(0);
    // }
 
    // else
    // {
    //     pid = fork();
    //     if (pid == 0)
    //     {
    //         exec(argv[2], argv+2);
    //         printf("exec %s failed\n", argv[2]);
    //         exit(0);
    //     }
        
    // }

    exit(0);
}
