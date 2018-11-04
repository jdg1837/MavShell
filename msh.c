/*
	Name: Juan Diego Gonzalez German
	ID: 1001401837
*/


#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <signal.h>

#define WHITESPACE " \t\n"		// We want to split our command line up into tokens
                                		// so we need to define what delimits our tokens.
                               			// In this case white space and tabs
                                		// will separate the tokens on our command line

#define MAX_COMMAND_SIZE 255	// The maximum command-line size

#define MAX_NUM_ARGUMENTS 11	// Mav shell only supports one command plus 10 arguments at a time

pid_t suspended = 0; //this variable will hold pid of last spawned child, to resume it if necessary, we initialize to zero, as no children has been suspended yet

void parse_input(char**, char*);				//foo to parse input and tokenize it into array of cmd + parameters
void update_history(char**, char*, int);		//updates array of previous commands typed in
void update_pids(int[15], int, int);			//updates array of previously spawned pids
void handle_signal(){}						//handler for SIGTSTP and SIGINT in parent, designed to ignore them

int main()
{
	//multi-use counter variable
	int i;

	//Allocate memory to get input from the user
	char* cmd_str = (char*) malloc( MAX_COMMAND_SIZE );

	//set up array of ints to store up to 15 PIDs, initialize all to 0. 
	//initialize pid counter to 0. This will track what is the next array position to fill
	int pids[15];
	for(i = 0; i < 15; i++)
		pids[i] = 0;
	int pid_ctr = 0;

	//we allocate enough memory to hold 15 input strings
	//initialize history counter to 0. This will track what is the next array position to fill
	char* history[15];
	for(i = 0; i < 15; i++)
		history[i] = (char*)malloc(MAX_COMMAND_SIZE);	
	int hist_ctr = 0;

	//we define act as a sigaction, and set the foo handle_signal as its handler
	//act will point to the action parent will take when certain signals are raised
	struct sigaction act;
	memset(&act, '\0', sizeof(act));
	act.sa_handler = &handle_signal;
	
	//set act as the sigaction for SIGINT or SIGTSTP. If it fails, we terminate program
  	if (sigaction(SIGINT , &act , NULL) < 0)
	{
		perror ("sigaction: ");
		return 1;
  	}
  	if (sigaction(SIGTSTP , &act , NULL) < 0)
	{
		perror ("sigaction: ");
		return 1;
  	}

	//We loop infinitely, until user types exit commands
	while( 1 )
	{
		// Print out the msh prompt
		printf ("msh> ");

		// Read the command from the commandline.
		// Maximum command that will be read is MAX_COMMAND_SIZE
		// This while command will wait here until the user
		// inputs something since fgets returns NULL when there
		// is no input
		while( !fgets (cmd_str, MAX_COMMAND_SIZE, stdin) );

		//Check if input is a blank line; if it is, we loop and ask for input again
		//if at least one char is not a blank space, then string is valid
		//note: non-empty strings with leading spaces are NOT valid
		int is_valid = 0;

		for(i = 0; i < strlen(cmd_str) -1; i++)
			if(cmd_str[i] != ' ' && cmd_str[i] != '\t' && cmd_str[i] != '\n')
				is_valid = 1;

		if(is_valid == 0)
			continue;

		//define array for tokenized input
		char* token[MAX_NUM_ARGUMENTS];                       
	
		//call function to tokenize input string		                                   
		parse_input(token, cmd_str);

		//if the command is to re-run nth command in history, 
		//we parse number to int and re-tokenize the input, if existing, at that point in history
		//if number not an int, or not in valid range, we loop back to msh> prompt
		if(token[0][0] == '!')
		{
			token[0][0] = '0';
			int n = atoi(token[0]);

			if((n <= 0) || (n > 15))
			{
				printf("error: n must be in range 1-15.\n");
				continue;
			}
			else if(n > hist_ctr)
			{
				printf("Command not in history.\n");
				continue;
			}
			else
			{
				char* token2[MAX_NUM_ARGUMENTS]; 
				parse_input(token2, history[n-1]);
				*token = *token2;
			}
		}

		//We store input string in history array
		//and we update hist_ctr to the next stop to be filled
		//We must do this after checking for the ! cmd
		//or ! will run wrong command
		update_history(history, cmd_str, hist_ctr);
		hist_ctr++;

		// If cmd is to quit shell, we exit loop
		if(strcmp(token[0],"quit") == 0 || strcmp(token[0], "exit") ==0)		
			break;

		// If cmd is to change directory, we do so from main thread
		else if(strcmp(token[0],"cd") == 0)
		{	
			if(chdir(token[1]) == -1)
				printf("Directory could not be changed. Please verify path.\n");
		}

		//bg will background the last spawned process, if possible
		else if(strcmp(token[0],"bg") == 0)
		{	
			kill(suspended, SIGCONT);
		}

		//listpids list the pids of all children, if they are fewer than 15
		//if there are more than that, the last 15 will be shown
		else if(strcmp(token[0], "listpids") == 0)
		{
			int j = 15;

			if(pid_ctr < 15)
				j = pid_ctr;

			for(i = 0; i < j; i++)
				printf("%d: %d\n", i+1, pids[i]);
		}

		//history list the last 15 commands
		//if fewer than 15, print all
		else if(strcmp(token[0], "history") == 0)
		{
			int j = 15;

			if(hist_ctr < 15)
				j = hist_ctr;

			for(i = 0; i < j; i++)
				printf("%d: %s", i+1, history[i]);
		}	
		
		//if the command is none of these, we assume it is an exec call and treat it as such
		else
		{
			//we fork to execute the command in the child process
			pid_t pid = fork();
	
			int status;

			//if pid == -1, forking failed and we exit
			if(pid < 0)
			{
				printf("Thread creation attempt failed. Program will exit now\n");
				exit(EXIT_FAILURE);
			}

			//if pid is a positive number, it is the child pid, and we are in parent process
			//we update pid as the latest pid, and add it to the pidlist
			//we wait for child to exit, then we loop again
			else if(pid > 0)
			{
				update_pids(pids, pid, pid_ctr);
				pid_ctr++;
				wait(&status);
			}

			//if pid == 0, we are in child. We run command as an exec call
			else
			{	
				suspended = getpid(); //since child could be suspended, we save the pid as the one to continue

				//Define paths to search commands in, in the order we will search them
				char* path1 = (char*)malloc(MAX_COMMAND_SIZE + 15);
				strcpy(path1,"./");
				strcat(path1, token[0]);

				char* path2= (char*)malloc(MAX_COMMAND_SIZE + 15);
				strcpy(path2,"/usr/local/bin/");
				strcat(path2, token[0]);

				char* path3= (char*)malloc(MAX_COMMAND_SIZE + 15);
				strcpy(path3,"/usr/bin/");
				strcat(path3, token[0]);
	
				char* path4= (char*)malloc(MAX_COMMAND_SIZE + 15);
				strcpy(path4, "/bin/");
				strcat(path4, token[0]);

				//we try to execute command from each path. We try the next option only if current ont fails
				//if no option works, we treat the command as invalid
				if(execv(path1, token) == -1)
					if(execv(path2, token) == -1)
						if(execv(path3, token) == -1)
							if(execv(path4, token) == -1)
								printf("%s: Command not found.\n", token[0]);

				//whatever the result of the exec call, we free allocated memory and termiante child process
				free(path1); free(path2); free(path3); free(path4);
				suspended = 0;	//since we got here, process has been terminated, so we remove it from the bg queue
				exit(EXIT_SUCCESS);
			}
		}
	
	}

	//once loop has been exited, we free memory and exit program
	for(i = 0; i < 15; i++)
		free(history[i]);
	free(cmd_str);

	return 0;
}

//This function takes the command string
//tokenizes it, using whitespaces as parameter
//and puts the tokens in the token array
void parse_input(char** token, char* cmd_str)
{
	int token_count = 0;

	// Pointer to point to the token
	// parsed by strsep
	char* arg_ptr;                                         
			                                   
	char* working_str  = strdup( cmd_str );                

	// we are going to move the working_str pointer so
	// keep track of its original value so we can deallocate
	// the correct amount at the end
	char* working_root = working_str;

	// Tokenize the input stringswith whitespace used as the delimiter
	while ( ( (arg_ptr = strsep(&working_str, WHITESPACE ) ) != NULL) && 
			(token_count < MAX_NUM_ARGUMENTS))
	{
		token[token_count] = strndup( arg_ptr, MAX_COMMAND_SIZE );
		
		if( strlen( token[token_count] ) == 0 )
		{
			token[token_count] = NULL;
		}
		token_count++;
	}

	free( working_root );

	// We force NULL-termination to each parameter passed
	int token_index  = 0;

	for( token_index = 0; token_index < token_count; token_index ++ ) 
	{
		if(token[token_index] == NULL)
			continue;
	
		strcat(token[token_index], "\0");
	}

	return;
}

//these functions uddate the lists of previous commands and child pids, respectivelly
//they take the array to be updated, the element to be added in, and the next position to be filled
//for both update functions: if the counter, aka next position to fill, is less than 
//max possible position, we place new element at that postion
//else, we shift elements and place new one at last possible location

void update_history(char** history, char* cmd_str, int hist_ctr)
{
	int j;

	if(hist_ctr < 15)
	{
		strcpy(history[hist_ctr], cmd_str);
	}

	else
	{
		for(j = 0; j < 14; j++)
		{	
			strcpy(history[j], history[j+1]);
		}
		strcpy(history[14], cmd_str);
	}

	return;
}

void update_pids(int pids[15], int pid, int pid_ctr)
{
	int j;

	if(pid_ctr < 15)
	{
		pids[pid_ctr] = pid;
	}

	else
	{
		for(j = 0; j < 14; j++)
		{	
			pids[j] = pids[j+1];
		}
		pids[14] = pid;
	}

	return;
}
