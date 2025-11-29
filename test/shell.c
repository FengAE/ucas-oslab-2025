/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *            Copyright (C) 2018 Institute of Computing Technology, CAS
 *               Author : Han Shukai (email : hanshukai@ict.ac.cn)
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *                  The shell acts as a task running in user mode.
 *       The main function is to make system calls through the user's output.
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * */

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <stddef.h>

#define BUFFER_SIZE 100
#define SHELL_BEGIN 20
#define SHELL_END 50

char buffer[BUFFER_SIZE]; 
int buf_ptr = 0;

void Backspace()
{
    if(buf_ptr > 0)
    {
        buf_ptr--;
        printf("\b \b");
    }
}

int main(void)
{
    sys_clear(SHELL_BEGIN, SHELL_END);
    sys_move_cursor(0, SHELL_BEGIN);
    printf("------------------- COMMAND -------------------\n");
    sys_move_cursor(0, SHELL_BEGIN+1);
    printf("> root@UCAS_OS: ");

    int ch;
    while (1)
    {
        // TODO [P3-task1]: call syscall to read UART port 
        // TODO [P3-task1]: parse input
        // note: backspace maybe 8('\b') or 127(delete)
        while((ch = sys_getchar()) == -1)
            sys_yield();
        if(ch == '\r' || ch == '\n')
        {
            printf("\n");
            if(buf_ptr != 0)
            {
                buffer[buf_ptr] = '\0';
                if(strcmp(buffer, "clear") == 0)
                {
                    sys_clear(SHELL_BEGIN, SHELL_END);
                    printf("------------------- COMMAND -------------------\n");
                }
                else if(strcmp(buffer, "ps") == 0)
                    sys_ps();
                else
                {
                    // int ptr = 0;
                    // char command[BUFFER_SIZE];
                    // for(; ptr<strlen(buffer) && buffer[ptr]!=' '; ptr++)
                    //     command[ptr] = buffer[ptr];
                    // command[ptr] = '\0';
                    int argc = 0;
                    char *argv[16]; 
                    int i = 0;
                    int len = strlen(buffer);
                    while (i < len) {
                        while (i < len && buffer[i] == ' ') {
                            buffer[i] = '\0'; 
                            i++;
                        }
                        if (i >= len) break;
                        argv[argc++] = &buffer[i];
                        // find the end of current word
                        while (i < len && buffer[i] != ' ') 
                            i++;
                    }
                    argv[argc] = NULL;

                    if (strcmp(argv[0], "exec") == 0) 
                    {                         
                        if (argc > 1) 
                        {
                            pid_t pid = sys_exec(argv[1], argc, argv); 
                            // if(pid == -1)    printf("Error: task alreay exits: \"%s\"\n", argv[1]);
                            if(pid == -2)  printf("Error: no free pcb\n");
                            else if(pid == -3)  printf("Error: load task image failed: \"%s\"\n", argv[1]);
                            else    printf("exec pid: [%d], successfully!\n", pid);
                            
                            if(!(argc == 3 && strcmp(argv[argc-1], "&") == 0))
                                sys_waitpid(pid);

                        } else 
                            printf("Error: exec needs parameters\n");
                    }
                    else if(strcmp(argv[0], "kill") == 0)
                    {
                        if(argc != 2)    
                            printf("Error: expect format: kill id\n");
                        else
                        {
                            int ptr = 0, id = 0;
                            for(; ptr<strlen(argv[1]) && argv[1][ptr]>='0' && argv[1][ptr]<='9'; ptr++)
                                id = id*10 + argv[1][ptr]-'0';
                            if(ptr < strlen(argv[1]))
                                printf("Error: expect format: kill id\n");
                            else
                            {
                                printf("kill pid: [%d]\n", id);
                                sys_kill(id);
                            }
                        }
                    }
                    else if(strcmp(argv[0], "taskset") == 0)
                    {
                        if((argc == 3) && (strncmp(argv[1], "0x", 2)==0))
                        {
                            int ptr = 2, mask = 0;
                            for(; ptr<strlen(argv[1]); ptr++)
                            {
                                if(argv[1][ptr]>='0' && argv[1][ptr]<='9')
                                    mask = mask*16 + argv[1][ptr]-'0';
                                else if(argv[1][ptr]>='a' && argv[1][ptr]<='f')
                                    mask = mask*16 + 10 + argv[1][ptr]-'a';
                                else break;
                            }
                            if(ptr < strlen(argv[1]))
                                printf("Error: expect format: taskset mask name\n");
                            else
                            {
                                printf("taskset 0x%d successfully\n", mask);
                                sys_taskset((void*)argv[2], mask, 1);
                            }
                        }
                        else if((argc == 4) && (strcmp(argv[1], "-p")==0) && (strncmp(argv[2], "0x", 2)==0))
                        {
                            int ptr = 2, mask = 0;
                            for(; ptr<strlen(argv[2]); ptr++)
                            {
                                if(argv[2][ptr]>='0' && argv[2][ptr]<='9')
                                    mask = mask*16 + argv[2][ptr]-'0';
                                else if(argv[2][ptr]>='a' && argv[2][ptr]<='f')
                                    mask = mask*16 + 10 + argv[2][ptr]-'a';
                                else break;
                            }
                            printf("set mask 0x%d\n", mask);
                            if(ptr < strlen(argv[2]))
                                printf("Error: expect format: taskset -p mask pid\n");
                            else
                            {
                                ptr = 0;
                                int id = 0;
                                for(; ptr<strlen(argv[3]) && argv[3][ptr]>='0' && argv[3][ptr]<='9'; ptr++)
                                    id = id*10 + argv[3][ptr]-'0';
                                if(ptr < strlen(argv[3]))
                                    printf("Error: expect format: taskset -p mask pid\n");
                                else
                                    sys_taskset((void*)id, mask, 0);
                            }
                        }
                    }
                    else
                        printf("Error: Unkown command \"%s\"\n", buffer);
                }  
            }
            printf("> root@UCAS_OS: ");
            buf_ptr = 0;
        }
        else 
        {
            if(ch == '\b' || ch == 127)
            {
                Backspace();
                continue;
            }
            buffer[buf_ptr++] = ch;
            printf("%c", ch);
        }      
        // TODO [P3-task1]: ps, exec, kill, clear    

        /************************************************************/
        /* Do not touch this comment. Reserved for future projects. */
        /************************************************************/    
    }

    return 0;
}
