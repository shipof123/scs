#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <limits.h>

#include <pwd.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <glob.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <editline/readline.h>
#include <histedit.h>

/*
    TODO:
        implement lua
*/
/*GLOBALS*/
lua_State* L;

/*
* MACROS for tokenizer
*/
#define SH_TOK_BUFFSIZE 64
#define SH_TOK_DELIM " \t\r\n\a"


/*
    Logging functions
*/
FILE* f;
void log_entry(FILE** f, char* entry){
    struct passwd* pw;
    uid_t uid;
    int c;

    uid = geteuid();
    pw = getpwuid(uid);

    fprintf(*f, "UID:%u Username:%s Entry:%s\n", uid, pw->pw_name , entry);
}

void sh_printf(char* str, ...){
    va_list v;
    char* msg;

    va_start(v, str);
    vsprintf(msg,str,v);
    fprintf(f, msg);
    printf(msg);
}
#define sh_puts(s) sh_printf("%s\n", s)

/*
Builtin funcs
*/

int sh_cd(char** args);
int sh_help(char** args);
int sh_glob(char** args);
int sh_lua(char** args);
int sh_stat(char** args);
int sh_exit(char** args);

/*
    names + func pointers
*/

char* builtin_name[] = {
    "cd",
    "help",
    "glob",
    "sh_stat",
    "~",
    "exit"
};

int (*builtin_func[]) (char**) = {
    &sh_cd,
    &sh_help,
    &sh_glob,
    &sh_stat,
    &sh_lua,
    &sh_exit
};

#define sh_num_builtins() (sizeof(builtin_name) / sizeof(char*))

/*
    IMPL
*/

int sh_glob(char** args){
    glob_t globbuf;

    globbuf.gl_offs = 10;

    if(args[1] == NULL){
        fprintf(stderr, "Expected a glob to process\n");
        return 1;
    }

    glob(args[1], GLOB_MARK, NULL, &globbuf);
    for(int i = 0; i < globbuf.gl_pathc; i++)
        sh_puts(globbuf.gl_pathv[i]);
    return 1;
}

int sh_lua(char** args){
    if(args[1] == NULL){
        fprintf(stderr, "scs: expected >= 1 argument to embedded lua shell\n");
    }
    char* buff = args[1];
    for (size_t i = 2; args[i]; i++) {
        strcat(buff, args[i]);
    }

    if(luaL_loadbuffer(L, buff, strlen(buff), "scs") || lua_pcall(L,0,0,0)){
        fprintf(stderr, "%s\n", lua_tostring(L, -1));
        lua_pop(L, 1);  /* pop error message from the stack */
    }
    return 1;
}

int sh_cd(char** args){
    if(args[1] == NULL){
        fprintf(stderr, "scs: expected argument to \"cd\"\n");
    } else {
        if(chdir(args[1]) != 0){
            perror("scs");
        }
    }
    return 1;
}

int sh_help(char** args){
    int i = 0;

    sh_puts("\x1b[H\x1b[J");
    sh_puts("scs v1.0:");
    sh_puts("-----Simple crappy Shell-----\n");

    for(i = 0; i < sh_num_builtins(); i++){
        sh_puts(builtin_name[i]);
    }
    sh_puts("Any questions? RTFMP");
}

int sh_stat(char** args){
    if(args[1] == NULL){
        fprintf(stderr, "%s\n", "Expected arg for builtin sh_stat");
        return 1;
    }
    struct stat s;
    stat(args[1], &s);

    sh_printf("dev id:%u\tinode:%u\n", s.st_dev, s.st_ino);
    sh_printf("mode:%d\t# of hard links: %d\n", s.st_mode, s.st_nlink);

    return 1;
}

int sh_exit(char** args){
    return 0;
}

void sh_main_loop();

int main(int argc, char const *argv[]) {
    L = luaL_newstate();
    luaL_openlibs(L);

    sh_main_loop();


    return EXIT_SUCCESS;
}

char** sh_split_line(char*);

void sh_main_loop(){
    char* line;
    char** args;
    int status;

    f = fopen(".scs_history", "a");

    do {
        char cwd[PATH_MAX];
        char prompt[PATH_MAX];

        /* Preparing prompt */

        getcwd(cwd, sizeof(cwd));
        sprintf(prompt, "%s$ ", cwd);


        line = readline(prompt);
        add_history(line);
        /* log it */
        log_entry(&f,line);
        //log_entry(&stdout, line);
        //log_entry_stdout(line);

        args = sh_split_line(line);
        status = sh_execute(args);

        free(line);
        free(args);
    } while(status);
    fclose(f);
}

char** sh_split_line(char* line){
    int buffsize = SH_TOK_BUFFSIZE;
    int pos = 0;

    char** tokens = malloc(buffsize * sizeof(char*));
    char* token;

    if(!tokens){
        perror("scs");
        exit(EXIT_FAILURE);
    }

    token = strtok(line, SH_TOK_DELIM);

    while(token){
        tokens[pos] = token;
        pos++;

        if(pos >= buffsize){
            buffsize += SH_TOK_BUFFSIZE;
            tokens = realloc(tokens, buffsize * sizeof(char*));
            if(!tokens){
                perror("scs");
                exit(EXIT_FAILURE);
            }
        }

        token = strtok(NULL, SH_TOK_DELIM);
    }
    tokens[pos] = NULL;
    return tokens;
}

int sh_execute(char** args){
    int i;

    if(args[0] == NULL){
        /* empty command */
        return 1;
    }

    for(i = 0; i < sh_num_builtins(); i++){
        if(strcmp(args[0], builtin_name[i]) == 0)
            return (*builtin_func[i])(args);
    }

    return sh_launch(args);
}

int sh_launch(char** args){
    pid_t pid, wpid;
    int status;

    pid = fork();
    if(pid == 0){
        /* child process */

        if(execvp(args[0], args) == -1){
            perror("scs");
        }
        exit(EXIT_FAILURE);
    } else if(pid < 0){
        perror("scs");
    } else {
        // parent
        do {
            wpid = waitpid(pid, &status, WUNTRACED);
        } while(!WIFEXITED(status) && !WIFSIGNALED(status));
    }
    return 1;
}
