#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>


#define SECONDS_PER_PROCESS 1


int childExecute(char *program, char **args, pid_t parent) {
	sigset_t sigset;
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGUSR1);
	sigprocmask(SIG_BLOCK, &sigset, NULL);
	int sig = 0;

	kill(parent, SIGCONT);
	//printf("Process %s is now waiting\n", program);

	int status = sigwait(&sigset, &sig);
	
	if (status == 0) {
		status = execvp(program, args);
		if (status < 0) {
			fprintf(stderr, "Failed to exec %s: ", program);
			printf("%s\n", strerror(errno));
		}
	}

	return status;
}


void displayProcessInfo(pid_t *pids, int numPids) {
	for (int i = 0; i < numPids; i++) {
		if (pids[i] == 0)
			continue;

		char *tracking[11] = {0};

		int file;
		char filename[64];
		snprintf(filename, sizeof(filename), "/proc/%u/status", pids[i]);
		file = open(filename, O_RDONLY);
		
		ssize_t bytesRead = 0;
		char *contents = (char *) malloc(2048 * sizeof(char));
		char *pidStatus[46];
		bytesRead = read(file, contents, 2048);
		contents[bytesRead] = '\0';

		pidStatus[0] = strtok(contents, "\n");
		char *line;

		for (int k = 1; (line = strtok(NULL, "\n")) != NULL; k++) {
			pidStatus[k] = line;
		}

		tracking[0] = pidStatus[0]; // Name
		tracking[1] = pidStatus[1]; // State
		tracking[2] = pidStatus[4]; // Pid
		tracking[3] = pidStatus[16]; // VMSize
		tracking[4] = pidStatus[21]; // VMData
		tracking[5] = pidStatus[22]; // VMStk
		tracking[6] = pidStatus[28]; // Threads
		tracking[7] = pidStatus[44]; // Voluntary Context Switches
		tracking[8] = pidStatus[45]; // Nonvoluntary Context Switches

		close(file);

		memset(filename, 0, 64);
		snprintf(filename, sizeof(filename), "/proc/%u/io", pids[i]);
		file = open(filename, O_RDONLY);
		char *ioContents = (char *) malloc(256 * sizeof(char));
		bytesRead = read(file, ioContents, 256);
		ioContents[bytesRead] = '\0';

		tracking[9] = strtok(ioContents, "\n");
		tracking[10] = strtok(NULL, "\n");

		close(file);

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


void waitSignal(int signal) {
	sigset_t sigset;
	sigemptyset(&sigset);
	sigaddset(&sigset, signal);
	sigprocmask(SIG_BLOCK, &sigset, NULL);
	int sig = 0;
	sigwait(&sigset, &sig);
}


void onAlarm(int signo) {
	kill(getpid(), SIGUSR2);
}


void initScheduler(pid_t *pids, int numPids) {
	for (int k = 0; k < numPids; k++) {
		kill(pids[k], SIGUSR1);
		kill(pids[k], SIGSTOP);
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

			signal(SIGALRM, onAlarm);
			alarm(SECONDS_PER_PROCESS);
	//		printf("Process %d running\n", i + 1);
			kill(pids[i], SIGCONT);
			waitSignal(SIGUSR2);

			int status = waitpid(pids[i], NULL, WNOHANG);

			if (status == 0) {
				kill(pids[i], SIGSTOP);
	//			printf("Process %d paused\n", i + 1);
			} else {
				completedProcesses ++;
				pids[i] = 0;
				printf("Process %d terminated\n", i + 1);
			}
		}

	//	printf("%d completed processes\n", completedProcesses);
		infoTimer++;

		if (completedProcesses == numPids)
			break;
	}
}


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
				fprintf(stderr, "Error starting process %d\n", i);
			} else if (pids[i] == 0) {
				childExecute(program, args, parentPid);
			} else {
				waitSignal(SIGCONT);
			}

			free(args);
		}
	}

	printf("Waiting 1 second before starting...\n");
	sleep(1);

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
