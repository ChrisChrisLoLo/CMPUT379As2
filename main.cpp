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
#include <sstream>
#include <iterator>
#include <vector>

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

#define DROP 2
#define FORWARD 3
#define SEND 4

#define CONT_FD 0
#define SWJ_FD 1
#define SWK_FD 2
#define TRAF_FD 3

#define MIN_PRI 4
#define MAX_RULES 100

#define MAX_BUFF 80

//Note that SWJ_FD = SEND_LEFT and SWK_FD = SEND RIGHT. THIS IS NEEDED.
#define SEND_LEFT 1
#define SEND_RIGHT 2

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

struct traf_t{
    int swi;
    int ipSrc;
    int ipDst;
};
using namespace std;

//Checks if number, taken from:
//https://stackoverflow.com/questions/4654636/how-to-determine-if-a-string-is-a-number-with-c
bool is_number(const std::string& s)
{
    return !s.empty() && std::find_if(s.begin(),
            s.end(), [](char c) { return !std::isdigit(c); }) == s.end();
}

//tokenize string into a vector of strings
//https://www.fluentcpp.com/2017/04/21/how-to-split-a-string-in-c/
vector<string> tokenize(string input){
    istringstream iss(input);
    vector<string> results(istream_iterator<string>{iss}, istream_iterator<string>());
    return results;
}

ifstream trafficFile;


void pExit(string e){
    cout<<e<<endl;
    exit(1);
}

void fdPrint(int fd,char* buf,string message){
    memset(buf, 0, sizeof(buf));
    // Need to close? close(fd[CONT_FD][0]);
    const char * cString = message.c_str();
    sprintf(buf,cString);
    write(fd,buf, MAX_BUFF);
}
struct switch_t{
    int swi;
    int swj;
    int swk;
    int ipLow;
    int ipHigh;
};



void handleQuery(vector<string> tokens, int fd[][2],vector<switch_t> swArr){
    int sourceSw = stoi(tokens[1]);
    int srcIp = stoi(tokens[2]);
    int dstIp = stoi(tokens[3]);
    char buf[MAX_BUFF];

    //see if there is a compatible switch that can meet the rules
    bool switchFound=false;
    for(int i=0;i<swArr.size();i++){
        if(dstIp>=swArr[i].ipLow && dstIp<=swArr[i].ipHigh){
            //ip destination matches current switch
            //fd[swArr[i].swi-1][1];
            if (sourceSw < swArr[i].swi) {
                //send rule to current switch

                //srcIP_lo, srcIP_hi, destIP_low, destIP_high, forward, actionVal , pri, pketcount
                string forwardRightPacket = (string) to_string(ADD)+" "+"0 1000"+" "+to_string(dstIp)+" "+to_string(dstIp)+" "
                                            +to_string(RELAY)+" "+to_string(SEND_RIGHT)+to_string(MIN_PRI)+" "+"0";

                //send to all switches in range [sourceSwitch,destinationSwitch);

                for(int j = sourceSw;j<swArr[i].swi;j++){
                    fdPrint(fd[j-1][1],buf,forwardRightPacket);
                }
                switchFound = true;
            }
            else if (sourceSw > swArr[i].swi){
                string forwardLeftPacket = (string) to_string(ADD)+" "+"0 1000"+" "+to_string(dstIp)+" "+to_string(dstIp)+" "
                                           +to_string(RELAY)+" "+to_string(SEND_LEFT)+to_string(MIN_PRI)+" "+"0";

                //send to all switches in range [sourceSwitch,destinationSwitch);

                for(int j = sourceSw; j>swArr[i].swi;j--){
                    fdPrint(fd[j-1][1],buf,forwardLeftPacket);
                }
                switchFound = true;
            }
        }
    }
    if(!switchFound){
        //no switch found, tell switch to drop package
        string dropPacket =  to_string(ADD)+" "+"0 1000"+" "+to_string(dstIp)+" "+to_string(dstIp)+" "
                            +to_string(DROP)+" "+"0"+" "+to_string(MIN_PRI)+" "+"0";
        fdPrint(fd[sourceSw-1][1],buf,dropPacket);
        cout<<"PRINTED"<<endl;
    }
}



void handleOpen(vector<string> tokens,int fd[][2],vector<switch_t> swArr){
    switch_t switchIn;
    switchIn.swi = stoi(tokens[1]);
    switchIn.swj = stoi(tokens[2]);
    switchIn.swk = stoi(tokens[3]);
    switchIn.ipLow = stoi(tokens[4]);
    switchIn.ipHigh = stoi(tokens[5]);

    //add switch to array.
    swArr.push_back(switchIn);

    string fifoDirWrite = "./fifo-0-"+to_string(switchIn.swi);
    mkfifo(fifoDirWrite.c_str(),(mode_t) 0777);
    int fileDescWrite = open(fifoDirWrite.c_str(),O_WRONLY|O_NONBLOCK);
    if (fileDescWrite<0){
        perror("Error in opening switch writeFIFO");
        exit(EXIT_FAILURE);
    }
    else {
        fd[switchIn.swi-1][1] = fileDescWrite;
    }
    string ackPacket = to_string(ACK);

    char outBuf[MAX_BUFF];
    fdPrint(fd[switchIn.swi-1][1],outBuf,ackPacket);

}


//Many concepts used from poll.c file created by E. Elmallah found in the examples in eclass.
void progController(int nSwitch) {
    int timeout = 0;
    int fd[nSwitch][2];
    struct pollfd pollfd[nSwitch];
    int done[nSwitch];
    //Open pipes from switches 1-n



    for (int i = 1; i < nSwitch + 1; i++) {
        string fifoDirWrite = "./fifo-0-" + to_string(i);
        string fifoDirRead = "./fifo-" + to_string(i) + "-0";

        //Make readfifo and open it store it in fd[][0].
        if (mkfifo(fifoDirRead.c_str(), (mode_t) 0777) < 0)perror(strerror(errno));

        int fileDescRead = open(fifoDirRead.c_str(), O_RDONLY | O_NONBLOCK);
        if (fileDescRead < 0) {
            perror("Error in opening readFIFO");
            exit(EXIT_FAILURE);
        } else {
            cout << "opened fifo-" + to_string(i) + "-0" << endl;
            fd[i - 1][0] = fileDescRead;
            pollfd[i - 1].fd = fileDescRead;
            pollfd[i - 1].events = POLLIN;
            pollfd[i - 1].revents = 0;
        }

        done[i] = 0;
        //Make writefifo and open it store it in fd[][1].


        //Controller loop
        char buf[MAX_BUFF];
        int inLen;
        vector<switch_t> switchArr;
        while (1) {
            int rval = poll(pollfd, nSwitch, timeout);
            if (rval < 0) {
                perror("Error in polling in controller");
                exit(EXIT_FAILURE);
            } else if (rval == 0); //Do Nothing
            else {
                //cout<<rval<<endl;
                for (int i = 0; i < nSwitch; i++) {
                    if (pollfd[i].revents & POLLIN) {
                        memset(buf, 0, MAX_BUFF);
                        inLen = read(fd[i][0], buf, MAX_BUFF);

                        string output = (string) buf;
                        cout << output << endl;
                        vector<string> tokens = tokenize(output);
                        switch (stoi(tokens[0])) {
                            case OPEN:
                                handleOpen(tokens, fd, switchArr);
                                break;

                            case QUERY:
                                handleQuery(tokens, fd, switchArr);
                                cout << "HANDLIN QUERY" << endl;
                                break;
                        }
                    }
                }
            }
        }
    }
}
void findFlowRule(int initTrafIp, int dstTrafIp, int swi, int swj, int swk, int fd[][2], vector<flow_rule> &flowTable, vector<traf_t> &todoList ) {
    bool resolved = false;
    flow_rule foundRule = {0};
    char buf[MAX_BUFF];
    //find rule that matches packet
    for (int i = 0; i < flowTable.size(); i++) {
        //if traffic ip matches on in the rule set
        if (initTrafIp <= flowTable[i].srcIpHi && initTrafIp >= flowTable[i].srcIpLo) {
            if (dstTrafIp <= flowTable[i].destIpHi && dstTrafIp >= flowTable[i].destIpLo) {
                flowTable[i].pktCount += 1;
                foundRule = flowTable[i];
                resolved = true;
                break;
            }
        }
    }
    //if no rule
    if (!resolved) {
        //ask controller for help. query the traffic in a vector
        cout << "Ask controller for help" << endl;

        string queryPacket =
                to_string(QUERY) + " " + to_string(swi) + " " + to_string(initTrafIp) + " " + to_string(dstTrafIp);
        fdPrint(fd[CONT_FD][1], buf, queryPacket);
        traf_t todoTraf;
        todoTraf.swi = swi;
        todoTraf.ipSrc = initTrafIp;
        todoTraf.ipDst = dstTrafIp;
        todoList.push_back(todoTraf);
    } else {
        switch (foundRule.actionType) {
            case FORWARD:
                //Deliver the package
                cout << "DELIVERED" << endl;
                break;

            case DROP:
                cout << "DROPPED" << endl;
                break;

            case SEND:

                string destSwitch = (foundRule.actionVal == SEND_LEFT)?to_string(swj):to_string(swk);
                string fifoDirWrite = "./fifo-"+to_string(swi)+"-"+destSwitch;
                int fileDescWrite = open(fifoDirWrite.c_str(),O_WRONLY|O_NONBLOCK);
                if (fileDescWrite<0){
                    perror("Error in opening switch writeFIFO");
                    exit(EXIT_FAILURE);
                }
                fd[foundRule.actionVal][1] = fileDescWrite;


                string relayPacket = to_string(RELAY)+" "+to_string(initTrafIp)+" "+to_string(dstTrafIp);
                if (foundRule.actionVal == SEND_LEFT){
                    fdPrint(fd[SWJ_FD][1], buf, relayPacket);
                }
                else if (foundRule.actionVal == SEND_RIGHT){
                    fdPrint(fd[SWK_FD][1], buf, relayPacket);
                }
                break;
        }
    }
}
//ADD, srcIP_lo, srcIP_hi, destIP_low, destIP_high, actionType, actionVal , pri, pketcount

void handleAdd(vector<string> tokens, vector<flow_rule> &flowTable, vector<traf_t> &todoList,int swi,int swj, int swk,int fd[][2]){
    //create new rule from the cont message and add it to the flow table

    flowTable.push_back({stoi(tokens[1]),stoi(tokens[2]),stoi(tokens[3]),
                         stoi(tokens[4]),stoi(tokens[5]),stoi(tokens[6]),stoi(tokens[7]),0});

    //now that new rule is added, go through all todoTraffic in the traffic list.
    //Mark any traffic resolved as true, and add them to a list to remove from.
    vector<bool> trafResolved;

    for(int i=0;i<todoList.size();i++){
        bool resolved = false;
        for(int j=0;j<flowTable.size();j++){
            //if traffic ip matches on in the rule set
            if(todoList[i].ipSrc <= flowTable[j].srcIpHi && todoList[i].ipSrc >= flowTable[j].srcIpLo){
                if(todoList[i].ipDst <= flowTable[j].destIpHi && todoList[i].ipDst >= flowTable[j].destIpLo){
                    flowTable[j].pktCount += 1;
                    //foundRule = flowTable[j];
                    resolved = true;
                    flow_rule foundRule = flowTable[i];
                    switch (foundRule.actionType){

                        case FORWARD:
                            cout << "New Rule tells DELIVERED" <<endl;
                            break;

                        case DROP:
                            cout << "New Rule tells DROPPED" << endl;
                            break;

                        case SEND:
                            char buf[MAX_BUFF];
                            string destSwitch = (foundRule.actionVal == SEND_LEFT)?to_string(swj):to_string(swk);
                            string fifoDirWrite = "./fifo-"+to_string(swi)+"-"+destSwitch;
                            int fileDescWrite = open(fifoDirWrite.c_str(),O_WRONLY|O_NONBLOCK);
                            if (fileDescWrite<0){
                                perror("Error in opening switch writeFIFO");
                                exit(EXIT_FAILURE);
                            }
                            fd[foundRule.actionVal][1] = fileDescWrite;


                            string relayPacket = to_string(RELAY)+" "+to_string(todoList[i].ipSrc)+" "+to_string(todoList[i].ipDst);
                            if (foundRule.actionVal == SEND_LEFT){
                                fdPrint(fd[SWJ_FD][1], buf, relayPacket);
                            }
                            else if (foundRule.actionVal == SEND_RIGHT){
                                fdPrint(fd[SWK_FD][1], buf, relayPacket);
                            }
                            break;
                    }
                    break;
                }
            }
        }
        trafResolved.push_back(resolved);
    }
    //https://stackoverflow.com/questions/3487717/erasing-multiple-objects-from-a-stdvector
    for(int i=trafResolved.size()-1;i>=0;i--){
        if (trafResolved[i]){
            todoList.erase(todoList.begin()+i);
        }
    }

}

void progSwitch(int swi, int swj,int swk,int ipLow,int ipHigh){
    int timeout = 0;
    int fd[4][2];
    struct pollfd pollfd[3];
    //cout<<endl<<swi<<endl<<swj<<endl<<swk<<endl<<ipLow<<ipHigh<<endl;
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
    //cout<<"opening " + fifoDirWrite<<endl;
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

    //if defined, open readFIFOs of switch j and switch k

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
        //cout<<"opening"<<fifoSwkRead<<endl;
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
    //Initialize traffic todo list
    vector<traf_t> todoList;

    //Initialize flow table and add initial rule
    vector<flow_rule> flowTable;

    flow_rule initRule;
    initRule.srcIpLo=0;
    initRule.srcIpHi=MAX_IP;
    initRule.destIpLo=ipLow;
    initRule.destIpHi=ipHigh;
    initRule.actionType=FORWARD;
    initRule.actionVal = 3;
    initRule.pri = MIN_PRI;
    initRule.pktCount = 0;

    flowTable.push_back(initRule);

    char outBuf[MAX_BUFF];
    char inBuf[MAX_BUFF];
    int inLen;

    //Send OPEN Packet
    string openPacket = to_string(OPEN)+" "+to_string(swi)+" "
            +to_string(swj)+" "+to_string(swk)+" "
            +to_string(ipLow)+" "+to_string(ipHigh) ;
    fdPrint(fd[CONT_FD][1],outBuf,openPacket);


    //Block switch until it has recieved awknowledgement. We need to do this for sync purposes.
    memset(inBuf, 0,MAX_BUFF);
    while(read(fd[CONT_FD][0], inBuf, MAX_BUFF)<0) {

    }
    cout << "I read" << endl;
    cout << inBuf << endl;
    string trafLine;
    while(1){
        //read line from traffic file
        if (trafficFile.is_open()) {
            if(getline(trafficFile,trafLine)){
                vector<string> trafTokens = tokenize(trafLine);
                if (trafTokens[0] == (string)"sw"+to_string(swi)){
                    cout<<"FOUND A Traffic line FOR ME"<<endl;

                    findFlowRule(stoi(trafTokens[1]),stoi(trafTokens[2]),swi,swj,swk,fd,flowTable,todoList);
                }
            }
        }
        //poll keyboard

        //poll switch

        int rval=poll(pollfd,3,timeout);
        if (rval < 0){
            perror("Error in polling in controller");
            exit(EXIT_FAILURE);
        }
        else if (rval == 0 ); //Do Nothing
        else{
            for(int i=0;i<3;i++){
                if(pollfd[i].revents & POLLIN){
                    memset(inBuf, 0, MAX_BUFF);
                    inLen = read(fd[i][0],inBuf,MAX_BUFF);

                    string output = (string) inBuf;
                    cout<<output<<endl;
                    vector<string>tokens = tokenize(output);
                    switch(stoi(tokens[0])){
                        case ACK:
                            cout<<"Got ACK"<<endl;
                            break;

                        case RELAY:
                            cout<<"Got RELAY"<<endl;
                            findFlowRule(stoi(tokens[1]),stoi(tokens[2]),swi,swj,swk,fd,flowTable,todoList);
                            break;

                        case ADD:
                            cout<<"Got ADD"<<endl;

                            handleAdd(tokens,flowTable,todoList,swi,swj,swk,fd);
                            break;

                    }

                }
            }
        }
    }
}


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
            pExit("Error: Traffic file cannot open");
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
            swj = (int) swjIn[2]-'0';
//            pExit("Error: swj is invalid");
        }

        if (swkIn==string("null")){
            swk = -1;
        }
        else{
            swk = swkIn[2]-'0';
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

        progSwitch(swi,swj,swk,ipLow,ipHigh);
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

