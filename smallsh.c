#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>

#define MAXCHAR 1024
#define LSH_RL_BUFSIZE 1024
#define LSH_TOK_BUFSIZE 64
#define LSH_TOK_DELIM " \t\r\n\a"

char**  tks;	
char*   line;	
int     isBackground = 0;			
int     status;			
int     token_count;
int     count = 0;
bool    background = true;


struct sigaction SIGINTAction;	// handles ^C
struct sigaction SIGTSTPAction; // handles ^Z

void append(char*, char);
void handle_SIGTSTP();
void run_command();
void childProc();
void parenrProc(pid_t childPid);
bool StartsWith(const char *a, const char *b);
void redirect();


//Functions starting with "lsh" are from the announcement.
//refernce: https://brennan.io/2015/01/16/write-a-shell-in-c/
char **lsh_split_line(char *line);
char *lsh_read_line(void);
int lsh_num_builtins();
int lsh_cd(char **args);
int lsh_status(char **args);
int lsh_exit(char **args);
int lsh_execute();

/*
* Append character at the end of the string
*/
void append(char *destination, char c){

    char *tmp = destination;

    while(*tmp != '\0') 
        tmp++; // find end of string

    *tmp = c;
    *(tmp+1) = '\0'; 

}


char *builtin_str[] = 
{
    "cd",
    "status",
    "exit"
};  


int (*builtin_func[]) (char **) = 
{
    &lsh_cd,
    &lsh_status,
    &lsh_exit
};


int lsh_num_builtins()
{
    return sizeof(builtin_str) / sizeof(char *);
}

/*
* Handle command "cd"
*/
int lsh_cd(char **args)
{
    if(args[1] == NULL)
    {
        chdir(getenv("HOME"));
    } 

    else 
    {
        if(chdir(args[1]) != 0)
        {
            perror("lsh");
        }
    }

    return 1;

}

/*
* Handle "status command"
*/
int lsh_status(char **args)
{
    printf("terminated by signal  %d\n", status);
    fflush(stdout);
    return 1;
}

/*
* Exits the program when user enters exit
*/
int lsh_exit(char **args)
{
    exit(1);
}

/*
* Handles ^z, enters and exits foregound mode only mode
*/
void handle_SIGTSTP(int signo)
{
    if(signo == SIGTSTP)
    {
        if(count % 2 == 0)
        {
            char* message = "\nEntering foreground-only mode (& is now ignored)\n";
            write(STDOUT_FILENO, message, 50);
            fflush(stdout);
            background = false;
        }

        else if(count % 2 == 1)
        {
            char* message = "\nExiting foreground-only mode\n";
            write (STDOUT_FILENO, message, 30);
            fflush(stdout);
            background = true;
        }

        count++;

    }

    write(STDOUT_FILENO, ": ", 2);
}


/*
* Read command that user entered
*/
char *lsh_read_line(void)
{
    int c;
    int position = 0;
    int bufsize = LSH_RL_BUFSIZE;
    char *buffer = malloc(sizeof(char) * bufsize);

    printf(": ");
    fflush(stdout);
    
    if (!buffer) 
    {
        fprintf(stderr, "lsh: allocation error\n");
        exit(EXIT_FAILURE);  
    }

    while (1) 
    {
        // Read a character
        c = getchar();
        // If we hit EOF, replace it with a null character and return.
        if (c == EOF || c == '\n') 
        {
            buffer[position] = '\0';
            return buffer;
        } 
        else 
        {
            buffer[position] = c;   
        }
    
        position++;

        // If we have exceeded the buffer, reallocate.
        if (position >= bufsize) 
        {
            bufsize += LSH_RL_BUFSIZE;
            buffer = realloc(buffer, bufsize);

            if (!buffer) 
            {
                fprintf(stderr, "lsh: allocation error\n");
                exit(EXIT_FAILURE);
            }
        }
    }
}


/*
* Tokenize user inputs
*/
char **lsh_split_line(char *line)
{
    int bufsize = LSH_TOK_BUFSIZE, position = 0;
    char **tokens = malloc(bufsize * sizeof(char*));
    char *token;
    char s1[10]; 

    sprintf(s1, "%d", getpid());

    if (!tokens) 
    {
        fprintf(stderr, "lsh: allocation error\n");
        exit(EXIT_FAILURE);
    }

    token = strtok(line, LSH_TOK_DELIM);

    //changes "$$" into pid
    while (token != NULL) 
    {
        char* tmp = malloc(MAXCHAR*sizeof(char));
        
        for(int i=0; i<strlen(token); i++)
        {
            if(token[i] == '$' && token[i+1] == '$')
            {
                strcat(tmp,s1);
                i++;
            }
            else
            {
                append(tmp,token[i]);
            }
        } 

        tokens[position] = tmp;
        position++;

        if (position >= bufsize) 
        {
            bufsize += LSH_TOK_BUFSIZE;
            tokens = realloc(tokens, bufsize * sizeof(char*));

            if (!tokens)
            {
                fprintf(stderr, "lsh: allocation error\n");
                exit(EXIT_FAILURE);
            }
        }

        token = strtok(NULL, LSH_TOK_DELIM);
    }
  
    tokens[position] = NULL;

    //stores number of argument in token_count
    token_count = position;
    
    return tokens;
}

/*
* Check if the string starts with specific character
*/
bool StartsWith(const char *a, const char *b)
{
    if(strncmp(a, b, strlen(b)) == 0) 
        return 1;

    return 0;
}

/*
* Accepts the argument user have typed and see if its built-in command, if not go to run_command
*/
int lsh_execute()
{
    int i;
    
    // Accepts argument starting wiht '#' as a comment
    if(token_count == 0 || StartsWith(tks[0], "#") == 1)
    {
        return 1;
    }

    for (i = 0; i < lsh_num_builtins(); i++) 
    {
        if (strcmp(tks[0], builtin_str[i]) == 0) 
        {
            return (*builtin_func[i])(tks);
        }
    }

    run_command();
}

/*
* Fork a child porcess and runs command 
*/
void run_command() 
{
	pid_t pid;				
	isBackground = 0;
    int buff_size = 100;
    char* tmp[100];

	// If there is a '&' at the end of the command, run in background
    if(strcmp(tks[token_count-1], "&") == 0) 
    {
    	isBackground = 1;
    	// Ignore '&'
    	tks[token_count - 1] = NULL;
    }

	pid = fork();					

	switch(pid) 
    {
		case -1: // Error
			perror("fork() failed\n");
			exit(1);
			break;

		case 0:  // Child Process
            signal(SIGINT, SIG_DFL); // Accept ^C
			childProc();
			break;

		default: // Parent Process
			parenrProc(pid);
	}

    //Catch if the process has exited normally of terminated by signal
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        if(WIFEXITED(status))
        {
            printf("background pid %d is done: exit value %d\n", pid, WEXITSTATUS(status));
            fflush(stdout);
        } 
        else
        {
            printf("background pid %d is done: terminated by signal %d\n", pid, WTERMSIG(status));
            fflush(stdout);
        }
    }
}


/*
* Hanlde standard input and output
*/
void redirect()
{
    int fd, fd1;
    int result;
    char *p[] = {tks[0], NULL};

    // if argument has both '<' and '>'
    if(token_count == 5)
    {
        if(strcmp(tks[3],">") == 0)
        {
            fd = open(tks[4], O_WRONLY | O_TRUNC | O_CREAT, 770);

            if(fd == -1)
            {
                perror("failed open");
            }

            result = dup2(fd, STDOUT_FILENO);

            if(result == -1)
            {
                perror("failed dup2");
                exit(2);
            };

            close(fd); 
        }

        if(strcmp(tks[1],"<") == 0)
        {
            fd1 = open(tks[2], O_RDONLY, 770);

            if(fd1 == -1)
            {
                perror("failed open");
            }

            result = dup2(fd1, STDIN_FILENO);

            if(result == -1)
            {
                perror("failed dup2");
                exit(2);
            };

            close(fd1); 
            execvp(tks[0], p);
            perror(tks[0]);
        }
    }
  
    // if argument has only '<' or '>'
    if(token_count == 3)
    {
        if(strcmp(tks[1],">") == 0)
        {
            fd = open(tks[2], O_WRONLY | O_TRUNC | O_CREAT, 770);

            if(fd == -1)
            {
                perror("failed open");
            }

            result = dup2(fd, STDOUT_FILENO);

            if(result == -1)
            {
                perror("failed dup2");
                exit(2);
            };

            close(fd); 
            execvp(tks[0], p);
            perror(tks[0]);
        }

        if(strcmp(tks[1],"<") == 0)
        {
            fd = open(tks[2], O_RDONLY, 0777);

            if(fd == -1)
            {
                perror(tks[2]);
            }

            result = dup2(fd, STDIN_FILENO);

            if(result == -1)
            {
                perror("failed dup2");
                exit(2);
            };

            close(fd); 
            execvp(tks[0], p);
            perror(tks[0]);
        }
    }
}

/*
* child process
*/
void childProc() 
{
    redirect();
    // execute function
	if(execvp(tks[0], tks) == -1 )
    {
        perror(tks[0]);
        exit(1); 
    }
}


/*
* parnet process
*/
void parenrProc(pid_t childPid) 
{
    // running background process using WNOHANG (Does not wait until thr child process ends)
	if(isBackground == 1 && background == 1) 
    {
		waitpid(childPid, &status, WNOHANG);
		printf("background pid is %d\n", childPid);
		fflush(stdout); 
	}

    // running in foreground process, waits until the child process ends
	else 
    {
		waitpid(childPid, &status, 0);

        if(WIFEXITED(status)) // check if the child was terminated normally
        {
            status = WEXITSTATUS(status);
        }

        else if(WIFSIGNALED(status)) // check if the child was terminated abnormally
        {
            printf("terminated by signal %d\n",WTERMSIG(status));
            fflush(stdout);
            status = WTERMSIG(status);
        } 

        else
        {
            status = WTERMSIG(status);
        }
	}
}

int main() 
{					
	// // Handle CTRL-Z
	SIGTSTPAction.sa_handler = handle_SIGTSTP; 	
    SIGTSTPAction.sa_flags = SA_RESTART; 		
    sigfillset(&SIGTSTPAction.sa_mask);			
    sigaction(SIGTSTP, &SIGTSTPAction, NULL);	

    // Handle CTRL-C
    SIGINTAction.sa_handler=SIG_IGN;			
    sigfillset(&SIGINTAction.sa_mask); 			
    sigaction(SIGINT, &SIGINTAction, NULL);		
	
	while(1) 
    {
        line = lsh_read_line();
        tks = lsh_split_line(line);
        lsh_execute();			
	}

	return 0;
}
