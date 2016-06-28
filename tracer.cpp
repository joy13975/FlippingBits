#include <iostream> // for cout/cin
#include <stdlib.h> // for exit codes
#include <unistd.h> // for usleep and fork
#include <sys/types.h> // for pid_t
#include <sys/wait.h> // for wait
#include <signal.h> // for kill
#include <fstream> //for file io
#include <cstring> //for strcmp
#include <stdlib.h> //stdtoull for uint64_t parsing
#include <sstream> //stringstream
#include <sys/ptrace.h> //for ptrace
#include <sys/reg.h> //for ORIG_RAX
#include <memory> //for shared_ptr
#include <regex> //for regex_replace
#include <sys/stat.h> //for stat

typedef uint64_t addr;

void panic(const std::string &msg);
void log(const std::string &msg);
std::string longToHex(const addr &a);
bool fileExists(const std::string &file);
void clrscr();

const std::string tracerHdr = "[Tracer]: ";

class memMap
{
private:
    void parseRange(std::string line, addr &bot, addr &top)
    {
        std::stringstream ss;
        int firstHyphen = line.find('-');
        ss << std::hex << line.substr(0, firstHyphen + 1);
        ss >> bot;
        ss.str("");
        ss << std::hex << line.substr(firstHyphen + 1, line.find(' ') - firstHyphen);
        ss >> top;
    }

public:
    addr stackTop = 0, stackBot = 0, heapTop = 0, heapBot = 0, textTop = 0, textBot = 0;
    int uDataSize = 0, iDataSize = 0, textSize = 0;

    memMap(pid_t traceePid, std::string traceePath) {
        std::ifstream traceeMemMaps;
        std::string traceeMemMapsPath = "/proc/" + std::to_string(traceePid) + "/maps";
        char * pEnd;

        //read tracee's memory map
        traceeMemMaps.open(traceeMemMapsPath);
        if (traceeMemMaps.is_open())
        {
            std::string line;
            while (!traceeMemMaps.eof())
            {
                getline(traceeMemMaps, line);
                if (line.find("[stack]") != std::string::npos) //parse stack top and bottom
                {
                    parseRange(line, stackBot, stackTop);
                }
                else if (line.find("[heap]") != std::string::npos) //parse heap top and bottom
                {
                    parseRange(line, heapBot, heapTop);
                }
                else if (line.find("r-xp") != std::string::npos && line.find(traceePath) != std::string::npos)
                {
                    parseRange(line, textBot, textTop);
                }
            }
        }
        else
        {
            panic("Could not open " + traceeMemMapsPath + "!\n");
        }

        traceeMemMaps.close();

        //get data segment sizes
        const char* sizeCmd = ("size " + traceePath).c_str();
        std::shared_ptr<FILE> pipe(popen(sizeCmd, "r"), pclose);

        if (pipe)
        {
            const int buffSz = 512;
            char buffer[buffSz];
            std::string result;
            while (!feof(pipe.get()))
                if (fgets(buffer, buffSz, pipe.get()) != NULL)
                    result += buffer;

            result = std::regex_replace(result, std::regex("[^0-9 ]+"), std::string(""));

            std::stringstream ss(result);

            ss >> textSize;
            ss >> iDataSize;
            ss >> uDataSize;
        }
        else
        {
            panic("Could not open pipe to " + std::string(sizeCmd) + "!\n");
        }
    }
};

class Tracer {

private:
    const std::string traceePath;
    const int traceeArgc;
    const char ** traceeArgs;
    pid_t traceePid;
    size_t mmapSize = 0;

    void tracerProcess() {
        waitForChildToStop();
        setupTracer();
        kill(traceePid, SIGCONT); //allow child to continue

        const int msWait = 2500;
        clrscr();

        log("Tracee PID: " + std::to_string(traceePid) + "\n");

        while (1)
        {
            if (waitForSysCall()) break; //call
            int syscall = ptrace(PTRACE_PEEKUSER, traceePid, sizeof(long) * ORIG_RAX);
            uint64_t reqAddr = ptrace(PTRACE_PEEKUSER, traceePid, sizeof(long) * RDI);
            uint64_t len = ptrace(PTRACE_PEEKUSER, traceePid, sizeof(long) * RSI);

            if (syscall == 9 || syscall == 12) {
                if (waitForSysCall()) break; //return
                std::cout << "\033[2;1H\033[0J";
                printDynamicMemInfo(memMap(traceePid, traceePath));

                uint64_t sysret = ptrace(PTRACE_PEEKUSER, traceePid, sizeof(long) * RAX);
                std::stringstream ss;

                ss << "syscall: ";

                if (syscall == 9)
                {
                    mmapSize += len;
                    ss << "mmap(addr: " << longToHex(reqAddr) << ", len: " << longToHex(len) << ", ...)";
                    ss << " = " << longToHex(sysret) << "\n";
                }
                else
                {
                    ss << "brk(addr: " << longToHex(reqAddr) << ")";
                    ss << " = " + longToHex(sysret) + "\n";
                }

                log(ss.str());
            }

            if (waitpid(traceePid, 0, WNOHANG | __WALL))
            {
                log("Tracee has stopped...\n");
                break;
            }
        }

        waitpid(traceePid, 0, __WALL); // wait for child to exit
    }

    void printDynamicMemInfo(const memMap &mm)
    {
        log("Uninitialised Data Size: " + std::to_string(mm.uDataSize) + " B\n");
        log("Initialised Data Size: " + std::to_string(mm.iDataSize) + " B\n");
        log("\tText Top: 0x" + longToHex(mm.textTop) + "\n");
        log("\tText Bot: 0x" + longToHex(mm.textBot) + "\n");
        log("Text Size: " + std::to_string(mm.textSize) + " B\n");

        //dynamic numbers
        log("\tStack Top: 0x" + longToHex(mm.stackTop) + "\n");
        log("\tStack Bot: 0x" + longToHex(mm.stackBot) + "\n");
        log("Stack Size: " + std::to_string((mm.stackTop - mm.stackBot) / 1024) + " KB\n");
        log("\tHeap Top: 0x" + longToHex(mm.heapTop) + "\n");
        log("\tHeap Bot: 0x" + longToHex(mm.heapBot) + "\n");
        log("Heap Size: " + std::to_string((mm.heapTop - mm.heapBot) / 1024) + " KB\n");

        //very dynamic numbers
        log("MMAP'd region size: " + std::to_string(mmapSize / 1024) + " KB\n");
    }

    void waitForChildToStop()
    {
        const int msWait = 100;
        int status;
        while (1)
        {
            waitpid(traceePid, &status, WNOHANG | __WALL); //keep checking status until stopped
            if (WIFSTOPPED(status) && WSTOPSIG(status) == SIGSTOP)
                break;
            usleep(msWait * 1000);
        }

        log("Child process initial stop detected\n");
    }

    int waitForSysCall()
    {
        int status;
        while (1)
        {
            ptrace(PTRACE_SYSCALL, traceePid, 0, 0);
            waitpid(traceePid, &status, __WALL);
            if (WIFSTOPPED(status) && WSTOPSIG(status) & 0x80)
                return 0;
            if (WIFEXITED(status))
                return 1;
        }
    }

    void setupTracer()
    {
        ptrace(PTRACE_SETOPTIONS, traceePid, 0, PTRACE_O_TRACESYSGOOD);
    }

    void startChildProcess()
    {
        ptrace(PTRACE_TRACEME);

        log("Tracee path: " + traceePath + "\n");
        log("Tracee args (" + std::to_string(traceeArgc) + "): \n");
        for (int i = 0; i < traceeArgc; i++)
            log("\t" + std::string(traceeArgs[i]) + "\n");
        log("Tracee PID: " + std::to_string(getpid()) + "\n");

        //wait for enter to continue
        log("Press enter to start tracing...\n");
        std::cin.ignore();

        kill(getpid(), SIGSTOP);

        execv(traceePath.c_str(), (char* const*) traceeArgs);

        log("Child process exited via parent\n");
    }

    void tracerPanic(std::string msg)
    {
        kill(traceePid, SIGKILL);
        panic(msg);
    }

public:
    Tracer(const int _argc, const char ** _traceePathAndArgs) :
        traceePath(_traceePathAndArgs[0]),
        traceeArgc(_argc),
        traceeArgs(&(_traceePathAndArgs[0]))
    {
        if (!fileExists(traceePath))
            panic("Tracee executable '" + traceePath + "' not found!\n");
        else
            log("Tracee executable '" + traceePath + "' found\n");
    }

    void run()
    {
        log("-+-+-Tracer Starts-+-+-\n");
        log("Starting tracee...\n");

        traceePid = fork();

        if (traceePid == 0) //this checks for child process
            startChildProcess();
        else
            tracerProcess();

        log("-+-+-Tracer Ends-+-+-\n");
    }
};

void panic(const std::string &msg)
{
    std::cerr << tracerHdr << "Fatal Error! " << msg;
    exit(EXIT_FAILURE);
}

void log(const std::string &msg)
{
    std::cout << tracerHdr << msg;
}

std::string longToHex(const addr &a)
{
    std::stringstream ss;
    ss << std::hex << a;
    return ss.str();
}

bool fileExists(const std::string &file) {
    struct stat buffer;
    return (stat (file.c_str(), &buffer) == 0);
}

void clrscr()
{
    std::cout << "\033[1J\033[1;1H";
}

int main(int argc, const char** argv)
{
    if (argc < 2)
    {
        log("Usage: tracer.exe <path of tracee> <tracee arguments ...>\n");
        exit(EXIT_FAILURE);
    }

    Tracer t(argc - 1, &(argv[1]));
    t.run();
    return EXIT_SUCCESS;
}