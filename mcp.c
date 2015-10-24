#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>


#define SECONDS_PER_PROCESS 1

static unsigned int processExited = 0;


int childExecute(char *program, char **args, pid_t parent) {
	sigset_t sigset;
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGUSR1);
	sigprocmask(SIG_BLOCK, &sigset, NULL);
	int sig = 0;

	kill(parent, SIGCONT);
	printf("Process %s is now waiting\n", program);

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
	printf("SENDING SIGUSR1\n");
	for (int k = 0; k < numPids; k++) {
		kill(pids[k], SIGUSR1);
		kill(pids[k], SIGSTOP);
	}

	unsigned int completedProcesses = 0;
	while (1) {
		for (int i = 0; i < numPids; i++) {
			if (pids[i] == 0)
				continue;

			signal(SIGALRM, onAlarm);
			alarm(SECONDS_PER_PROCESS);
			printf("Process %d running\n", i + 1);
			kill(pids[i], SIGCONT);
			waitSignal(SIGUSR2);

			int status = waitpid(pids[i], NULL, WNOHANG);

			if (status == 0) {
				kill(pids[i], SIGSTOP);
				printf("Process %d paused\n", i + 1);
			} else {
				completedProcesses ++;
				pids[i] = 0;
				processExited = 0;
				printf("Process %d terminated\n", i + 1);
			}
		}

		printf("%d completed processes\n", completedProcesses);

		if (completedProcesses == numPids)
			break;
	}
}


void execute(FILE *file) {
	char *line = NULL;
	size_t n = 0;
	size_t numPids = 1;
	pid_t *pids = (pid_t *) malloc(sizeof(pid_t));
	pid_t parentPid = getpid();
	int i = 0;

	while (getline(&line, &n, file) != -1) {
		size_t len = strlen(line);
		line[len - 1] = '\0';

		if (i == numPids) {
			numPids++;
			pids = (pid_t *) realloc(pids, numPids * sizeof(pid_t));
		}

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

		pids[i] = fork();

		if (pids[i] < 0) {
			fprintf(stderr, "Error starting process %d\n", i);
		} else if (pids[i] == 0) {
			childExecute(program, args, parentPid);
		} else {
			waitSignal(SIGCONT);
		}

		free(line);
		line = NULL;
		free(args);
		i++;
	}
	
	free(line);

	printf("Waiting 1 second before starting...\n");
	sleep(1);

	initScheduler(pids, numPids);
	free(pids);
}


int main(int argc, char *argv[]) {
	if (argc < 2) {
		fprintf(stderr, "Please provide input file.\n");
		return 1;
	}

	FILE *file;
	file = fopen(argv[1], "r");

	if (file == NULL) {
		fprintf(stderr, "Error opening file\n");
		return 1;
	}

	execute(file);
	fclose(file);

	printf("MCP CLOSING SUCCESSFULLY!!\n");

	return 0;
}
