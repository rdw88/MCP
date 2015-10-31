/**
 * 
 * Project 1 - Ghost of the MCP
 * By Ryan Wise
 * CIS 415 Operating Systems (Fall '15)
 * October 31, 2015
 * 
 */


#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>


/**
 * The amount of seconds the scheduler allows each process to run for before 
 * continuing execution of the next.
 */
#define SECONDS_PER_PROCESS 1



/**
 * Begins the execution of the forked child process. Waits for the MCP
 * to give the signal to execute and loads the program with the provided arguments.
 * 
 * @param program The program to execute.
 * @param args    The program's arguments to be executed with.
 * @param parent  The MCP's Process ID.
 */
void childExecute(char *program, char **args, pid_t parent) {
	sigset_t sigset;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGUSR1);
	sigprocmask(SIG_BLOCK, &sigset, NULL);

	int sig = 0;

	int err = kill(parent, SIGCONT);
	if (err == -1) {
		fprintf(stderr, "Error signaling MCP: %s\n", strerror(errno));
		return;
	}

	int status = sigwait(&sigset, &sig);
	
	if (status == 0) {
		err = execvp(program, args);
		if (err == -1) {
			fprintf(stderr, "Error executing %s: %s\n", program, strerror(errno));
		}
	}
}


/**
 * Fetches select information about each process the MCP is running and displays
 * it to stdout.
 * 
 * @param pids    Pointer to an array of process PIDs the MCP has started.
 * @param numPids The number of PIDs in @pids.
 */
void displayProcessInfo(pid_t *pids, int numPids) {
	for (int i = 0; i < numPids; i++) {
		if (pids[i] == 0)
			continue;

		char *tracking[11] = {0};

		int file;
		char filename[64];
		snprintf(filename, sizeof(filename), "/proc/%u/status", pids[i]);
		file = open(filename, O_RDONLY);

		if (file == -1) {
			fprintf(stderr, "Error opening file %s: %s\n", filename, strerror(errno));
			continue;
		}
		
		ssize_t bytesRead = 0;
		char *contents = (char *) malloc(2048 * sizeof(char));
		char *pidStatus[46];
		bytesRead = read(file, contents, 2048);

		if (bytesRead == -1) {
			fprintf(stderr, "Error reading file %s: %s\n", filename, strerror(errno));
			free(contents);
			close(file);
			continue;
		}

		contents[bytesRead] = '\0';

		pidStatus[0] = strtok(contents, "\n");
		char *line;

		for (int k = 1; (line = strtok(NULL, "\n")) != NULL; k++) {
			pidStatus[k] = line;
		}

		tracking[0] = pidStatus[0]; // Name
		tracking[1] = pidStatus[1]; // State
		tracking[2] = pidStatus[4]; // Pid
		tracking[3] = pidStatus[12]; // VMSize
		tracking[4] = pidStatus[17]; // VMData
		tracking[5] = pidStatus[18]; // VMStk
		tracking[6] = pidStatus[23]; // Threads
		tracking[7] = pidStatus[39]; // Voluntary Context Switches
		tracking[8] = pidStatus[40]; // Nonvoluntary Context Switches

		int err = close(file);
		if (err == -1) {
			fprintf(stderr, "Error closing file %s: %s\n", filename, strerror(errno));
			free(contents);
			continue;
		}

		memset(filename, 0, 64);
		snprintf(filename, sizeof(filename), "/proc/%u/io", pids[i]);
		file = open(filename, O_RDONLY);

		if (file == -1) {
			fprintf(stderr, "Error opening file %s: %s\n", filename, strerror(errno));
			continue;
		}

		char *ioContents = (char *) malloc(256 * sizeof(char));
		bytesRead = read(file, ioContents, 256);

		if (bytesRead == -1) {
			fprintf(stderr, "Error reading file %s: %s\n", filename, strerror(errno));
			free(contents);
			free(ioContents);
			close(file);
			continue;
		}

		ioContents[bytesRead] = '\0';

		tracking[9] = strtok(ioContents, "\n");
		tracking[10] = strtok(NULL, "\n");

		err = close(file);
		if (err == -1) {
			fprintf(stderr, "Error closing file %s: %s\n", filename, strerror(errno));
			free(contents);
			free(ioContents);
			continue;
		}

		size_t nameLen = strlen(tracking[0]) - 6; // Name with "Name:\t" removed
		char name[nameLen];

		memcpy(name, &tracking[0][6], nameLen);
		name[nameLen] = '\0';

		printf("\n============ PROCESS INFO FOR %s =============\n", name);
		for (int k = 0; k < 11; k++) {
			printf("%s\n", tracking[k]);
		}
		printf("==============================================\n\n");

		free(contents);
		free(ioContents);
	}
}


/**
 * Helper function that makes the calling thread halt execution until a signal
 * has been received.
 * 
 * @param  signal The signal to wait for.
 * @return        The result of @sigwait (see man sigwait) or -1 if an error occurred.
 */
int waitSignal(int signal) {
	sigset_t sigset;

	sigemptyset(&sigset);
	sigaddset(&sigset, signal);
	sigprocmask(SIG_BLOCK, &sigset, NULL);

	int sig = 0;
	return sigwait(&sigset, &sig);
}


/**
 * Interrupt handler for the alarm(2) system call made within the scheduler. 
 * Signals the scheduler to proceed with the execution of the next child process.
 * 
 * @param signo The signal number.
 */
void onAlarm(int signo) {
	int err = kill(getpid(), SIGUSR2);

	if (err == -1) {
		fprintf(stderr, "Error signaling SIGUSR2: %s\n", strerror(errno));
	}
}


/**
 * Initializes and runs the scheduler until all child processes of the MCP
 * have finished execution.
 * 
 * @param pids    A pointer to an array of child process PIDs.
 * @param numPids The number of PIDs in @pids.
 */
void initScheduler(pid_t *pids, int numPids) {
	int err;

	for (int k = 0; k < numPids; k++) {
		err = kill(pids[k], SIGUSR1);

		if (err == -1) {
			fprintf(stderr, "Error signaling PID %u: %s\n", pids[k], strerror(errno));
			continue;
		}

		err = kill(pids[k], SIGSTOP);
		
		if (err == -1) {
			fprintf(stderr, "Error signaling PID %u: %s\n", pids[k], strerror(errno));
		}
	}

	unsigned int completedProcesses = 0;
	unsigned int infoTimer = 0;
	while (1) {
		if (infoTimer == 5) {
			displayProcessInfo(pids, numPids);
			infoTimer = 0;
		}

		for (int i = 0; i < numPids; i++) {
			if (pids[i] == 0)
				continue;

			sighandler_t sig = signal(SIGALRM, onAlarm);
			if (sig == SIG_ERR) {
				fprintf(stderr, "Error setting SIGALRM: %s\n", strerror(errno));
				continue;
			}

			alarm(SECONDS_PER_PROCESS);
			err = kill(pids[i], SIGCONT);
			if (err == -1) {
				fprintf(stderr, "Error signaling PID %u: %s\n", pids[i], strerror(errno));
				continue;
			}

			waitSignal(SIGUSR2); // The alarm interrupt handler will signal SIGUSR2 to the MCP.

			int status = waitpid(pids[i], NULL, WNOHANG);
			
			if (status == -1) {
				fprintf(stderr, "Error executing waitpid: %s\n", strerror(errno));
				continue;
			} else if (status == 0) {
				err = kill(pids[i], SIGSTOP);

				if (err == -1) {
					fprintf(stderr, "Error signaling PID %u: %s\n", pids[i], strerror(errno));
				}
			} else {
				completedProcesses ++;
				pids[i] = 0;
				printf("Process %d terminated\n", i + 1);
			}
		}

		infoTimer++;

		if (completedProcesses == numPids)
			break;
	}
}


/**
 * Parses a line of input from the input file, getting the program name
 * and its args into an array that can be passed to execvp(3).
 * 
 * @param  line A line from the input file.
 * @return      Pointer to the array of arguments to be passed to execvp(3).
 */
char **parse(char *line) {
	size_t n = strlen(line);
	int numArgs = 1;
	for (int i = 0; i < n; i++) {
		if (line[i] == ' ')
			numArgs ++;
	}

	char *program;
	char **args = (char **) malloc((numArgs + 1) * sizeof(char *));

	if (numArgs == 1) {
		program = line;
		args[0] = line;
	} else {
		char *token = strtok(line, " ");
		for (int i = 0; i < numArgs; i++) {
			args[i] = token;

			if (i == 0)
				program = token;

			token = strtok(NULL, " ");
		}
	}

	args[numArgs] = NULL;

	return args;
}


/**
 * Execute the programs defined in a file.
 * 
 * @param file An integer referencing the input file (returned from open(2)).
 */
void execute(int file) {
	char *block = (char *) malloc(1024 * sizeof(char));
	char **lines;
	size_t numPids = 1;
	ssize_t bytesRead = 0;
	pid_t *pids = 0;
	pid_t parentPid = getpid();

	while ((bytesRead = read(file, block, 1023)) > 0) {
		block[bytesRead] = '\0';
		char *line = strtok(block, "\n");
		pids = (pid_t *) malloc(sizeof(pid_t));
		lines = (char **) malloc(sizeof(char *));

		if (line == NULL) {
			lines[0] = block;
		} else {
			lines[0] = line;
		}
		
		for (int i = 1;; i++) {
			char *str = strtok(NULL, "\n");
			if (str == NULL)
				break;

			numPids++;
			pids = (pid_t *) realloc(pids, numPids * sizeof(pid_t));
			lines = (char **) realloc(lines, numPids * sizeof(char *));
			lines[i] = str;
		}

		for (int i = 0; i < numPids; i++) {
			char **args = parse(lines[i]);
			char *program = args[0];
		
			pids[i] = fork();

			if (pids[i] < 0) {
				fprintf(stderr, "Error starting process %d: %s\n", i, strerror(errno));
			} else if (pids[i] == 0) {
				childExecute(program, args, parentPid);
			} else {
				waitSignal(SIGCONT);
			}

			free(args);
		}
	}

	initScheduler(pids, numPids);

	free(block);
	free(pids);
	free(lines);
}


int main(int argc, char *argv[]) {
	if (argc < 2) {
		fprintf(stderr, "Please provide input file.\n");
		return 1;
	}

	int file;
	file = open(argv[1], O_RDONLY);

	if (file == -1) {
		fprintf(stderr, "Error opening file\n");
		return 1;
	}

	execute(file);
	close(file);

	printf("MCP Closing Successfully!\n");

	return 0;
}
