#include <queue>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <string.h>
#include <string>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/stat.h>

using namespace std;

// Global variables used in the program
int pipe1[2];
pid_t controllerPID;
queue<string> commands;

// Structs
struct State
{
	int value;
	int currentPC;
};

struct Process
{
	int pid;
	string ir;
	State currentState;
	int startTime;
	int cpuTimeUsed;
	string *instrSet;
	int numInstructions;
};

struct CPU
{
	bool isIdle;
	State *statePtr;
	string *progArrPtr;
	int currTimeUnit;

	CPU()
	{
		isIdle = true;
		statePtr = NULL;
		progArrPtr = NULL;
		currTimeUnit = 0;
	}
};

// Forward declarations for functions
void sigusr1Handler(int);
void sigalrmHandler(int);
bool createProcess(Process*&, char*);
void broadcastStatus(queue<Process*>, queue<Process*>, Process*&, int, CPU cpu);
void processCommands(string, queue<Process*>&, queue<Process*>&, Process*&, CPU&, int&, int&, int&, int&);
void printProcessDetails(Process*);
void processInstructions(queue<Process*>&, queue<Process*>&, Process*&, CPU&, int*, int*, string, int&, int);
void freeCPU(CPU&);
void contextSwitch(CPU&, queue<Process*>&, Process*&);

int main()
{
	// Setting handlers for some signals
	signal(SIGALRM, sigalrmHandler);
	signal(SIGUSR1, sigusr1Handler);

	int processID = 0;
	int currTime = 0;
	int numProcesses = 0;
	int totalTurnaroundTime = 0;

	pipe(pipe1);

	// Creating a child process
	controllerPID = fork();
	if (!controllerPID)
	{
		// Code for the controller process
		queue<Process*> readyQueue;
		queue<Process*> blockedQueue;
		Process* running;
		CPU cpu;

		while (true)
		{
			if (!commands.empty())
			{
				string cmd = commands.front();
				commands.pop();

				processCommands(cmd, readyQueue, blockedQueue, running, cpu, processID, currTime, numProcesses, totalTurnaroundTime);
			}
		}
	}
	else
	{
		// Code for the in charge process
		sigalrmHandler(SIGALRM);
		wait(NULL);
	}
	return 0;
}

void sigusr1Handler(int sigNum)
{
	// Reading from the pipe
	char readBuffer[100];
	close(pipe1[1]);
	read(pipe1[0], readBuffer, 100);

	// Adding command to the command queue
	string cmd = readBuffer;
	commands.push(cmd);
}

void sigalrmHandler(int sigNum)
{
	// Taking input from user
	char writeBuffer[100];
	cout << "\nEnter a command: ";
	cin.getline(writeBuffer, 100);

	// Writing to the pipe
	close(pipe1[0]);
	write(pipe1[1], writeBuffer, 100);

	// Sending a signal to the controller process and resetting the alarm
	kill(controllerPID, SIGUSR1);
	alarm(2);
}

bool createProcess(Process *&newProcess, string fileName)
{
	bool flag = false;
	const char *c = fileName.c_str();
	ifstream myFile(c);
	if (myFile.is_open())
	{
		flag = true;
		int numInstructions = 0;
		string line;
		while (getline(myFile, line))
			++numInstructions;
		
		newProcess->numInstructions = numInstructions;
		newProcess->instrSet = new string[numInstructions];
		
		myFile.clear();
		myFile.seekg(0, ios::beg);
		
		int idx = 0;
		while(getline(myFile, line))
		{
			newProcess->instrSet[idx] = line;
			++idx;
		}
		
		myFile.close();
	}
	else
		cout << "Error: File not found!\n";

	return flag;	
}

void broadcastStatus(queue<Process*> readyQueue, queue<Process*> blockedQueue, Process *&p, int currTime, CPU cpu)
{
	cout << "********************************************\n";
	cout << "   The current system state is as follows   \n";
	cout << "********************************************\n";

	cout << "\nCurrent Time: " << currTime << endl;

    if (!cpu.isIdle)
	{
        cout << "\nCurrently running process... \n";
        cout << "PID: " << p->pid << endl;
        cout << "Integer value: " << p->currentState.value << endl;
        cout << "CPU time used: " << p->cpuTimeUsed << endl;
    }
    else
        cout << "\nNo running process currently...\n";

	if (!blockedQueue.empty())
	{
		cout << "\nBlocked processes...\n";
		while (!blockedQueue.empty())
		{
			Process *temp = blockedQueue.front();
			blockedQueue.pop();
			cout << "PID: " << temp->pid << endl;
			cout << "Integer value: " << temp->currentState.value << endl;
			cout << "CPU time used: " << temp->cpuTimeUsed << endl << endl;
		}
	}
	else
		cout << "\nNo processes currently in Blocked Queue...\n";

	if (!readyQueue.empty())
	{
		cout << "\nProcesses ready to execute...\n";
		while (!readyQueue.empty())
		{
			Process *temp = readyQueue.front();
			readyQueue.pop();
			cout << "PID: " << temp->pid << endl;
			cout << "Integer value: " << temp->currentState.value << endl;
			cout << "CPU time used: " << temp->cpuTimeUsed << endl << endl;
		}
	}
	else
		cout << "\nNo processes currently in Ready Queue... \n";
}

void printProcessDetails(Process *p)
{
	cout << "PID: " << p->pid << "\n";
	cout << "CPU time used: " << p->cpuTimeUsed << "\n";
	cout << "Start time: " << p->startTime << "\n";
	cout << "Instructions: (" << p->numInstructions << ")\n";
	
	for (int i = 0; i < p->numInstructions; ++i)
		cout << p->instrSet[i] << "\n";
}

void freeCPU(CPU &cpu)
{
	cpu.isIdle = true;
	cpu.progArrPtr = NULL;
	cpu.statePtr = NULL;
	cpu.currTimeUnit = 0;
}

void contextSwitch(CPU &cpu, queue<Process*> &readyQueue, Process *&running)
{
	if (!readyQueue.empty())
	{
		running = readyQueue.front();
		readyQueue.pop();

		cpu.currTimeUnit = 0;
		cpu.isIdle = false;
		cpu.progArrPtr = running->instrSet;
		cpu.statePtr = &running->currentState;
	}
}

void processInstructions(queue<Process*> &readyQueue, queue<Process*> &blockedQueue, Process *&running, CPU &cpu, int *PC, int *value, string currInstr, int &totalTurnaroundTime, int currTime)
{
    string temp;
    if (currInstr.length() > 2)
    	temp = currInstr.substr(2, currInstr.find('\n'));

	int num = 0;

	if (currInstr[0] == 'S')
	{
		num = atoi(temp.c_str());
		*value = num;
	}
	else if (currInstr[0] == 'A')
	{
		num = atoi(temp.c_str());
		*value = *value + num;
	}
	else if (currInstr[0] == 'D')
	{
		num = atoi(temp.c_str());
		*value = *value - num;
	}
	else if (currInstr[0] == 'R')
	{
		Process* newProcess = new Process;
		newProcess->pid = running->pid;
		newProcess->startTime = running->startTime;
		newProcess->cpuTimeUsed = running->cpuTimeUsed;

		bool flag = createProcess(newProcess, temp);
		
		if (flag)
		{
			*value = 0;
			*PC = -1;

            if (running->instrSet)
                delete[] running->instrSet;

			running = newProcess;
			cpu.progArrPtr = running->instrSet;
			cpu.statePtr = &running->currentState;
			cout << "Process image replaced... \n";
			printProcessDetails(newProcess);
		}
		else
			cout << "Error: failed to replace program image \n";
	}
	else if (currInstr[0] == 'B')
	{
		cout << "pid: " << running->pid << ", moved from running state to blocked queue..\n";
		blockedQueue.push(running);
		freeCPU(cpu);
		contextSwitch(cpu, readyQueue, running);	
	}
	else if (currInstr[0] == 'E')
	{
		cout << "Terminating pid: " << running->pid << endl;
		totalTurnaroundTime += currTime + running->startTime;
		if (running->instrSet)
			delete[] running->instrSet;

		freeCPU(cpu);

		contextSwitch(cpu, readyQueue, running);	
	}	
}

void processCommands(string cmd, queue<Process*> &readyQueue, queue<Process*> &blockedQueue, Process *&running, CPU &cpu, int &processID, int &currTime, int &numProcesses, int &totalTurnaroundTime)
{
	if (cmd.substr(0, 3) == "CRT")
	{
		// Create process
		cout << endl;
		Process *newProcess = new Process;
		newProcess->pid = processID;
		newProcess->startTime = currTime;

		string fileName = cmd.substr(4, cmd.length() - 4);

		bool flag = createProcess(newProcess, fileName);

		if (flag)
		{
			readyQueue.push(newProcess);
			++numProcesses;
			++processID;
			cout << "New process created \n";
			printProcessDetails(newProcess);
		}
		else
			cout << "Error: Failed to create process! \n";
	}
	else if (cmd == "PRT")
	{
		// Broadcaster message
		cout << endl;
		if (!fork())
		{
			broadcastStatus(readyQueue, blockedQueue, running, currTime, cpu);
			exit(0);
		}
		else
            wait(NULL);
	}
	else if (cmd == "END")
	{
		// Broadcast status and then controller exits
		cout << endl;
		if (!fork())
		{
			broadcastStatus(readyQueue, blockedQueue, running, currTime, cpu);

			cout << "\nTotal turnaround time: " << totalTurnaroundTime << endl;
			cout << "Total number of processes: " << numProcesses << endl;
			cout << "Average turnaround time: " << float(totalTurnaroundTime) / float(numProcesses) << endl;

			exit(0);
		}
		else
		{
			wait(NULL);
			exit(0);
		}
	}
	else if (cmd == "UNB")
	{
		// unblock from blocked queue and push to ready queue
		cout << endl;
		if (!blockedQueue.empty())
		{
			readyQueue.push(blockedQueue.front());
			cout << "Process with pid: " << blockedQueue.front()->pid << ", moved from blocked-queue to ready-queue...\n";
			blockedQueue.pop();
		}
		else
			cout << "Error: No processes in blocked queue \n";
	}
	else if (cmd == "INC")
	{
		cout << endl;
		if (!cpu.isIdle)
		{
			int *value = &cpu.statePtr->value;
			int *PC = &cpu.statePtr->currentPC;
			string currInstr = cpu.progArrPtr[*PC]; 
			running->ir = currInstr;

			cpu.currTimeUnit = cpu.currTimeUnit + 1;					
			running->cpuTimeUsed = running->cpuTimeUsed + 1;

			processInstructions(readyQueue, blockedQueue, running, cpu, PC, value, currInstr, totalTurnaroundTime, currTime);

			cout << "pid: " << running->pid << ", inst: " << currInstr << ", value: " << *value << ", curr time unit: " << cpu.currTimeUnit << endl;
			
			*PC = *PC + 1;

			if (cpu.currTimeUnit == 3 && *PC < running->numInstructions)
			{
				cout << "Time quantum over \nswitching context.. \n";
				
				readyQueue.push(running);
					
				freeCPU(cpu);
				
				contextSwitch(cpu, readyQueue, running);
			}
			else if ((cpu.currTimeUnit < 3 && *PC == running->numInstructions) || (cpu.currTimeUnit == 3 && *PC == running->numInstructions))
			{
				cout << "pid: " << running->pid << ", finished executing..\n" << "switching context.. \n";

				totalTurnaroundTime += currTime + running->startTime;

                if (running->instrSet)
                    delete[] running->instrSet;
				
				freeCPU(cpu);
				
				contextSwitch(cpu, readyQueue, running);	
			}
		}
		else
			cout << "Error: CPU is idle, no processes in ready queue \n";
		
		++currTime;
	}

	// if cpu is idle then switch context
	if (cpu.isIdle)
	    contextSwitch(cpu, readyQueue, running);
}