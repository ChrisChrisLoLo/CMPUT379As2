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

//Define codes for packet actions
#define OPEN 2
#define ACK 3
#define QUERY 4
#define ADD 5
#define RELAY 6

//Define codes for flow table rules
#define DROP 1
#define FORWARD 2
#define SEND 3

//Define file descriptor positions for fd[][2] in the switch
#define CONT_FD 0
#define SWJ_FD 1
#define SWK_FD 2

//Define minimum priority
#define MIN_PRI 4

//Define maximum message buffer size
#define MAX_BUFF 80

//Note that SWJ_FD = SEND_LEFT and SWK_FD = SEND RIGHT. THIS IS NEEDED.
#define SEND_LEFT 1
#define SEND_RIGHT 2

#define KEYBOARDFD_NUM 1



//Struct to define a given flow rule.
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

//Struct used to define a given traffic rule
struct traf_t{
    int swi;
    int ipSrc;
    int ipDst;
};

//Struct used to define the given stats of a controller or switch.
//Keeps track of all packets sent and recieved.
struct packStat_t{
    int rOpen=0;
    int rQuery=0;
    int rAdmit=0;
    int rAck=0;
    int rAdd=0;
    int rRelay=0;
    int tAck=0;
    int tAdd=0;
    int tOpen=0;
    int tQuery=0;
    int tRelay=0;
};

//Struct to define ports and number pertaining to a switch
struct switch_t{
    int swi;
    int swj;
    int swk;
    int ipLow;
    int ipHigh;
};

using namespace std;

const string packetPairs[7] ={"ERROR1","ERROR2","OPEN","ACK","QUERY","ADD","RELAY"};
const string flowPairs[4]={"ERROR1","DROP","FORWARD","SEND"};
//Initialize the packet statistics as a global.
packStat_t pStat;

//Initialize the traffic file as a global.
ifstream trafficFile;


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


//Print a message and exit. makes code in main() a bit cleaner.
void pExit(string e){
    cout<<e<<endl;
    exit(1);
}

//Print to a given file descriptor.
void fdPrint(int fd,char* buf,string message){
    memset(buf, 0, sizeof(buf));
    const char * cString = message.c_str();
    sprintf(buf,cString);
    write(fd,buf, MAX_BUFF);
}

//Used by the controller to handle a query signal. takes in the packets contents, the fd's the controller has,
//as well as the array of switches that is in contact with the controller.
void handleQuery(vector<string> tokens, int fd[][2],vector<switch_t> swArr){
    int sourceSw = stoi(tokens[1]);
    int srcIp = stoi(tokens[2]);
    int dstIp = stoi(tokens[3]);
    char buf[MAX_BUFF];

    //print that we recieved something
    printf("Received (src= sw%i, dest=cont) [QUERY]:  header= (srcIP= %i, destIP= %i)\n",sourceSw,srcIp,dstIp);

    //see if there is a compatible switch that can meet the rules
    bool switchFound=false;
    for(int i=0;i<swArr.size();i++){
        if(dstIp>=swArr[i].ipLow && dstIp<=swArr[i].ipHigh){
            if (sourceSw < swArr[i].swi) {
                //Tell to send packet right
                string forwardRightPacket = (string) to_string(ADD)+" "+"0 1000"+" "+to_string(swArr[i].ipLow)+" "+to_string(swArr[i].ipHigh)+" "
                                            +to_string(SEND)+" "+to_string(SEND_RIGHT)+" "+to_string(MIN_PRI)+" "+"0";
                bool canTravel = true;
                //double check that there are intermediary switches that can carry the package
                for(int j = sourceSw;j<swArr[i].swi;j++){
                    bool matches = false;
                    for(int k=0; k<swArr.size();k++){
                        if(j==swArr[k].swi){
                            matches = true;
                        }
                    }
                    if(!matches){
                        canTravel = false;
                    }
                }
                if(!canTravel){
                    continue;
                }
                //send to all switches in range [sourceSwitch,destinationSwitch);
//                for(int j = sourceSw;j<swArr[i].swi;j++) {
//                    fdPrint(fd[j - 1][1], buf, forwardRightPacket);
//                    printf("Transmitted (src= cont, dest= sw%i)[ADD]\n", j);
//                    printf("(srcIP= 0-1000, destIP= %i-%i, action= %s:%i, pri=%i, pktCount=0)\n", dstIp, dstIp,
//                           flowPairs[SEND].c_str(), SEND, MIN_PRI);
//                    pStat.tAdd++;
//                }
                fdPrint(fd[sourceSw-1][1],buf,forwardRightPacket);
                printf("Transmitted (src= cont, dest= sw%i)[ADD]\n",sourceSw);
                printf("(srcIP= 0-1000, destIP= %i-%i, action= %s:%i, pri=%i, pktCount=0)\n",dstIp,dstIp,flowPairs[SEND].c_str(),SEND,MIN_PRI);
                pStat.tAdd++;
                switchFound = true;
            }
            else if (sourceSw > swArr[i].swi){
                //Give command to send packet left
                string forwardLeftPacket = (string) to_string(ADD)+" "+"0 1000"+" "+to_string(swArr[i].ipLow)+" "+to_string(swArr[i].ipHigh)+" "
                                           +to_string(SEND)+" "+to_string(SEND_LEFT)+" "+to_string(MIN_PRI)+" "+"0";
                bool canTravel = true;
                //double check that there are intermediary switches that can carry the package
                for(int j = sourceSw;j>swArr[i].swi;j--){
                    bool matches = false;
                    for(int k=0; k<swArr.size();k++){
                        if(j==swArr[k].swi) {
                            if (j == swArr[k].swi) {
                                matches = true;
                            }
                        }
                    }
                    if(!matches){
                        canTravel = false;
                    }
                }
                if(!canTravel){
                    continue;
                }
                //Send to all switches in range [sourceSwitch,destinationSwitch).
                //This is to handle the case where the packet must travel multiple times.
                //ie. from switch 1 to switch 7
                //It would not be wise to only tell the current switch to send the packet left
                //fully knowing well that we need to repeat this process n-2 times.
//                for(int j = sourceSw; j>swArr[i].swi;j--){
//                    fdPrint(fd[j-1][1],buf,forwardLeftPacket);
//                    printf("Transmitted (src= cont, dest= sw%i)[ADD]\n",j);
//                    printf("(srcIP= 0-1000, destIP= %i-%i, action= %s:%i, pri=%i, pktCount=0)\n",dstIp,dstIp,flowPairs[SEND].c_str(),SEND,MIN_PRI);
//                    pStat.tAdd++;
//                }
                pStat.tAdd++;
                fdPrint(fd[sourceSw-1][1],buf,forwardLeftPacket);
                printf("Transmitted (src= cont, dest= sw%i)[ADD]\n",sourceSw);
                switchFound = true;
            }
        }
    }
    if(!switchFound){
        //no switch found, tell switch to drop package
        string dropPacket =  to_string(ADD)+" "+"0 1000"+" "+to_string(dstIp)+" "+to_string(dstIp)+" "
                            +to_string(DROP)+" "+"0"+" "+to_string(MIN_PRI)+" "+"0";
        fdPrint(fd[sourceSw-1][1],buf,dropPacket);
        pStat.tAdd++;
        printf("Transmitted (src= cont, dest= sw%i)[ADD]\n",sourceSw);
        printf("(srcIP= 0-1000, destIP= %i-%i, action= %s:%i, pri=%i, pktCount=0)\n",dstIp,dstIp,flowPairs[DROP].c_str(),DROP,MIN_PRI);
//        cout<<"PRINTED"<<endl;
    }
}


//Handle the open signal has the controller.
void handleOpen(vector<string> tokens,int fd[][2],vector<switch_t> &swArr){
    switch_t switchIn;
    switchIn.swi = stoi(tokens[1]);
    switchIn.swj = stoi(tokens[2]);
    switchIn.swk = stoi(tokens[3]);
    switchIn.ipLow = stoi(tokens[4]);
    switchIn.ipHigh = stoi(tokens[5]);


    printf("Received (src= sw%i, dest=cont) [OPEN]:  header= (port0= cont ,port1= %i, port2 = %i, port3= %i-%i)\n",
            switchIn.swi,switchIn.swj,switchIn.swk,switchIn.ipLow,switchIn.ipHigh);

    //add switch to the list of switches.
    swArr.push_back({stoi(tokens[1]),stoi(tokens[2]),stoi(tokens[3]),stoi(tokens[4]),stoi(tokens[5])});

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

    //send acknowledgement packet to switch
    string ackPacket = to_string(ACK);
    pStat.tAck++;
    printf("Transmitted (src= cont, dest= sw%i)[ACK]\n",switchIn.swi);
    char outBuf[MAX_BUFF];
    fdPrint(fd[switchIn.swi-1][1],outBuf,ackPacket);


}

//The setup and loop of the controller code is found here.
//Many concepts used from poll.c file created by E. Elmallah, which was found in the examples in eclass.
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
        mkfifo(fifoDirRead.c_str(), (mode_t) 0777);

        int fileDescRead = open(fifoDirRead.c_str(), O_RDONLY | O_NONBLOCK);
        if (fileDescRead < 0) {
            perror("Error in opening readFIFO");
            exit(EXIT_FAILURE);
        } else {
            //cout << "opened fifo-" + to_string(i) + "-0" << endl;
            fd[i - 1][0] = fileDescRead;
            pollfd[i - 1].fd = fileDescRead;
            pollfd[i - 1].events = POLLIN;
            pollfd[i - 1].revents = 0;
        }

        done[i] = 0;
    }

    //Controller loop
    char buf[MAX_BUFF];
    int inLen;
    vector<switch_t> switchArr;

    //Initialize a file descriptor for standard input
    struct pollfd keyboardFd[1];
    keyboardFd[0].fd = STDIN_FILENO;
    keyboardFd[0].events=POLLIN;
    keyboardFd[0].revents=0;
    char outBuf[MAX_BUFF];
    char inBuf[MAX_BUFF];
    while (1) {

        //poll the  keyboard
        int rvalKeyboard=poll(keyboardFd,KEYBOARDFD_NUM,timeout);
        if (rvalKeyboard < 0){
            perror("Error in polling in controller");
            exit(EXIT_FAILURE);
        }
        else if (rvalKeyboard == 0 ); //Do Nothing
        else{
            //BUG: Sometimes keyboard input doesnt work for whatever reason. find out reason why.
            //check if keyboard has pollin. For some reason poll pri is being introduced
            if(keyboardFd[0].revents & POLLIN){
                memset(inBuf, 0, MAX_BUFF);
                inLen = read(0,inBuf,MAX_BUFF);

                string output = (string) inBuf;
                //cout<<output<<endl;

                if (output == (string)"list\n"){
                    //print switch information
                    cout<<"Switch information:"<<endl;
                    for(int i=0;i<switchArr.size();i++){
                        switch_t sw = switchArr[i];
                        printf("[%i] port1=%i port2=%i port3=%i-%i\n",sw.swi,sw.swj,sw.swk,sw.ipLow,sw.ipHigh);
                    }
                    //print packet stats
                    cout<<endl<<"Packet Stats:"<<endl;
                    printf("Recieved:      OPEN:%i, QUERY:%i\n",pStat.rOpen,pStat.rQuery);
                    printf("Transmitted:   ACK:%i, ADD:%i\n",pStat.tAck,pStat.tAdd);
                }
                else if (output == (string)"exit\n"){
                    cout<<"Exiting..."<<endl;
                    exit(0);
                }
                else{
                    cout<<"Unknown input command."<<endl;
                }
            }
        }
        //poll the switches
        int rval = poll(pollfd, nSwitch, timeout);
        if (rval < 0) {
            perror("Error in polling in controller");
            exit(EXIT_FAILURE);
        }
        else if (rval == 0); //Do Nothing
        else {
            for (int i = 0; i < nSwitch; i++) {
                if (pollfd[i].revents & POLLIN) {
                    memset(buf, 0, MAX_BUFF);
                    inLen = read(fd[i][0], buf, MAX_BUFF);

                    string output = (string) buf;
                    vector<string> tokens = tokenize(output);
                    switch (stoi(tokens[0])) {
                        case OPEN:
                            pStat.rOpen++;
                            handleOpen(tokens, fd, switchArr);
                            break;

                        case QUERY:
                            pStat.rQuery++;
                            handleQuery(tokens, fd, switchArr);
                            break;
                    }
                }
            }
        }
    }

}

vector<string> sentQueryPacks;

//Attempts to find a rule that works for a packet that the switch as recieved. If no rule is found, send a Query packet
//to the controller.
void findFlowRule(int initTrafIp, int dstTrafIp, int swi, int swj, int swk, int fd[][2], vector<flow_rule> &flowTable, vector<traf_t> &todoList ) {
    bool resolved = false;
    flow_rule foundRule = {0};
    char buf[MAX_BUFF];
    //find rule that matches packet. We want the most recent rule as the initial rule is a catch all
    for (int i = flowTable.size()-1; i >= 0; i--) {
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


    //if no rule has been found.
    if (!resolved) {
        string queryPacket =
                to_string(QUERY) + " " + to_string(swi) + " " + to_string(initTrafIp) + " " + to_string(dstTrafIp);
        bool dupPacket = false;
        for(int i;i<sentQueryPacks.size();i++){
            if(sentQueryPacks[i].compare(queryPacket)==0){
                dupPacket = true;
            }
        }
        if(dupPacket){
            return;
        }
        else {
            sentQueryPacks.push_back(queryPacket);

            //ask controller for help. query the traffic in a vector
            printf("Transmitted (src= %i, dest= cont) [QUERY]:  header= (srcIP= %i, destIP= %i)\n", swi, initTrafIp,
                   dstTrafIp);

            fdPrint(fd[CONT_FD][1], buf, queryPacket);
            pStat.tQuery++;
            traf_t todoTraf;
            todoTraf.swi = swi;
            todoTraf.ipSrc = initTrafIp;
            todoTraf.ipDst = dstTrafIp;
            todoList.push_back(todoTraf);
        }
    }
    else {
        switch (foundRule.actionType) {
            case FORWARD:
                //Deliver the package
                break;

            case DROP:
                //Drop the package
                break;

            case SEND:
                //Send the package to another switch
                //Set up a fifo with the recipient switch if not yet done.
                string destSwitch = (foundRule.actionVal == SEND_LEFT)?to_string(swj):to_string(swk);
                string fifoDirWrite = "./fifo-"+to_string(swi)+"-"+destSwitch;
                int fileDescWrite = open(fifoDirWrite.c_str(),O_WRONLY|O_NONBLOCK);
                if (fileDescWrite<0){
                    perror("Error in opening relay switch writeFIFO");
                    exit(EXIT_FAILURE);
                }
                fd[foundRule.actionVal][1] = fileDescWrite;


                string relayPacket = to_string(RELAY)+" "+to_string(initTrafIp)+" "+to_string(dstTrafIp);
                int dstSwitchNum;
                if (foundRule.actionVal == SEND_LEFT){
                    fdPrint(fd[SWJ_FD][1], buf, relayPacket);
                    dstSwitchNum=swj;
                }
                else if (foundRule.actionVal == SEND_RIGHT){
                    fdPrint(fd[SWK_FD][1], buf, relayPacket);
                    dstSwitchNum=swk;
                }
                pStat.tRelay++;
                printf("Transmitted (src= sw%i, dest= sw%i) [RELAY]:  header= (srcIP= %i, destIP= %i)\n",
                        swi,dstSwitchNum,initTrafIp,dstTrafIp);
                break;
        }
    }
}

//Handles the add signal as the switch.
//NOTE: It is possible for there to be more ADD packets than rules when listing. THis is becuase not all ADDS result in a new rule.

void handleAdd(vector<string> tokens, vector<flow_rule> &flowTable, vector<traf_t> &todoList,int swi,int swj, int swk,int fd[][2]){
    //create new rule from the cont message and add it to the flow table
    flow_rule newRule ={stoi(tokens[1]),stoi(tokens[2]),stoi(tokens[3]),
                        stoi(tokens[4]),stoi(tokens[5]),stoi(tokens[6]),stoi(tokens[7]),0};
    printf("Received (src= cont, dest= sw%i) [ADD]:\n",swi);
    printf("(srcIP= %i-%i, destIP= %i-%i, action= %s:%i, pri= %i, pktCount= %i)\n",
            newRule.srcIpLo,newRule.srcIpHi,newRule.destIpLo,newRule.destIpHi,
            flowPairs[newRule.actionType].c_str(),newRule.actionType,newRule.pri,newRule.pktCount);
    //Sometimes a switch can ask for the same rule multiple times, since it does not wait to receive a rule.
    //To prevent this, check if the rule we have was just added
    bool duplicateRule = false;
    for(int i=0;i<flowTable.size();i++){
        if (newRule.srcIpHi==flowTable[i].srcIpHi&&
            newRule.srcIpLo==flowTable[i].srcIpLo&&
            newRule.destIpHi==flowTable[i].destIpHi&&
            newRule.destIpLo==flowTable[i].destIpLo&&
            newRule.actionType==flowTable[i].actionType){
                duplicateRule = true;
        }
    }
    if(!duplicateRule){
        flowTable.push_back(newRule);
    }

    //now that the new rule is added, go through all todoTraffic in the traffic list.
    //Mark any traffic resolved as true, and add them to a list to remove from.
    vector<bool> trafResolved;

    for(int i=0;i<todoList.size();i++){
        bool resolved = false;
        for(int j=flowTable.size()-1;j>=0;j--){
            //if traffic ip matches on in the rule set
            if(todoList[i].ipSrc <= flowTable[j].srcIpHi && todoList[i].ipSrc >= flowTable[j].srcIpLo){
                if(todoList[i].ipDst <= flowTable[j].destIpHi && todoList[i].ipDst >= flowTable[j].destIpLo){
                    flowTable[j].pktCount += 1;
                    //foundRule = flowTable[j];
                    resolved = true;
                    flow_rule foundRule = flowTable[j];
                    switch (foundRule.actionType){

                        case FORWARD:
                            //Forward the package
                            break;
                        case DROP:
                            //Drop the package
                            break;
                        case SEND:
                            //Send the package to a switch
                            char buf[MAX_BUFF];
                            string destSwitch = (foundRule.actionVal == SEND_LEFT)?to_string(swj):to_string(swk);
                            string fifoDirWrite = "./fifo-"+to_string(swi)+"-"+destSwitch;
                            int fileDescWrite = open(fifoDirWrite.c_str(),O_WRONLY|O_NONBLOCK);
                            if (fileDescWrite<0){
                                perror("Error in opening send switch writeFIFO");
                                cout<<fifoDirWrite<<endl;
                                exit(EXIT_FAILURE);
                            }
                            fd[foundRule.actionVal][1] = fileDescWrite;


                            string relayPacket = to_string(RELAY)+" "+to_string(todoList[i].ipSrc)+" "+to_string(todoList[i].ipDst);
                            int dstSwitchNum;
                            if (foundRule.actionVal == SEND_LEFT){
                                fdPrint(fd[SWJ_FD][1], buf, relayPacket);
                                dstSwitchNum=swj;
                            }
                            else if (foundRule.actionVal == SEND_RIGHT){
                                fdPrint(fd[SWK_FD][1], buf, relayPacket);
                                dstSwitchNum=swk;
                            }
                            pStat.tRelay++;
                            printf("Transmitted (src= sw%i, dest= sw%i) [RELAY]:  header= (srcIP= %i, destIP= %i)\n",
                                   swi,dstSwitchNum,todoList[i].ipSrc,todoList[i].ipDst);
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

//Code that is used in the setup and loop of the virtual switch.
void progSwitch(int swi, int swj,int swk,int ipLow,int ipHigh){
    int timeout = 0;
    int fd[4][2];
    struct pollfd pollfd[3];
    string fifoDirWrite = "./fifo-"+to_string(swi)+"-0";
    string fifoDirRead = "./fifo-0-"+to_string(swi);


    //Open controller FIFO
    mkfifo(fifoDirRead.c_str(),(mode_t) 0777);
    int fileDescRead = open(fifoDirRead.c_str(),O_RDONLY|O_NONBLOCK);
    if (fileDescRead<0){
        perror("Error in opening controller readFIFO");
        exit(EXIT_FAILURE);
    }
    else{
        //add the controller to the pollfd list and fd list
        fd[CONT_FD][0] = fileDescRead;
        pollfd[CONT_FD].fd = fileDescRead;
        pollfd[CONT_FD].events = POLLIN;
        pollfd[CONT_FD].revents = 0;
    }
    int fileDescWrite = open(fifoDirWrite.c_str(),O_WRONLY|O_NONBLOCK);

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
        mkfifo(fifoSwjRead.c_str(),(mode_t) 0777);
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
        mkfifo(fifoSwkRead.c_str(),(mode_t) 0777);
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

    //Initialize a file descriptor for standard input
    struct pollfd keyboardFd[1];
    keyboardFd[0].fd = STDOUT_FILENO;
    keyboardFd[0].events=POLLIN;
    keyboardFd[0].revents=0;

    char outBuf[MAX_BUFF];
    char inBuf[MAX_BUFF];
    int inLen;

    //Send OPEN Packet
    string openPacket = to_string(OPEN)+" "+to_string(swi)+" "
            +to_string(swj)+" "+to_string(swk)+" "
            +to_string(ipLow)+" "+to_string(ipHigh) ;
    fdPrint(fd[CONT_FD][1],outBuf,openPacket);
    printf("Transmitted (src= sw%i, dest= cont) [OPEN]:\n",swi);
    printf("(port0= cont, port1= %i, port2= %i, port3= %i-%i)\n",swj,swk,ipLow,ipHigh);
    pStat.tOpen++;

    //Block switch until it has recieved awknowledgement. We need to do this for sync purposes.
    //While concurrency is important for this assignment all concurrency is meaningless if initial
    //communication between switch and controller isn't established.
    memset(inBuf, 0,MAX_BUFF);
    while(read(fd[CONT_FD][0], inBuf, MAX_BUFF)<0) {}
    pStat.rAck++;
    printf("Received (src= cont, dest= sw%i) [ACK]\n",swi);

    string trafLine;

    while(1){
        //read line from traffic file
        if (trafficFile.is_open()) {
            if(getline(trafficFile,trafLine)){
                vector<string> trafTokens = tokenize(trafLine);

                if (trafTokens.size() > 0 && trafTokens[0] == (string)"sw"+to_string(swi)){
                    //admit "packet"
                    pStat.rAdmit++;
                    findFlowRule(stoi(trafTokens[1]),stoi(trafTokens[2]),swi,swj,swk,fd,flowTable,todoList);
                }
            }
        }
        //poll keyboard

        int rvalKeyboard=poll(keyboardFd,KEYBOARDFD_NUM,timeout);
        if (rvalKeyboard < 0){
            perror("Error in polling in controller");
            exit(EXIT_FAILURE);
        }
        else if (rvalKeyboard == 0 ); //Do Nothing
        else{
            if(keyboardFd[0].revents & POLLIN){
                memset(inBuf, 0, MAX_BUFF);
                inLen = read(0,inBuf,MAX_BUFF);

                string output = (string) inBuf;

                if (output == (string)"list\n"){
                    //print the flow table
                    cout<<"Flow table:"<<endl;
                    for(int i=0; i<flowTable.size();i++){
                        printf("[%i] (srcIp= %i-%i, destIP= %i-%i action= %s:%i, pri= %i, pktCount= %i)\n",i,flowTable[i].srcIpLo,
                                flowTable[i].srcIpHi,flowTable[i].destIpLo,flowTable[i].destIpHi,
                                flowPairs[flowTable[i].actionType].c_str(),flowTable[i].actionType,flowTable[i].pri,flowTable[i].pktCount);
                    }
                    //print the packet stats
                    cout<<"Packet Stats:"<<endl;
                    printf("Recieved:      ADMIT:%i, ACK:%i, ADDRULE:%i, RELAYIN:%i\n",pStat.rAdmit,pStat.rAck,pStat.rAdd,pStat.rRelay);
                    printf("Transmitted:   OPEN:%i, QUERY:%i, RELAYOUT:%i\n",pStat.tOpen,pStat.tQuery,pStat.tRelay);
                }
                else if (output == (string)"exit\n"){
                    cout<<"Exiting..."<<endl;
                    exit(0);
                }
                else{
                    cout<<"Unknown input command."<<endl;
                }
            }
        }


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
                    //determine what switch (if any) the fd belongs to.
                    int sendingSwitch = (i==SWK_FD)?swk:swj;
                    memset(inBuf, 0, MAX_BUFF);
                    inLen = read(fd[i][0],inBuf,MAX_BUFF);

                    string output = (string) inBuf;
                    vector<string>tokens = tokenize(output);
                    switch(stoi(tokens[0])){
                        case ACK:
                            //Ack package already recieved.
                            break;

                        case RELAY:
                            //cout<<"Got RELAY"<<endl;
                            pStat.rRelay++;
                            printf("Received (src= sw%i, dest= sw%i) [RELAY]:  header= (srcIP= %i, destIP= %i)\n",
                                    sendingSwitch,swi,stoi(tokens[1]),stoi(tokens[2]));
                            findFlowRule(stoi(tokens[1]),stoi(tokens[2]),swi,swj,swk,fd,flowTable,todoList);
                            break;

                        case ADD:
                            //cout<<"Got ADD"<<endl;
                            pStat.rAdd++;
                            handleAdd(tokens,flowTable,todoList,swi,swj,swk,fd);
                            break;
                    }

                }
            }
        }
    }
}

//Main function. Handles all arguments and either executes progSwitch() or progCont(),
//depending on what is requested of it.
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
    if (argc <= 1){
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

            //Execute software defined controller.
            progController(nSwitchInt);
            exit(0);
        }
    }
    else if(argv[1][0]=='s'&&argv[1][1]=='w'){
        char swiNum = argv[1][2];
        int swi = (int) swiNum-48;
        string trafficFileName = (string)argv[2];
        //test if traffic file is legitimate.
        trafficFile.open(trafficFileName);
        if(!trafficFile.is_open()){
            pExit("Error: Traffic file cannot open");
        }
        char * swjIn = argv[3];
        char * swkIn = argv[4];
        int swj,swk;

        if (swjIn==string("null")){
            swj = -1;
        }
        else if(isdigit(swjIn[2])){
            swj = (int) swjIn[2]-'0';

        }
        else{
            pExit("Error: swj is invalid");
        }

        if (swkIn==string("null")){
            swk = -1;
        }
        else if(isdigit(swkIn[2])){
            swk = swkIn[2]-'0';

        }
        else{
            pExit("Error: swk is invalid");
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


        //Execute software defined switch
        progSwitch(swi,swj,swk,ipLow,ipHigh);
    }
    else {
        pExit("Error: first argument is not 'cont' nor 'swi'");
    }

}

