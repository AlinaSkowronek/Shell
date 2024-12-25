// 
// tsh - A tiny shell program with job control
// 
// <Put your name and login ID here>
//

using namespace std;

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string>

#include "globals.h"
#include "jobs.h"
#include "helper-routines.h"

//
// Needed global variable definitions
//

static char prompt[] = "tsh> ";
int verbose = 0;

//
// You need to implement the functions eval, builtin_cmd, do_bgfg,
// waitfg, sigchld_handler, sigstp_handler, sigint_handler
//
// The code below provides the "prototypes" for those functions
// so that earlier code can refer to them. You need to fill in the
// function bodies below.
// 

void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

//
// main - The shell's main routine 
//
int main(int argc, char **argv) 
{
  int emit_prompt = 1; // emit prompt (default)

  //
  // Redirect stderr to stdout (so that driver will get all output
  // on the pipe connected to stdout)
  //
  dup2(1, 2);

  /* Parse the command line */
  char c;
  while ((c = getopt(argc, argv, "hvp")) != EOF) {
    switch (c) {
    case 'h':             // print help message
      usage();
      break;
    case 'v':             // emit additional diagnostic info
      verbose = 1;
      break;
    case 'p':             // don't print a prompt
      emit_prompt = 0;  // handy for automatic testing
      break;
    default:
      usage();
    }
  }

  //
  // Install the signal handlers
  //

  //
  // These are the ones you will need to implement
  //
  Signal(SIGINT,  sigint_handler);   // ctrl-c
  Signal(SIGTSTP, sigtstp_handler);  // ctrl-z
  Signal(SIGCHLD, sigchld_handler);  // Terminated or stopped child

  //
  // This one provides a clean way to kill the shell
  //
  Signal(SIGQUIT, sigquit_handler); 

  //
  // Initialize the job list
  //
  initjobs(jobs);

  //
  // Execute the shell's read/eval loop
  //
  for(;;) {
    //
    // Read command line
    //
    if (emit_prompt) {
      printf("%s", prompt);
      fflush(stdout);
    }

    char cmdline[MAXLINE];

    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)) {
      app_error("fgets error");
    }
    //
    // End of file? (did user type ctrl-d?)
    //
    if (feof(stdin)) {
      fflush(stdout);
      exit(0);
    }

    //
    // Evaluate command line
    //
    eval(cmdline);
    fflush(stdout);
    fflush(stdout);
  } 

  exit(0); //control never reaches here
}
  
/////////////////////////////////////////////////////////////////////////////
//
// eval - Evaluate the command line that the user has just typed in
// 
// If the user has requested a built-in command (quit, jobs, bg or fg)
// then execute it immediately. Otherwise, fork a child process and
// run the job in the context of the child. If the job is running in
// the foreground, wait for it to terminate and then return.  Note:
// each child process must have a unique process group ID so that our
// background children don't receive SIGINT (SIGTSTP) from the kernel
// when we type ctrl-c (ctrl-z) at the keyboard.
//
void eval(char *cmdline) 
{
  /* Parse command line */
  //
  // The 'argv' vector is filled in by the parseline
  // routine below. It provides the arguments needed
  // for the execve() routine, which you'll need to
  // use below to launch a process.
  //
  char *argv[MAXARGS];

  //
  // The 'bg' variable is TRUE if the job should run
  // in background mode or FALSE if it should run in FG
  //
  int bg = parseline(cmdline, argv); //check if it ends with an &
  if (argv[0] == NULL)  
    return;   /* ignore empty lines */

  int isBultin = builtin_cmd(argv);

  if(!isBultin) //checks if command is built in or not (non built in)
  {
    //child
    int pid = fork(); //process ID for child -> create a child process

    if(pid == 0) // if we are in the child process
    {
      setpgid(0,0); //make sure child has a unque proccess ID, setting it to its own process group

      //error handling if command is not found
      int statusOfExecv = execv(argv[0], argv);
      if(statusOfExecv < 0)
      {
        printf("%s: Command not found\n", argv[0]);
        exit(1); //exit due to an error (1 -> error status)
      }

    }

    int jobState;
      if(bg)
      {
        jobState = BG;
      }
      else
      {
        jobState = FG;
      }
      addjob(jobs, pid, jobState, cmdline); // add job to job list

    //if not a background process
    if(!bg)
    {
      waitfg(pid);
    }
    else
    {
      //print job
      printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline); // job id, process id, jobid, the command
    }

  }

  return;
}


/////////////////////////////////////////////////////////////////////////////
//
// builtin_cmd - If the user has typed a built-in command then execute
// it immediately. The command name would be in argv[0] and
// is a C string. We've cast this to a C++ string type to simplify
// string comparisons; however, the do_bgfg routine will need 
// to use the argv array as well to look for a job number.
//
int builtin_cmd(char **argv) 
{
  string cmd(argv[0]);

  // built in command quit
  if (strcmp(argv[0], "quit") == 0) 
  {
    exit(0); 
  }

  if (strcmp(argv[0], "jobs") == 0) 
  {
    listjobs(jobs);
    return 1;
  }

  if(strcmp(argv[0], "bg") == 0) //check for bg command
  {
    do_bgfg(argv); //do the bg handling
    return 1; //builtin command was executed
  }

  if(strcmp(argv[0], "fg") == 0) //check for fg command
  {
    do_bgfg(argv); //do the fg handling
    return 1; //builtin command was executed
  }

  return 0;     /* not a builtin command */
}

/////////////////////////////////////////////////////////////////////////////
//
// do_bgfg - Execute the builtin bg and fg commands
//
void do_bgfg(char **argv) 
{
  struct job_t *jobp=NULL;
    
  /* Ignore command if no argument */
  if (argv[1] == NULL) {
    printf("%s command requires PID or %%jobid argument\n", argv[0]);
    return;
  }
    
  /* Parse the required PID or %JID arg */
  if (isdigit(argv[1][0])) {
    pid_t pid = atoi(argv[1]);
    if (!(jobp = getjobpid(jobs, pid))) {
      printf("(%d): No such process\n", pid);
      return;
    }
  }
  else if (argv[1][0] == '%') {
    int jid = atoi(&argv[1][1]);
    if (!(jobp = getjobjid(jobs, jid))) {
      printf("%s: No such job\n", argv[1]);
      return;
    }
  }	    
  else {
    printf("%s: argument must be a PID or %%jobid\n", argv[0]);
    return;
  }

  //
  // You need to complete rest. At this point,
  // the variable 'jobp' is the job pointer
  // for the job ID specified as an argument.
  //
  // Your actions will depend on the specified command
  // so we've converted argv[0] to a string (cmd) for
  // your benefit.
  //
  string cmd(argv[0]);

  //bg logic
  if(strcmp(argv[0], "bg") == 0) //check for the bg command
  {
    kill(-(jobp -> pid), SIGCONT); //send the continue signal to job
    jobp -> state = BG; //set the job state to BG
    printf("[%d] (%d) %s", jobp ->jid, jobp->pid, jobp ->cmdline);
  }

  //fg logic
  if(strcmp(argv[0], "fg") == 0) //check for the bg command
  {
    kill(-(jobp -> pid), SIGCONT); //send the continue signal to job
    jobp -> state = FG; //set the job state to FG
    waitfg(jobp -> pid); //wait for foreground job to complete
  }

  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// waitfg - Block until process pid is no longer the foreground process
//
void waitfg(pid_t pid)
{
  struct job_t *job = getjobpid(jobs, pid);

// execute if the foreground job has already completed and been reaped by the handler
//if there is no job return
  if(!job)
  {
    return;
  }
// if a job does exist
// access its pid and check if its state if a FG ->sleep
  while(job -> pid == pid && job -> state == FG)
  {
    sleep(.1);
  }
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// Signal handlers
//


/////////////////////////////////////////////////////////////////////////////
//
// sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
//     a child job terminates (becomes a zombie), or stops because it
//     received a SIGSTOP or SIGTSTP signal. The handler reaps all
//     available zombie children, but doesn't wait for any other
//     currently running children to terminate.  
//
void sigchld_handler(int sig) 
{
  pid_t child_pid; //child process ID
  int status_of_waitpid; //track status of waitpid
  int isMoreChildren = 1; //check if there are more than one child

  while(isMoreChildren) //check all child processes
  {
    child_pid = waitpid(-1, &status_of_waitpid, WNOHANG|WUNTRACED); //WUNTRACED for a stopped process
    isMoreChildren = child_pid > 0;

    //handle stopped children

    if(WIFSTOPPED(status_of_waitpid)) //if child is stopped
    {
      struct job_t *j = getjobpid(jobs, child_pid); //get the job from list
      if(j) //if job exists
      {
        j -> state = ST; //set state to stopped
        fprintf(stdout,"Job [%d] (%d) stopped by signal %d\n", pid2jid(child_pid), child_pid, WSTOPSIG(status_of_waitpid));
      }
    }

    //when a process normally exits out (handling exited children)
     else if(WIFEXITED(status_of_waitpid))
    {
      deletejob(jobs, child_pid);
    }

    //trace 6
    //if theres a signal process (handling the signal children)
  else if(WIFSIGNALED(status_of_waitpid)) //if child was not signaled
  {
    int child_jid = pid2jid(child_pid); //get job id
    int isSuccessful = deletejob(jobs, child_pid); // delte job
    
    if(isSuccessful) //if deleted
    {
      fprintf(stdout,"Job [%d] (%d) terminated by signal %d\n", child_jid, child_pid, WTERMSIG(status_of_waitpid));
    }

  }

  }
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigint_handler - The kernel sends a SIGINT to the shell whenver the
//    user types ctrl-c at the keyboard.  Catch it and send it along
//    to the foreground job.  
//
void sigint_handler(int sig) 
{
  pid_t pid = fgpid(jobs); // get PID of the foreground jobs
  if(pid > 0) //if there is a FG job
  {
    kill(-pid, SIGINT); //then send the SIGINT signal to it
  }
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
//     the user types ctrl-z at the keyboard. Catch it and suspend the
//     foreground job by sending it a SIGTSTP.  
//
void sigtstp_handler(int sig) 
{
  pid_t pid = fgpid(jobs); //get PID of the foreground jobs
  if (pid > 0) //if there is a foreground job
  {
    kill(-pid, SIGTSTP); // then send the SIGTSTP signal
  }
  return;
}

/*********************
 * End signal handlers
 *********************/




