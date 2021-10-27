#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("wrong usage\n");
        exit(0);
    }

    
    int pid = fork();
    if(pid<0)
    {
        printf("fork(): failed\n");
        exit(1);
    }

    if(pid!=0)
    {
       // 
    }

    else if (pid == 0)
    {
        strace(2147483647);
        exec(argv[2], argv+2);
        printf("exec %s failed\n", argv[2]);
        exit(1);
    }
    


exit(0);
}
