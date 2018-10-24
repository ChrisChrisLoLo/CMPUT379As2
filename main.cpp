#include <algorithm>
#include <fstream>
#include <iostream>
#include <errno.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <poll.h>
//TODO:SET LIMIT TO 10 MIN
#define RLIM_CUR 100 * 60
#define RLIM_MAX 100 * 60

#define MAX_NSW 7
#define MIN_IP 0
#define MAX_IP 1000

#define OPEN 2
#define ACK 3
#define QUERY 4
#define ADD 5
#define RELAY 6

#define DROP 0
#define FORWARD 1

#define CONT_FD 0
#define SWJ_FD 1
#define SWK_FD 2

#define MIN_PRI 4
#define MAX_RULES 100

struct flow_rule{
    int srcIpLo;
    int srcIpHi;
    int destIpLo;
    int destIpHi;
    int actionType;
    int actionVal;
    int pri;
    int pktCount;
};

using namespace std;

//Checks if number, taken from:
//https://stackoverflow.com/questions/4654636/how-to-determine-if-a-string-is-a-number-with-c
bool is_number(const std::string& s)
{
    return !s.empty() && std::find_if(s.begin(),
            s.end(), [](char c) { return !std::isdigit(c); }) == s.end();
}


void pExit(string e){
    cout<<e<<endl;
    exit(1);
}

//Many concepts used from poll.c file created by E. Elmallah found in the examples in eclass.
void progController(int nSwitch){
    int timeout = 0;
    int fd[nSwitch][2];
    struct pollfd pollfd[nSwitch];
    int done[nSwitch];
    //Open pipes from switches 1-n



    for(int i=1;i<nSwitch+1;i++){
        string fifoDirWrite = "./fifo-0-"+to_string(i);
        string fifoDirRead = "./fifo-"+to_string(i)+"-0";

        //Make readfifo and open it store it in fd[][0].
        if (mkfifo(fifoDirRead.c_str(),(mode_t) 0777) < 0)perror(strerror(errno));

        int fileDescRead = open(fifoDirRead.c_str(),O_RDONLY|O_NONBLOCK);
        if (fileDescRead<0){
            perror("Error in opening readFIFO");
            exit(EXIT_FAILURE);
        }
        else{
            cout<<"opened fifo-"+to_string(i)+"-0"<<endl;
            fd[i-1][0] = fileDescRead;
            pollfd[i-1].fd = fileDescRead;
            pollfd[i-1].events = POLLIN;
            pollfd[i-1].revents = 0;
        }

        done[i] = 0;
        //Make writefifo and open it store it in fd[][1].

//        //READER MUST OPEN FIFO BEFORE THE WRITER CAN
//        if (mkfifo(fifoDirWrite.c_str(),(mode_t) 0777) < 0)perror(strerror(errno));
//        int fileDescWrite = open(fifoDirWrite.c_str(),O_WRONLY|O_NONBLOCK);
//        if (fileDescWrite<0){
//            perror("Error in opening writeFIFO");
//            exit(EXIT_FAILURE);
//        }
//        else {
//            fd[i-1][1] = fileDescWrite;
//        }

    }
    //Controller loop
    int bufsize = 80;
    char buf[bufsize];
    int inLen;
    while(1){
        int rval=poll(pollfd,nSwitch,timeout);
        if (rval < 0){
            perror("Error in polling in controller");
            exit(EXIT_FAILURE);
        }
        else if (rval == 0 ); //Do Nothing
        else{
            //cout<<rval<<endl;
            for(int i=0;i<nSwitch;i++){
                if(pollfd[i].revents & POLLIN){
                    memset(buf, 0, bufsize);
                    inLen = read(fd[i][0],buf,bufsize);
                    for(int j=0;j<inLen;j++){
                        if (buf[j] == 0){
                            break;
                        }
                        cout<<buf[j];
                    }
                    string output = (string) buf;
                    cout<<output<<endl;
                }

            }
        }
    }

};

void progSwitch(string trafficFile,int swi, int swj,int swk,int ipLow,int ipHigh){
    int timeout = 0;
    int fd[3][2];
    struct pollfd pollfd[3];
    cout<<trafficFile<<endl<<swi<<endl<<swj<<endl<<swk<<endl<<ipLow<<ipHigh<<endl;
    string fifoDirWrite = "./fifo-"+to_string(swi)+"-0";
    string fifoDirRead = "./fifo-0-"+to_string(swi);


    //Open controller FIFO
    if (mkfifo(fifoDirRead.c_str(),(mode_t) 0777) < 0)perror(strerror(errno));
    int fileDescRead = open(fifoDirRead.c_str(),O_RDONLY|O_NONBLOCK);
    if (fileDescRead<0){
        perror("Error in opening controller readFIFO");
        exit(EXIT_FAILURE);
    }
    else{
        fd[CONT_FD][0] = fileDescRead;
        pollfd[CONT_FD].fd = fileDescRead;
        pollfd[CONT_FD].events = POLLIN;
        pollfd[CONT_FD].revents = 0;
    }
    //////if (mkfifo(fifoDirWrite.c_str(),(mode_t) 0777) < 0)perror(strerror(errno));
    //close(fd[CONT_FD][0]);
    cout<<"opening " + fifoDirWrite<<endl;
    int fileDescWrite = open(fifoDirWrite.c_str(),O_WRONLY|O_NONBLOCK);

//    int fileDescWrite = open("./fifo-1-0",O_WRONLY|O_NONBLOCK);

    if (fileDescWrite<0){
        perror("Error in opening controller writeFIFO");
        exit(EXIT_FAILURE);
    }
    else {
        fd[CONT_FD][1] = fileDescWrite;
        //Close??
    }



    if(swj != -1){
        string fifoSwjWrite = "./fifo-"+to_string(swi)+"-"+to_string(swj);
        string fifoSwjRead = "./fifo-"+to_string(swj)+"-"+to_string(swi);
        if (mkfifo(fifoSwjRead.c_str(),(mode_t) 0777) < 0)perror(strerror(errno));
        int fileSwjRead = open(fifoSwjRead.c_str(),O_RDONLY|O_NONBLOCK);
        if (fileSwjRead<0){
            perror("Error in opening swj readFIFO");
            exit(EXIT_FAILURE);
        }
        else{
            fd[SWJ_FD][0] = fileSwjRead;
            pollfd[SWJ_FD].fd = fileSwjRead;
            pollfd[SWJ_FD].events = POLLIN;
            pollfd[SWJ_FD].revents = 0;
        }
    }
    if(swk != -1){
        string fifoSwkWrite = "./fifo-"+to_string(swi)+"-"+to_string(swk);
        string fifoSwkRead = "./fifo-"+to_string(swk)+"-"+to_string(swi);
//        if (mkfifo(fifoSwkRead.c_str(),(mode_t) 0777) < 0)perror(strerror(errno));
        if (mkfifo(fifoSwkRead.c_str(),(mode_t) 0777) < 0) {
            cout << "fifo already made" << endl;
        }
        cout<<"opening"<<fifoSwkRead<<endl;
        int fileSwkRead = open(fifoSwkRead.c_str(),O_RDONLY|O_NONBLOCK);
        if (fileSwkRead<0){
            perror("Error in opening swk readFIFO");
            exit(EXIT_FAILURE);
        }
        else{
            fd[SWK_FD][0] = fileSwkRead;
            pollfd[SWK_FD].fd = fileSwkRead;
            pollfd[SWK_FD].events = POLLIN;
            pollfd[SWK_FD].revents = 0;
        }
    }

    //Initialize flow table and add initial rule
    flow_rule flowTable[MAX_RULES];

    flow_rule initRule;
    initRule.srcIpLo=0;
    initRule.srcIpHi=MAX_IP;
    initRule.destIpLo=ipLow;
    initRule.destIpHi=ipHigh;
    initRule.actionType=FORWARD;
    initRule.actionVal = 3;
    initRule.pri = MIN_PRI;
    initRule.pktCount = 0;

    flowTable[0] = initRule;

    int bufsize = 80;
    char buf[bufsize];
    int inLen;
    while(1){
        memset(buf, 0, bufsize);
        // Need to close? close(fd[CONT_FD][0]);
        sprintf(buf,"HIII\n");
        write(fd[CONT_FD][1],buf,bufsize);
        memset(buf, 0, bufsize);

        int rval=poll(pollfd,3,timeout);
        if (rval < 0){
            perror("Error in polling in controller");
            exit(EXIT_FAILURE);
        }
        else if (rval == 0 ); //Do Nothing
        else{
            //cout<<rval<<endl;
            for(int i=0;i<3;i++){
                if(pollfd[i].revents & POLLIN){
                    memset(buf, 0, bufsize);
                    inLen = read(fd[i][0],buf,bufsize);
                    for(int j=0;j<inLen;j++){
                        if (buf[j] == 0){
                            break;
                        }
                        cout<<buf[j];
                    }
                    string output = (string) buf;
                    cout<<output<<endl;
                }
            }
        }
    }
}

ifstream trafficFile;

int main(int argc, char* argv[]){
    rlimit rlim;
    if (getrlimit(RLIMIT_CPU, &rlim) == -1){
        cout<<"Error:Rlimit get failed"<<endl;
        cout<<strerror(errno)<<endl;
    }
    rlim.rlim_cur = RLIM_CUR;
    rlim.rlim_max = RLIM_MAX;
    if (setrlimit(RLIMIT_CPU, &rlim) == -1){
        cout<<"Error:Rlimit set failed"<<endl;
        cout<<strerror(errno)<<endl;
    }


    if (argc < 1){
        pExit("Error: No parameters specified");
    }
    else if (argc > 6){
        pExit("Error: too many parameters specified");
    }
    if ((string)argv[1]=="cont"){
        if (!argv[2]){
            pExit("Error: no number of switches given");
        }
        string nSwitch = (string)argv[2];
        if(!is_number(nSwitch)){
            pExit("Error: number of switches not a number");
        }
        else{
            int nSwitchInt = stoi(nSwitch);
            if(nSwitchInt > MAX_NSW){
                pExit("Error: number of switches exceeded the maximum amount");
            }
            cout<< nSwitchInt;
            cout<<"!!EXECUTING CONTROLLER PROG!!"<<endl;
            progController(nSwitchInt);
            exit(0);
        }
    }
    else if(argv[1][0]=='s'&&argv[1][1]=='w'){
        char swiNum = argv[1][2];
        int swi = (int) swiNum-48;
        string trafficFileName = (string)argv[2];
        //open traffic file if it screws up raise error and exit.
        trafficFile.open(trafficFileName);
        cout<<trafficFileName<<endl;
        if(!trafficFile.is_open()){
            pExit("Error: file cannot open");
        }
        char * swjIn = argv[3];
        char * swkIn = argv[4];
        int swj,swk;
        //TODO:CHECK THAT SWK AND SWJ ARE NUMS
//        if (is_number((string)swjIn[2])){
//            swj = atoi(swjIn[2]);
//        }

        //if (swjIn==(char*)"null"){
        if (swjIn==string("null")){
            swj = -1;
        }
        else{
            swj = (int) swjIn[2]-48;
//            pExit("Error: swj is invalid");
        }

        if (swkIn==string("null")){
            swk = -1;
        }
        else{
            swk = (int) swkIn[2]-48;
//            pExit("Error: swk is invalid");
        }
        string ipRange = (string)argv[5];
        //https://stackoverflow.com/questions/28163723/c-how-to-get-substring-after-a-character
        string ipLowIn = ipRange.substr(0,ipRange.find("-"));
        string ipHighIn = ipRange.substr(ipRange.find("-") + 1);

        if(!is_number(ipLowIn)||!is_number(ipHighIn)){
            pExit("Error: ip range does contains a non-number");
        }

        int ipLow = stoi(ipLowIn);
        int ipHigh = stoi(ipHighIn);

        if(ipLow<MIN_IP || ipHigh>MAX_IP){
            pExit("Error: ip range given exceeds the permitted ip range");
        }

        cout<<ipLow<<"-"<<ipHigh<<endl;

        progSwitch(trafficFileName,swi,swj,swk,ipLow,ipHigh);
    }
    else {
        cout<<argv[1];
        pExit("Error: first argument is not 'cont' nor 'swi'");
    }


    string targetPidIn = "";
    string intervalIn = "";

    if (argv[1]) targetPidIn = (string)argv[1];
    if (argv[2]) intervalIn = (string)argv[2];
}

