#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <winsock2.h>
#include <string.h>
#include <windows.h>
#include <process.h> 

#define NAMESIZE 20

#define FNameMax 32             /* Max length of File Name */
#define FileMax  32				/* Max number of Files */
#define baseM	 6				/* base number */
#define ringSize 64				/* ringSize = 2^baseM */
#define fBufSize 1024			/* file buffer size */

typedef struct {                /* Node Info Type Structure */
	int ID;                     /* ID */
	struct sockaddr_in addrInfo;/* Socket address */
} nodeInfoType;

typedef struct {				/* File Type Structure */
	char Name[FNameMax];	    /* File Name */
	int  Key;					/* File Key */
	nodeInfoType owner;			/* Owner's Node */
	nodeInfoType refOwner;		/* Ref Owner's Node */
} fileRefType;

typedef struct {					/* Global Information of Current Files */
	unsigned int fileNum;			/* Number of files */
	fileRefType  fileRef[FileMax];	/* The Number of Current Files */
} fileInfoType;

typedef struct {			    /* Finger Table Structure */
	nodeInfoType Pre;		    /* Predecessor pointer */
	nodeInfoType finger[baseM];	/* Fingers (array of pointers) */
} fingerInfoType;

typedef struct {                /* Chord Information Structure */
	fileInfoType   FRefInfo;	/* File Ref Own Information */
	fingerInfoType fingerInfo;	/* Finger Table Information */
} chordInfoType;

typedef struct {				/* Node Structure */
	nodeInfoType  nodeInfo;     /* Node's IPv4 Address */
	fileInfoType  fileInfo;     /* File Own Information */
	chordInfoType chordInfo;    /* Chord Data Information */
} nodeType;

typedef struct {
	unsigned short msgID;      // message ID
	unsigned short msgType;	   // message type (0: request, 1: response)
	nodeInfoType   nodeInfo;   // node address info 
	short          moreInfo;   // more info 
	fileRefType    fileInfo;   // file (reference) info
	unsigned int   bodySize;   // body size in Bytes
} chordHeaderType;             // CHORD message header type


void procRecvMsg(void *);
// thread function for handling receiving messages 

void procPPandFF(void *);
// thread function for sending ping messages and fixfinger 

int recvn(SOCKET s, char *buf, int len, int flags);
// For receiving a file

unsigned strHash(const char *);
// A Simple Hash Function from a string to the ID/key space

int twoPow(int power);
// For getting a power of 2 

int modMinus(int modN, int minuend, int subtrand);
// For modN modular operation of "minend - subtrand"

int modPlus(int modN, int addend1, int addend2);
// For modN modular operation of "addend1 + addend2"

int modIn(int modN, int targNum, int range1, int range2, int leftmode, int rightmode);
// For checking if targNum is "in" the range using left and right modes 
// under modN modular environment 

char *fgetsCleanup(char *);
// For handling fgets function

void flushStdin(void);
// For flushing stdin

void showCommand(void);
// For showing commands

void recvFile(void *arg);
// For recvFile

nodeType myNode = { 0 };               // node information -> global variable
SOCKET rqSock, rpSock, flSock, frSock, fsSock, pfSock, ffSock;
int sMode = 1; // silent mode
int recvFileSize;
char recvFileName[FNameMax + 1];
HANDLE hMutex;

int main(int argc, char *argv[])
{
	WSADATA wsaData;
	HANDLE hThread[2];
	HANDLE recvThread;
	int exitFlag = 0; // indicates termination condition
	char command[7];
	char cmdChar = '\0';
	int joinFlag = 0; // indicates the join/create status
	char tempIP[16];
	char tempPort[6];
	char fileName[FNameMax + 1];
	char fileBuf[fBufSize];
	char strSockAddr[21];
	struct sockaddr_in peerAddr, targetAddr;
	chordHeaderType tempMsg, bufMsg;
	int optVal = 5000;  // 5 seconds
	int retVal; // return value
	nodeInfoType succNode, predNode, targetNode;
	fileInfoType keysInfo;
	fileRefType refInfo;
	FILE *fp;
	int i, j, targetKey, addrSize, fileSize, numTotal, searchResult, resultFlag;
	int key_count = 0;

	/* step 0: Program Initialization  */
	/* step 1: Commnad line argument handling  */
	/* step 2: Winsock handling */
	/* step 3: Prompt handling (loop) */
	/* step 4: User input processing (switch) */
	/* step 5: Program termination */

	/* step 0 */

	printf("*****************************************************************\n");
	printf("*      DHT-Based P2P Protocol (CHORD) Node Controller           *\n");
	printf("*                  Ver. 0.1      Oct. 17, 2018                  *\n");
	printf("*            (04조) Kim, Ho-han  Son, Hyeon-Cheol               *\n");
	printf("*****************************************************************\n\n");

	/* step 1: Commnad line argument handling  */

	myNode.nodeInfo.addrInfo.sin_family = AF_INET;

	if (argc != 3) {
		printf("\a[ERROR] Usage : %s <IP Addr> <Port No(49152~65535)>\n", argv[0]);
		exit(1);
	}

	if ((myNode.nodeInfo.addrInfo.sin_addr.s_addr = inet_addr(argv[1])) == INADDR_NONE) {
		printf("\a[ERROR] <IP Addr> is wrong!\n");
		exit(1);
	}

	if (atoi(argv[2]) > 65535 || atoi(argv[2]) < 49152) {
		printf("\a[ERROR] <Port No> should be in [49152, 65535]!\n");
		exit(1);
	}

	myNode.nodeInfo.addrInfo.sin_port = htons(atoi(argv[2]));

	strcpy(strSockAddr, argv[2]);
	strcat(strSockAddr, argv[1]);
	printf("strSoclAddr: %s\n", strSockAddr);
	myNode.nodeInfo.ID = strHash(strSockAddr);

	printf(">>> Welcome to ChordNode Program! \n");
	printf(">>> Your IP address: %s, Port No: %d, ID: %d \n", argv[1], atoi(argv[2]), myNode.nodeInfo.ID);
	printf(">>> Silent Mode is ON!\n\n");

	/* step 2: Winsock handling */

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) { /* Load Winsock 2.2 DLL */
		printf("\a[ERROR] WSAStartup() error!");
		exit(1);
	}

	/* step 3: Prompt handling (loop) */

	showCommand();

	do {

		while (1) {
			printf("CHORD> \n");
			printf("CHORD> Enter your command ('help' for help message).\n");
			printf("CHORD> ");
			fgets(command, sizeof(command), stdin);
			fgetsCleanup(command);
			if (!strcmp(command, "c") || !strcmp(command, "create"))
				cmdChar = 'c';
			else if (!strcmp(command, "j") || !strcmp(command, "join"))
				cmdChar = 'j';
			else if (!strcmp(command, "l") || !strcmp(command, "leave"))
				cmdChar = 'l';
			else if (!strcmp(command, "a") || !strcmp(command, "add"))
				cmdChar = 'a';
			else if (!strcmp(command, "d") || !strcmp(command, "delete"))
				cmdChar = 'd';
			else if (!strcmp(command, "s") || !strcmp(command, "search"))
				cmdChar = 's';
			else if (!strcmp(command, "f") || !strcmp(command, "finger"))
				cmdChar = 'f';
			else if (!strcmp(command, "i") || !strcmp(command, "info"))
				cmdChar = 'i';
			else if (!strcmp(command, "h") || !strcmp(command, "help"))
				cmdChar = 'h';
			else if (!strcmp(command, "m") || !strcmp(command, "mute"))
				cmdChar = 'm';
			else if (!strcmp(command, "q") || !strcmp(command, "quit"))
				cmdChar = 'q';
			else if (!strlen(command))
				continue;
			else {
				printf("\a[ERROR] Wrong command! Input a correct command.\n\n");
				continue;
			}
			break;
		}

		/* step 4: User input processing (switch) */

		switch (cmdChar) {
		case 'c':
			if (joinFlag) {
				printf("\a[ERROR] You are currently in the network; You cannot create the network!\n\n");
				continue;
			}
			joinFlag = 1;
			printf("CHORD> You have created a chord network!\n");

			// fill up the finger table information with myself
			myNode.chordInfo.fingerInfo.Pre = myNode.nodeInfo;

			WaitForSingleObject(hMutex, INFINITE);
			for (i = 0; i < baseM; i++)
				myNode.chordInfo.fingerInfo.finger[i] = myNode.nodeInfo;
			ReleaseMutex(hMutex);

			printf("CHORD> Your finger table has been updated!\n");

			// UDP sockets creation for request and response
			rqSock = socket(AF_INET, SOCK_DGRAM, 0); // for request
			rpSock = socket(AF_INET, SOCK_DGRAM, 0); // for response
													 //ffSock = socket(AF_INET, SOCK_DGRAM, 0);
													 //pfSock = socket(AF_INET, SOCK_DGRAM, 0);

			retVal = setsockopt(rqSock, SOL_SOCKET, SO_RCVTIMEO, (char *)&optVal, sizeof(optVal));
			if (retVal == SOCKET_ERROR) {
				printf("\a[ERROR] setsockopt() Error!\n");
				exit(1);
			}

			if (bind(rpSock, (struct sockaddr *) &myNode.nodeInfo.addrInfo, sizeof(myNode.nodeInfo.addrInfo)) < 0) {
				printf("\a[ERROR] Response port bind failed!\n");
				exit(1);
			}

			flSock = socket(AF_INET, SOCK_STREAM, 0); // for accepting file down request 

			if (bind(flSock, (SOCKADDR*)&myNode.nodeInfo.addrInfo, sizeof(myNode.nodeInfo.addrInfo)) == SOCKET_ERROR) {
				printf("\a[ERROR] bind() error!\n");
				exit(1);
			}

			retVal = listen(flSock, SOMAXCONN);
			if (retVal == SOCKET_ERROR) {
				printf("\a[ERROR] listen() error!\n"); // for file sending
				exit(1);
			}

			// threads creation for processing incoming request message
			hThread[0] = (HANDLE)_beginthreadex(NULL, 0, (void *)procRecvMsg, (void *)&exitFlag, 0, NULL);
			// threads creation for processing sending ping message 
			hThread[1] = (HANDLE)_beginthreadex(NULL, 0, (void *)procPPandFF, (void *)&exitFlag, 0, NULL);
			// threads creation for processing recvFile
			recvThread = (HANDLE)_beginthreadex(NULL, 0, (void *)recvFile, (void *)&exitFlag, 0, NULL);
			break;
		case 'j':
			if (joinFlag) {
				printf("\a[ERROR] You are currently in the network; You cannot join again!\n\n");
				continue;
			}
			joinFlag = 1;
			// finger table initialization

			WaitForSingleObject(hMutex, INFINITE);
			myNode.chordInfo.fingerInfo.Pre.ID = -1;
			for (i = 0; i < baseM; i++)
				myNode.chordInfo.fingerInfo.finger[i].ID = -1;
			ReleaseMutex(hMutex);

			// UDP sockets creation for request and response
			rqSock = socket(AF_INET, SOCK_DGRAM, 0); // for request
			rpSock = socket(AF_INET, SOCK_DGRAM, 0); // for response
			pfSock = socket(AF_INET, SOCK_DGRAM, 0); // for pingpong
			ffSock = socket(AF_INET, SOCK_DGRAM, 0); // for pingpong

			memset(&peerAddr, 0, sizeof(peerAddr));
			peerAddr.sin_family = AF_INET;

			printf("CHORD> You need a helper node to join the existing network.\n");
			printf("CHORD> If you want to create a network, the helper node is yourself.\n");
			while (1) {
				printf("CHORD> Enter IP address of the helper node: ");
				fgets(tempIP, sizeof(tempIP), stdin);
				fgetsCleanup(tempIP);
				if ((peerAddr.sin_addr.s_addr = inet_addr(tempIP)) == INADDR_NONE) {
					printf("CHORD> \a[ERROR] <IP Addr> %s is wrong!\n", tempIP);
					continue;
				}
				else
					break;
			}

			while (1) {
				printf("CHORD> Enter port number of the helper node: ");
				fgets(tempPort, sizeof(tempPort), stdin);
				fgetsCleanup(tempPort);
				if (atoi(argv[2]) > 65535 || atoi(argv[2]) < 49152) {
					printf("CHORD> \a[ERROR] <Port No> should be in [49152, 65535]!\n");
					continue;
				}
				else
					break;
			}
			peerAddr.sin_port = htons(atoi(tempPort));

			if (!memcmp(&myNode.nodeInfo.addrInfo, &peerAddr, sizeof(struct sockaddr_in))) {
				printf("\a[ERROR] Helper node cannot be yourself in Joining!\n\n");
				joinFlag = 0;
				continue;
			}

			// create a joinInfo request message
			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 1;
			tempMsg.msgType = 0;
			tempMsg.nodeInfo = myNode.nodeInfo;
			tempMsg.moreInfo = 0;
			tempMsg.bodySize = 0;

			retVal = setsockopt(rqSock, SOL_SOCKET, SO_RCVTIMEO, (char *)&optVal, sizeof(optVal));
			if (retVal == SOCKET_ERROR) {
				printf("\a[ERROR] setsockopt() Error!\n");
				exit(1);
			}

			if (bind(rpSock, (struct sockaddr *) &myNode.nodeInfo.addrInfo, sizeof(myNode.nodeInfo.addrInfo)) < 0) {
				printf("\a[ERROR] Response port bind failed!\n");
				exit(1);
			}

			// send a joinInfo request message to the helper node
			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &peerAddr, sizeof(peerAddr));
			printf("CHORD> JoinInfo request Message has been sent.\n");

			// receive a joinInfo response message from the helper node
			retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, NULL, NULL);
			if (retVal == SOCKET_ERROR) {
				if (WSAGetLastError() == WSAETIMEDOUT) {
					printf("\a[ERROR] Request timed out!\n");
					joinFlag = 0;
					continue;
				}
				printf("\a[ERROR] Recvfrom Error!\n");
				joinFlag = 0;
				continue;
			}

			if ((bufMsg.msgID != 1) || (bufMsg.msgType != 1)) { // wrong msg
				printf("\a[ERROR] Wrong Message (Not JoinInfo Response) Received!\n");
				joinFlag = 0;
				continue;
			}

			if (bufMsg.moreInfo == -1) { // failure
				printf("\a[ERROR] JoinInfo Request Failed!\n");
				joinFlag = 0;
				continue;
			}

			printf("CHORD> JoinInfo response Message has been received.\n");

			// decode the joinInfo response message
			succNode = bufMsg.nodeInfo;

			WaitForSingleObject(hMutex, INFINITE);
			myNode.chordInfo.fingerInfo.finger[0] = succNode;
			ReleaseMutex(hMutex);

			printf("CHORD> You got your successor node from the helper node.\n");
			printf("CHORD> Successor IP Addr: %s, Port No: %d, ID: %d\n",
				inet_ntoa(succNode.addrInfo.sin_addr), ntohs(succNode.addrInfo.sin_port), succNode.ID);

			// create MoveKeys Request message
			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 2;
			tempMsg.msgType = 0;
			tempMsg.nodeInfo = myNode.nodeInfo;
			tempMsg.moreInfo = 0;
			tempMsg.bodySize = 0;

			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &myNode.chordInfo.fingerInfo.finger[0].addrInfo, sizeof(struct sockaddr));

			retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, NULL, NULL);
			if (retVal == SOCKET_ERROR) {
				if (WSAGetLastError() == WSAETIMEDOUT) {
					printf("\a[ERROR] Request timed out!\n");
					joinFlag = 0;
					continue;
				}
				printf("\a[ERROR] Recvfrom Error!\n");
				joinFlag = 0;
				continue;
			}

			// create Pre_info Request message
			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 3;
			tempMsg.msgType = 0;
			tempMsg.nodeInfo = myNode.nodeInfo;
			tempMsg.moreInfo = 0;
			tempMsg.bodySize = 0;

			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &myNode.chordInfo.fingerInfo.finger[0].addrInfo, sizeof(struct sockaddr));

			retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, NULL, NULL);
			if (retVal == SOCKET_ERROR) {
				if (WSAGetLastError() == WSAETIMEDOUT) {
					printf("\a[ERROR] Request timed out!\n");
					joinFlag = 0;
					continue;
				}
				printf("\a[ERROR] Recvfrom Error!\n");
				joinFlag = 0;
				continue;
			}

			// set Predecessor
			WaitForSingleObject(hMutex, INFINITE);
			myNode.chordInfo.fingerInfo.Pre = bufMsg.nodeInfo;
			ReleaseMutex(hMutex);

			// create Successor_Update Request message
			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 6;
			tempMsg.msgType = 0;
			tempMsg.nodeInfo = myNode.nodeInfo;
			tempMsg.moreInfo = 0;
			tempMsg.bodySize = 0;

			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &myNode.chordInfo.fingerInfo.Pre.addrInfo, sizeof(struct sockaddr));

			retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, NULL, NULL);
			//printf("%d\n", bufMsg.msgID);
			if (retVal == SOCKET_ERROR) {
				if (WSAGetLastError() == WSAETIMEDOUT) {
					printf("\a[ERROR] Successor_Update Request timed out!\n");
					joinFlag = 0;
					continue;
				}
				printf("\a[ERROR] Successor_Update Recvfrom Error!\n");
				joinFlag = 0;
				continue;
			}

			// create Pre_Update Request message
			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 4;
			tempMsg.msgType = 0;
			tempMsg.nodeInfo = myNode.nodeInfo;
			tempMsg.moreInfo = 0;
			tempMsg.bodySize = 0;

			// send Pre_Update Request message
			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &myNode.chordInfo.fingerInfo.finger[0].addrInfo, sizeof(struct sockaddr));

			retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, NULL, NULL);
			if (retVal == SOCKET_ERROR) {
				if (WSAGetLastError() == WSAETIMEDOUT) {
					printf("\a[ERROR] Pre_Update Request timed out!\n");
					joinFlag = 0;
					continue;
				}
				printf("\a[ERROR] Pre_Update Recvfrom Error!\n");
				joinFlag = 0;
				continue;
			}

			// threads creation for processing incoming request message
			hThread[0] = (HANDLE)_beginthreadex(NULL, 0, (void *)procRecvMsg, (void *)&exitFlag, 0, NULL);
			// threads creation for processing sending ping message 
			hThread[1] = (HANDLE)_beginthreadex(NULL, 0, (void *)procPPandFF, (void *)&exitFlag, 0, NULL);
			// threads creation for processing recvFile
			recvThread = (HANDLE)_beginthreadex(NULL, 0, (void *)recvFile, (void *)&exitFlag, 0, NULL);
			break;
		case 'l':
			// Send Leave Keys Request
			targetAddr = myNode.chordInfo.fingerInfo.finger[0].addrInfo;
			for (int i = 0; i < myNode.chordInfo.FRefInfo.fileNum; i++)
			{
				key_count++;
			}
			if (key_count != 0)
			{
				for (i = 0; i < myNode.chordInfo.FRefInfo.fileNum; i++)
				{
					memset(&tempMsg, 0, sizeof(tempMsg));
					tempMsg.msgID = 8;
					tempMsg.msgType = 0;
					tempMsg.nodeInfo = myNode.nodeInfo;
					tempMsg.fileInfo = myNode.chordInfo.FRefInfo.fileRef[i];
					tempMsg.moreInfo = key_count;
					tempMsg.bodySize = sizeof(myNode.chordInfo.FRefInfo.fileRef[i]);

					sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &targetAddr, sizeof(struct sockaddr));

					retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, NULL, NULL);
					if (retVal == SOCKET_ERROR) {
						if (WSAGetLastError() == WSAETIMEDOUT) {
							printf("\a[ERROR] Leave_Keys Request timed out!\n");
							continue;
						}
						printf("\a[ERROR] Leave_Keys Recvfrom Error!\n");
						continue;
					}
				}
			}

			// send Successor Update Request to myNode's predecessor
			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 6;
			tempMsg.msgType = 0;
			tempMsg.nodeInfo = myNode.chordInfo.fingerInfo.finger[0];
			tempMsg.bodySize = 0;

			targetAddr = myNode.chordInfo.fingerInfo.Pre.addrInfo;

			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &targetAddr, sizeof(struct sockaddr));


			retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, NULL, NULL);
			if (retVal == SOCKET_ERROR) {
				if (WSAGetLastError() == WSAETIMEDOUT) {
					printf("\a[ERROR] Suc_Update Request timed out!\n");
					continue;
				}
				printf("\a[ERROR] Suc_Update Recvfrom Error!\n");
				continue;
			}

			exitFlag = 1;
			printf("CHORD> Quitting the Program... \n");
			Sleep(2000);
			exit(1);
			break;
		case 'a':
			printf("CHORD> files to be added must be in the same folder where this program is located\n");
			printf("CHORD> Note that the maximum file name size is 32.\n");
			printf("CHORD> Enter the file name to add: ");
			scanf("%s", &fileName);
			targetKey = strHash(fileName);
			printf("CHORD> Input File Name: %s, Key: %d\n", fileName, targetKey);

			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 7;
			tempMsg.msgType = 0;
			tempMsg.nodeInfo = myNode.nodeInfo;
			tempMsg.moreInfo = targetKey;
			tempMsg.bodySize = 0;

			targetAddr = myNode.chordInfo.fingerInfo.Pre.addrInfo;

			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &targetAddr, sizeof(struct sockaddr));

			retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, NULL, NULL);
			if (retVal == SOCKET_ERROR) {
				if (WSAGetLastError() == WSAETIMEDOUT) {
					printf("\a[ERROR] Find Predecessor Request('a') timed out!\n");
					continue;
				}
				printf("\a[ERROR] Find Predecessor('a') Recvfrom Error!\n");
				continue;
			}

			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 5;
			tempMsg.msgType = 0;
			tempMsg.nodeInfo = bufMsg.nodeInfo;
			tempMsg.moreInfo = targetKey;
			tempMsg.bodySize = 0;

			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &bufMsg.nodeInfo.addrInfo, sizeof(bufMsg.nodeInfo.addrInfo));

			retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);
			if (retVal == SOCKET_ERROR) {
				if (WSAGetLastError() == WSAETIMEDOUT) {
					printf("\a[ERROR] Successor Info Request('a') timed out!\n");
				}
				printf("\a[ERROR] Successor Info Request('a') Recvfrom Error!\n");
			}

			printf("CHORD> File Successor IP addr: %s, Port No: %d, ID: %d\n", inet_ntoa(bufMsg.nodeInfo.addrInfo.sin_addr), ntohs(bufMsg.nodeInfo.addrInfo.sin_port), bufMsg.nodeInfo.ID);
			succNode = bufMsg.nodeInfo;

			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 9;
			tempMsg.msgType = 0;
			tempMsg.nodeInfo = succNode;
			tempMsg.moreInfo = targetKey;
			tempMsg.bodySize = 0;

			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &bufMsg.nodeInfo.addrInfo, sizeof(bufMsg.nodeInfo.addrInfo));

			retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);
			if (retVal == SOCKET_ERROR) {
				if (WSAGetLastError() == WSAETIMEDOUT) {
					printf("\a[ERROR] File Reference Add Request('a') timed out!\n");
				}
				printf("\a[ERROR] File Reference Add Request('a') Recvfrom Error!\n");
			}

			myNode.fileInfo.fileRef[myNode.fileInfo.fileNum].owner = myNode.nodeInfo;
			myNode.fileInfo.fileRef[myNode.fileInfo.fileNum].Key = strHash(fileName);
			strcpy(myNode.fileInfo.fileRef[myNode.fileInfo.fileNum].Name, fileName);
			myNode.fileInfo.fileRef[myNode.fileInfo.fileNum].refOwner = succNode;
			myNode.fileInfo.fileNum++;
			printf("CHORD> File Ref Info has been sent successfully to the Successor.\n");
			printf("CHORD> File Add has been successfully finished.\n");
			break;
		case 'd':
			if (!joinFlag) {
				printf("\a[ERROR] You are not in Network Press (c)reat or (j)oin \n\n");
				continue;
			}
			printf("CHORD> Enter the file name to add: ");
			fgets(fileName, sizeof(fileName), stdin);
			fgetsCleanup(fileName);
			targetKey = strHash(fileName);
			printf("CHORD> Input File Name: %s, Key: %d\n", fileName, targetKey);

			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 7;
			tempMsg.msgType = 0;
			tempMsg.nodeInfo = myNode.nodeInfo;
			tempMsg.moreInfo = targetKey;
			tempMsg.bodySize = 0;

			targetAddr = myNode.chordInfo.fingerInfo.Pre.addrInfo;

			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &targetAddr, sizeof(struct sockaddr));

			retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, NULL, NULL);
			if (retVal == SOCKET_ERROR) {
				if (WSAGetLastError() == WSAETIMEDOUT) {
					printf("\a[ERROR] Find Predecessor Request('a') timed out!\n");
					continue;
				}
				printf("\a[ERROR] Find Predecessor('a') Recvfrom Error!\n");
				continue;
			}

			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 5;
			tempMsg.msgType = 0;
			tempMsg.nodeInfo = bufMsg.nodeInfo;
			tempMsg.moreInfo = targetKey;
			tempMsg.bodySize = 0;

			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &bufMsg.nodeInfo.addrInfo, sizeof(bufMsg.nodeInfo.addrInfo));

			retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);
			if (retVal == SOCKET_ERROR) {
				if (WSAGetLastError() == WSAETIMEDOUT) {
					printf("\a[ERROR] Successor Info Request('a') timed out!\n");
				}
				printf("\a[ERROR] Successor Info Request('a') Recvfrom Error!\n");
			}

			printf("CHORD> File Successor IP addr: %s, Port No: %d, ID: %d\n", inet_ntoa(bufMsg.nodeInfo.addrInfo.sin_addr), ntohs(bufMsg.nodeInfo.addrInfo.sin_port), bufMsg.nodeInfo.ID);
			succNode = bufMsg.nodeInfo;

			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 10;
			tempMsg.msgType = 0;
			tempMsg.moreInfo = targetKey;
			tempMsg.bodySize = 0;

			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &succNode.addrInfo, sizeof(struct sockaddr));

			retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, NULL, NULL);
			if (retVal == SOCKET_ERROR) {
				if (WSAGetLastError() == WSAETIMEDOUT) {
					printf("\a[ERROR] File Delete Request timed out!\n");
					continue;
				}
				printf("\a[ERROR] File Delete Request Recvfrom Error!\n");
				continue;
			}

			memset(&myNode.fileInfo.fileRef[i], -1, sizeof(myNode.fileInfo.fileRef[i]));
			WaitForSingleObject(hMutex, INFINITE);
			for (j = i; j < myNode.fileInfo.fileNum; j++)
			{
				myNode.fileInfo.fileRef[j] = myNode.fileInfo.fileRef[j + 1];
			}
			myNode.fileInfo.fileNum--;
			ReleaseMutex(hMutex);
			printf("CHORD> File Ref Info has been sent successfully to the Successor.\n");
			printf("CHORD> File Delete has been successfully finished.\n");
			break;
		case 's':
			printf("CHORD> Input File name to search and download: ");
			fgets(fileName, sizeof(fileName), stdin);
			fgetsCleanup(fileName);
			targetKey = strHash(fileName);
			printf("\nCHORD> Input File Name: %s, Key: %d", fileName, targetKey);

			//for (i = 0; i < myNode.chordInfo.FRefInfo.fileNum; i++)
			//{
			//	if (myNode.chordInfo.FRefInfo.fileRef[i].Key == targetKey)
			//	{
			//		memset(&tempMsg, 0, sizeof(tempMsg));
			//		tempMsg.msgID = 12;
			//		tempMsg.msgType = 0;
			//		tempMsg.nodeInfo = myNode.nodeInfo;
			//		tempMsg.moreInfo = targetKey;
			//		tempMsg.bodySize = 0;

			//		sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
			//			(struct sockaddr *) &myNode.chordInfo.FRefInfo.fileRef[i].owner.addrInfo, sizeof(myNode.chordInfo.FRefInfo.fileRef[i].owner.addrInfo));

			//		retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);
			//		if (retVal == SOCKET_ERROR) {
			//			if (WSAGetLastError() == WSAETIMEDOUT) {
			//				printf("\a[ERROR] if File Reference Info Request('s') timed out!\n");
			//			}
			//			printf("\a[ERROR] if File Reference Info Request('s') Recvfrom Error!\n");
			//		}
			//	}
			//}
			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 7;
			tempMsg.msgType = 0;
			tempMsg.nodeInfo = myNode.nodeInfo;
			tempMsg.moreInfo = targetKey;
			tempMsg.bodySize = 0;

			targetAddr = myNode.chordInfo.fingerInfo.Pre.addrInfo;

			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &targetAddr, sizeof(struct sockaddr));

			retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, NULL, NULL);
			if (retVal == SOCKET_ERROR) {
				if (WSAGetLastError() == WSAETIMEDOUT) {
					printf("\a[ERROR] Find Predecessor Request('a') timed out!\n");
					continue;
				}
				printf("\a[ERROR] Find Predecessor('a') Recvfrom Error!\n");
				continue;
			}

			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 5;
			tempMsg.msgType = 0;
			tempMsg.nodeInfo = bufMsg.nodeInfo;
			tempMsg.moreInfo = targetKey;
			tempMsg.bodySize = 0;

			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &bufMsg.nodeInfo.addrInfo, sizeof(bufMsg.nodeInfo.addrInfo));

			retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);
			if (retVal == SOCKET_ERROR) {
				if (WSAGetLastError() == WSAETIMEDOUT) {
					printf("\a[ERROR] Successor Info Request('a') timed out!\n");
				}
				printf("\a[ERROR] Successor Info Request('a') Recvfrom Error!\n");
			}

			printf("CHORD> File Successor IP addr: %s, Port No: %d, ID: %d\n", inet_ntoa(bufMsg.nodeInfo.addrInfo.sin_addr), ntohs(bufMsg.nodeInfo.addrInfo.sin_port), bufMsg.nodeInfo.ID);
			succNode = bufMsg.nodeInfo;

			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 12;
			tempMsg.msgType = 0;
			strcpy(tempMsg.fileInfo.Name, fileName);
			//tempMsg.nodeInfo = myNode.nodeInfo;
			tempMsg.moreInfo = targetKey;
			tempMsg.bodySize = 0;

			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &succNode.addrInfo, sizeof(struct sockaddr));

			retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);
			if (retVal == SOCKET_ERROR) {
				if (WSAGetLastError() == WSAETIMEDOUT) {
					printf("\a[ERROR] File Reference Info Request('s') timed out!\n");
				}
				printf("\a[ERROR] File Reference Info Request('s') Recvfrom Error!\n");
			}

			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 11;
			tempMsg.msgType = 0;
			tempMsg.fileInfo = bufMsg.fileInfo;
			tempMsg.nodeInfo = myNode.nodeInfo;
			tempMsg.moreInfo = targetKey;
			tempMsg.bodySize = 0;

			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &bufMsg.fileInfo.owner.addrInfo, sizeof(bufMsg.fileInfo.owner.addrInfo));

			retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);
			if (retVal == SOCKET_ERROR) {
				if (WSAGetLastError() == WSAETIMEDOUT) {
					printf("\a[ERROR] File Reference Info Request('s') timed out!\n");
				}
				printf("\a[ERROR] File Reference Info Request('s') Recvfrom Error!\n");
			}
			if (bufMsg.moreInfo == -1)
			{
				printf("[ERROR] File Down Request('s') fopen ERROR!\n");
				break;
			}
			break;
		case 'f':
			printf("CHORD> Finger talbe Infomation: \n");
			printf("CHORD> My IP Addr: %s, Port No: %d, ID: %d\n",
				inet_ntoa(myNode.nodeInfo.addrInfo.sin_addr), ntohs(myNode.nodeInfo.addrInfo.sin_port), myNode.nodeInfo.ID);
			printf("CHORD> Predecessor IP Addr: %s, Port No: %d, ID: %d\n",
				inet_ntoa(myNode.chordInfo.fingerInfo.Pre.addrInfo.sin_addr), ntohs(myNode.chordInfo.fingerInfo.Pre.addrInfo.sin_port), myNode.chordInfo.fingerInfo.Pre.ID);
			for (i = 0; i < baseM; i++)
			{
				printf("CHORD> Finger[%d] IP Addr: %s, Port No: %d, ID: %d\n",
					i, inet_ntoa(myNode.chordInfo.fingerInfo.finger[i].addrInfo.sin_addr), ntohs(myNode.chordInfo.fingerInfo.finger[i].addrInfo.sin_port)
					, myNode.chordInfo.fingerInfo.finger[i].ID);
			}
			break;
		case 'i':
			printf("CHORD> My Node Information \n");
			printf("CHORD> My Node IP Addr: %s, Port No: %d, ID: %d\n",
				inet_ntoa(myNode.nodeInfo.addrInfo.sin_addr), ntohs(myNode.nodeInfo.addrInfo.sin_port), myNode.nodeInfo.ID);
			break;
		case 'm':
			if (sMode == 1)
			{
				sMode = 0;
				printf(">>> Silent Mode is OFF!\n\n");
			}
			else
			{
				sMode = 1;
				printf(">>> Silent Mode is ON!\n\n");
			}
			break;
		case 'h':
			showCommand();
			break;
		case 'help':
			showCommand();
			break;
		case 'q':
			exitFlag = 1;
			printf("CHORD> Quitting the Program... \n");
			break;
		}

	} while (cmdChar != 'q');

	/* step 5: Program termination */

	WaitForMultipleObjects(2, hThread, TRUE, INFINITE);

	closesocket(rqSock);
	closesocket(rpSock);
	closesocket(frSock);

	WSACleanup();

	printf("*************************  B  Y  E  *****************************\n");

	return 0;
}


void procRecvMsg(void *arg)
{
	struct sockaddr_in peerAddr, targetAddr;
	chordHeaderType tempMsg, bufMsg;
	nodeInfoType succNode, predNode, reqNode;
	int optVal = 5000;  // 5 seconds
	int retVal; // return value
	fileInfoType keysInfo;
	char fileBuf[fBufSize];
	FILE *fp;
	int i, j, targetKey, resultCode, keyNum, addrSize, fileSize, numRead, numTotal;

	int *exitFlag = (int *)arg;
	int key_count = 0, result = 0;

	retVal = setsockopt(rpSock, SOL_SOCKET, SO_RCVTIMEO, (char *)&optVal, sizeof(optVal));
	if (retVal == SOCKET_ERROR) {
		printf("\a[ERROR] setsockopt() Error!\n");
		exit(1);
	}
	addrSize = sizeof(peerAddr);
	while (!(*exitFlag)) {

		retVal = recvfrom(rpSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) &peerAddr, &addrSize);

		if (retVal == SOCKET_ERROR) {
			if (WSAGetLastError() == WSAETIMEDOUT) {
				if (!sMode)
					printf("CHORD> procRecvMsg recvfrom timed out.\n");
			}
			continue;
		}

		if (bufMsg.msgType != 0) {
			printf("\a[ERROR] Unexpected Response Message Received. Therefore Message Ignored!\n");
			continue;
		}
		switch (bufMsg.msgID) {
		case 0: // PingPong
			if (sMode == 0) {
				printf("CHORD> Pingpong Request Message Received!\n");
				printf("CHORD> Sender IP Addr: %s, Port No: %d\n",
					inet_ntoa(peerAddr.sin_addr), ntohs(peerAddr.sin_port));
			}
			// To be coded later
			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 0;
			tempMsg.msgType = 1;
			tempMsg.nodeInfo = myNode.nodeInfo;
			tempMsg.moreInfo = result;
			tempMsg.bodySize = 0;

			sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &peerAddr, sizeof(peerAddr));

			if (sMode == 0) {
				printf("CHORD> Pingpong Request Message  has been sent.\n");
			}
			break;
		case 1: // JoinInfo
			if (sMode == 0) {
				printf("CHORD> JoinInfo Request Message Received!\n");
				printf("CHORD> Sender IP Addr: %s, Port No: %d\n",
					inet_ntoa(peerAddr.sin_addr), ntohs(peerAddr.sin_port));
			}

			// initialization
			resultCode = -1;
			reqNode = bufMsg.nodeInfo;
			targetKey = modPlus(ringSize, bufMsg.nodeInfo.ID, 1);


			// successor check
			if (targetKey == myNode.nodeInfo.ID) {
				succNode = myNode.nodeInfo;
				resultCode = 0;
			}
			else if (myNode.nodeInfo.ID == myNode.chordInfo.fingerInfo.finger[0].ID) {
				succNode = myNode.nodeInfo;  // the Initial case
				resultCode = 0;
			}
			else if (modIn(ringSize, targetKey, myNode.nodeInfo.ID, myNode.chordInfo.fingerInfo.finger[0].ID, 0, 1)) {
				// my node itself is predecessor
				succNode = myNode.chordInfo.fingerInfo.finger[0];
				resultCode = 0;
			}
			if (resultCode == -1)
			{

				resultCode = 0;
				for (j = baseM - 1; j >= 0; j--) {
					if (myNode.chordInfo.fingerInfo.finger[j].ID == -1)
						continue;
					if (modIn(ringSize, myNode.chordInfo.fingerInfo.finger[j].ID, myNode.nodeInfo.ID, targetKey, 0, 0)) {
						targetAddr = myNode.chordInfo.fingerInfo.finger[j].addrInfo;
						break;
					}
				}

				// find predecessor request message 
				memset(&tempMsg, 0, sizeof(tempMsg));
				tempMsg.msgID = 7;
				tempMsg.msgType = 0;
				tempMsg.nodeInfo = myNode.nodeInfo;
				tempMsg.moreInfo = targetKey;
				tempMsg.bodySize = 0;

				sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
					(struct sockaddr *) &targetAddr, sizeof(targetAddr));

				retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);

				if (retVal == SOCKET_ERROR) {
					if (WSAGetLastError() == WSAETIMEDOUT) {
						printf("\a[ERROR] Find_Successor(Pre) Request timed out!\n");
					}
					printf("\a[ERROR] Find_Successor(Pre) Recvfrom Error!\n");
				}

				//  Successor Info request message 
				memset(&tempMsg, 0, sizeof(tempMsg));
				tempMsg.msgID = 5;
				tempMsg.msgType = 0;
				tempMsg.nodeInfo = myNode.nodeInfo;
				tempMsg.moreInfo = targetKey;
				tempMsg.bodySize = 0;

				sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
					(struct sockaddr *) &bufMsg.nodeInfo.addrInfo, sizeof(bufMsg.nodeInfo.addrInfo));

				retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);
				if (retVal == SOCKET_ERROR) {
					if (WSAGetLastError() == WSAETIMEDOUT) {
						printf("\a[ERROR] Find_Successor(Sccessor_Info) Request timed out!\n");
					}
					printf("\a[ERROR] Find_Successor(Sccessor_Info) Recvfrom Error!\n");
				}
				succNode = bufMsg.nodeInfo;
			} // !! You should code here!!

			  // create join info response message
			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 1;
			tempMsg.msgType = 1;
			tempMsg.nodeInfo = succNode;
			tempMsg.moreInfo = resultCode;
			tempMsg.bodySize = 0;

			// send a join info response message to the target node
			sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &peerAddr, sizeof(peerAddr));

			if (sMode == 0) {
				printf("CHORD> JoinInfo Response Message has been sent.\n");
				printf("CHORD> Result: %d, Successor IP Addr: %s, Port No: %d, ID: %d\n", resultCode,
					inet_ntoa(succNode.addrInfo.sin_addr), ntohs(succNode.addrInfo.sin_port), succNode.ID);
			}
			break;
		case 2: // Move_keys response
			if (sMode == 0) {
				printf("CHORD> Move_keys Request Message Received!\n");
				printf("CHORD> Sender IP Addr: %s, Port No: %d\n",
					inet_ntoa(peerAddr.sin_addr), ntohs(peerAddr.sin_port));
			}

			for (i = 0; i < myNode.chordInfo.FRefInfo.fileNum; i++)
			{
				if (modIn(ringSize, bufMsg.nodeInfo.ID, myNode.chordInfo.FRefInfo.fileRef[i].Key, myNode.nodeInfo.ID, 1, 0))
				{
					key_count++;
				}
			}
			if (key_count != 0)
			{
				WaitForSingleObject(hMutex, INFINITE);
				for (i = 0; i < myNode.chordInfo.FRefInfo.fileNum; i++)
				{
					if (modIn(ringSize, bufMsg.nodeInfo.ID, myNode.chordInfo.FRefInfo.fileRef[i].Key, myNode.nodeInfo.ID, 1, 0))
					{
						memset(&tempMsg, 0, sizeof(tempMsg));
						tempMsg.msgID = 2;
						tempMsg.msgType = 1;
						tempMsg.fileInfo = myNode.chordInfo.FRefInfo.fileRef[i];
						tempMsg.moreInfo = key_count;
						tempMsg.bodySize = sizeof(myNode.chordInfo.FRefInfo.fileRef[i]);

						sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &peerAddr, sizeof(struct sockaddr));
					}
				}
				for (i = 0; i < myNode.chordInfo.FRefInfo.fileNum; i++)
				{
					myNode.chordInfo.FRefInfo.fileRef[i] = myNode.chordInfo.FRefInfo.fileRef[i + 1];
				}
				myNode.chordInfo.FRefInfo.fileNum -= key_count;
				ReleaseMutex(hMutex);
			}
			else
			{
				memset(&tempMsg, 0, sizeof(tempMsg));
				tempMsg.msgID = 2;
				tempMsg.msgType = 1;
				tempMsg.moreInfo = key_count;

				sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0, (struct sockaddr *) &peerAddr, sizeof(struct sockaddr));
			}

			if (sMode == 0) {
				printf("CHORD> Move_keys response Message has been sent.\n");
			}
			break;
		case 3: // Predecessor Info Response
			if (sMode == 0) {
				printf("CHORD> Predecessor Info Request Message Received!\n");
				printf("CHORD> Sender IP Addr: %s, Port No: %d\n",
					inet_ntoa(peerAddr.sin_addr), ntohs(peerAddr.sin_port));
			}

			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 3;
			tempMsg.msgType = 1;
			tempMsg.nodeInfo = myNode.chordInfo.fingerInfo.Pre;
			tempMsg.moreInfo = result;
			tempMsg.bodySize = 0;

			sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &peerAddr, sizeof(struct sockaddr));

			if (sMode == 0) {
				printf("CHORD> Predecessor Info Response Message has been sent.\n");
			}
			break;
		case 4:// Predecessor Update Response
			if (sMode == 0) {
				printf("CHORD> Predecessor Update Request Message Received!\n");
				printf("CHORD> Sender IP Addr: %s, Port No: %d\n",
					inet_ntoa(peerAddr.sin_addr), ntohs(peerAddr.sin_port));
			}

			WaitForSingleObject(hMutex, INFINITE);
			myNode.chordInfo.fingerInfo.Pre = bufMsg.nodeInfo;
			ReleaseMutex(hMutex);
			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 4;
			tempMsg.msgType = 1;
			tempMsg.moreInfo = result; // succese
			tempMsg.bodySize = 0;

			sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &peerAddr, sizeof(struct sockaddr));

			if (sMode == 0) {
				printf("CHORD> Predecessor Update Response Message has been sent.\n");
			}
			break;
		case 5:// Successor Info Response
			if (sMode == 0) {
				printf("CHORD> Successor Info Request Message Received!\n");
				printf("CHORD> Sender IP Addr: %s, Port No: %d\n",
					inet_ntoa(peerAddr.sin_addr), ntohs(peerAddr.sin_port));
			}

			reqNode = bufMsg.nodeInfo;
			targetKey = bufMsg.moreInfo;

			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 5;
			tempMsg.msgType = 1;
			tempMsg.nodeInfo = myNode.chordInfo.fingerInfo.finger[0]; // succese
			tempMsg.bodySize = 0;

			sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &peerAddr, sizeof(struct sockaddr));

			if (sMode == 0) {
				printf("CHORD> Successor Info Response Message has been sent.\n");
			}
			break;
		case 6:// Sucessor Update Response
			if (sMode == 0) {
				printf("CHORD> Sucessor Update Request Message Received!\n");
				printf("CHORD> Sender IP Addr: %s, Port No: %d\n",
					inet_ntoa(peerAddr.sin_addr), ntohs(peerAddr.sin_port));
			}

			WaitForSingleObject(hMutex, INFINITE);
			myNode.chordInfo.fingerInfo.finger[0] = bufMsg.nodeInfo;
			ReleaseMutex(hMutex);
			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 6;
			tempMsg.msgType = 1;
			tempMsg.moreInfo = result; // success
			tempMsg.bodySize = 0;

			sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &peerAddr, sizeof(struct sockaddr));

			if (sMode == 0) {
				printf("CHORD> Sucessor Update Response Message has been sent.\n");
			}
			break;
		case 7:// FInd Predecessor Response
			if (sMode == 0) {
				printf("CHORD> FInd Predecessor Request Message Received!\n");
				printf("CHORD> Sender IP Addr: %s, Port No: %d\n",
					inet_ntoa(peerAddr.sin_addr), ntohs(peerAddr.sin_port));
			}

			resultCode = -1;
			reqNode = bufMsg.nodeInfo;
			targetKey = bufMsg.moreInfo;

			if (myNode.nodeInfo.ID == myNode.chordInfo.fingerInfo.finger[0].ID) {
				predNode = myNode.nodeInfo;  // the Initial case
				resultCode = 0;
			}
			else if (modIn(ringSize, targetKey, myNode.nodeInfo.ID, myNode.chordInfo.fingerInfo.finger[0].ID, 0, 1)) {
				// my node itself is predecessor
				predNode = myNode.nodeInfo;
				resultCode = 0;
			}

			if (resultCode == -1)
			{
				resultCode = 0;
				for (i = baseM - 1; i >= 0; i--) {
					if (myNode.chordInfo.fingerInfo.finger[i].ID == -1)
						continue;
					if (modIn(ringSize, myNode.chordInfo.fingerInfo.finger[i].ID, myNode.nodeInfo.ID, targetKey, 0, 0)) {
						targetAddr = myNode.chordInfo.fingerInfo.finger[i].addrInfo;
						break;
					}
				}

				// find predecessor request message 
				memset(&tempMsg, 0, sizeof(tempMsg));
				tempMsg.msgID = 7;
				tempMsg.msgType = 0; // request
				tempMsg.nodeInfo = myNode.nodeInfo;
				tempMsg.moreInfo = targetKey;
				tempMsg.bodySize = 0;

				sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
					(struct sockaddr *) &targetAddr, sizeof(struct sockaddr));

				retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);
				if (retVal == SOCKET_ERROR) {
					if (WSAGetLastError() == WSAETIMEDOUT) {
						printf("\a[ERROR] Find_Predecessor Request timed out!\n");
					}
					printf("\a[ERROR] Find_Predecessor Recvfrom Error!\n");
				}

				if ((bufMsg.msgID != 7) || (bufMsg.msgType != 1)) { // wrong msg
					printf("\a[ERROR] Wrong Message (Not Find_Predecessor Response) Received!\n");
				}

				if (bufMsg.moreInfo == -1) { // failure
					printf("\a[ERROR] Find_Predecessor Request Failed!\n");
				}

				// find predecessor response message 
				memset(&tempMsg, 0, sizeof(tempMsg));
				tempMsg.msgID = 7;
				tempMsg.msgType = 1; // reponse
				tempMsg.nodeInfo = bufMsg.nodeInfo;
				tempMsg.moreInfo = result;
				tempMsg.bodySize = 0;

				sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0,
					(struct sockaddr *) &peerAddr, sizeof(struct sockaddr));
			}
			else
			{
				// find predecessor response message 
				memset(&tempMsg, 0, sizeof(tempMsg));
				tempMsg.msgID = 7;
				tempMsg.msgType = 1;
				tempMsg.nodeInfo = predNode;
				tempMsg.moreInfo = result;
				tempMsg.bodySize = 0;

				sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0,
					(struct sockaddr *) &peerAddr, sizeof(struct sockaddr));
			}



			if (sMode == 0) {
				printf("CHORD> FInd Predecessor Response Message has been sent.\n");
			}
			break;
		case 8: // Leave Keys Response
			if (sMode == 0) {
				printf("CHORD> LeaveKeys Request Message Received!\n");
				printf("CHORD> Sender IP Addr: %s, Port No: %d\n",
					inet_ntoa(peerAddr.sin_addr), ntohs(peerAddr.sin_port));
			}

			reqNode = bufMsg.nodeInfo;

			WaitForSingleObject(hMutex, INFINITE);

			myNode.chordInfo.FRefInfo.fileRef[myNode.fileInfo.fileNum] = bufMsg.fileInfo;
			myNode.chordInfo.FRefInfo.fileNum++;

			ReleaseMutex(hMutex);

			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 8;
			tempMsg.msgType = 1;
			tempMsg.nodeInfo = myNode.nodeInfo;
			tempMsg.moreInfo = result;
			tempMsg.bodySize = 0;

			sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &peerAddr, sizeof(struct sockaddr));

			if (sMode == 0) {
				printf("CHORD>  Leave Keys Response Message has been sent.\n");
			}
			break;
		case 9: // File Reference Add Response
			if (sMode == 0) {
				printf("CHORD> File Reference Add Request Message Received!\n");
				printf("CHORD> Sender IP Addr: %s, Port No: %d\n",
					inet_ntoa(peerAddr.sin_addr), ntohs(peerAddr.sin_port));
			}

			reqNode = bufMsg.nodeInfo;

			WaitForSingleObject(hMutex, INFINITE);

			myNode.chordInfo.FRefInfo.fileRef[myNode.fileInfo.fileNum] = bufMsg.fileInfo;
			myNode.chordInfo.FRefInfo.fileNum++;

			ReleaseMutex(hMutex);

			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 8;
			tempMsg.msgType = 1;
			tempMsg.nodeInfo = myNode.nodeInfo;
			tempMsg.moreInfo = result;
			tempMsg.bodySize = 0;

			sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &peerAddr, sizeof(struct sockaddr));

			if (sMode == 0) {
				printf("CHORD> File Reference Add Response Message has been sent.\n");
			}
			break;
		case 10:// File Reference Delete Response
			if (sMode == 0) {
				printf("CHORD> File Reference Delete Request Message Received!\n");
				printf("CHORD> Sender IP Addr: %s, Port No: %d\n",
					inet_ntoa(peerAddr.sin_addr), ntohs(peerAddr.sin_port));
			}

			for (i = 0; i < FileMax; i++)
			{
				if (myNode.chordInfo.FRefInfo.fileRef[i].Key == bufMsg.moreInfo)
				{
					memset(&myNode.chordInfo.FRefInfo.fileRef[i], -1, sizeof(myNode.chordInfo.FRefInfo.fileRef[i]));
					WaitForSingleObject(hMutex, INFINITE);
					for (j = i; j < myNode.fileInfo.fileNum; j++)
					{
						myNode.chordInfo.FRefInfo.fileRef[j] = myNode.chordInfo.FRefInfo.fileRef[j + 1];
					}
					myNode.chordInfo.FRefInfo.fileNum--;
					ReleaseMutex(hMutex);
					break;
				}
			}
			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 10;
			tempMsg.msgType = 1;
			tempMsg.moreInfo = result; // succese
			tempMsg.bodySize = 0;

			sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &peerAddr, sizeof(struct sockaddr));

			if (sMode == 0) {
				printf("CHORD> File Reference Delete Response Message has been sent.\n");
			}
			break;

		case 11: // File Down Response
			if (sMode == 0) {
				printf("CHORD> File Down Request Message Received!\n");
				printf("CHORD> Sender IP Addr: %s, Port No: %d\n",
					inet_ntoa(peerAddr.sin_addr), ntohs(peerAddr.sin_port));
			}
			reqNode = bufMsg.nodeInfo;
			targetKey = bufMsg.moreInfo;
			numTotal = 0;

			for (i = 0; i < myNode.fileInfo.fileNum; i++)
			{
				if (myNode.fileInfo.fileRef[i].Key == targetKey)
				{
					break;
				}
			}

			fp = fopen(myNode.fileInfo.fileRef[i].Name, "rb");
			if (fp == NULL)
			{
				memset(&tempMsg, 0, sizeof(tempMsg));
				tempMsg.msgID = 11;
				tempMsg.msgType = 1;
				tempMsg.moreInfo = -1;

				sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0,
					(struct sockaddr *) &peerAddr, sizeof(struct sockaddr));
				break;
			}

			// 미구현
			/*retVal = connect(fsSock, (struct sockaddr *)&peerAddr, sizeof(struct sockaddr));
			if (retVal == SOCKET_ERROR) {
			printf("[ERROR] fsSock connect() failed\n");
			resultCode = -1;
			}*/


			fseek(fp, 0, SEEK_END);
			fileSize = ftell(fp);

			// 미구현
			//rewind(fp);
			/*
			while (1)
			{
			i++;
			numRead = fread(fileBuf, 1, fBufSize, fp);   //파일포인터로부터 내용을 읽어옴
			if (numRead > 0)
			{
			retVal = send(fsSock, fileBuf, numRead, 0);   //버퍼의 내용을 서버로 전송;
			numTotal += numRead;   //읽어온 총 길이를 구하는 변수
			}
			else if (numTotal == fileSize)   //읽어온 길이가 파일의 길이와 같을 시 파일 전송 완료
			{
			printf("CHORD> 파일 전송 완료!: %d 바이트\n", numTotal);
			break;
			}
			else
			{
			perror("파일 입출력 오류\n");
			break;
			}
			}*/
			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 11;
			tempMsg.msgType = 1;
			tempMsg.moreInfo = 0;
			tempMsg.bodySize = fileSize;
			sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &peerAddr, sizeof(struct sockaddr));
			break;

		case 12: // File Reference Info Response
			if (sMode == 0) {
				printf("CHORD> File Reference Info Request Message Received!\n");
				printf("CHORD> Sender IP Addr: %s, Port No: %d\n",
					inet_ntoa(peerAddr.sin_addr), ntohs(peerAddr.sin_port));
			}

			for (i = 0; i < myNode.chordInfo.FRefInfo.fileNum; i++)
			{
				if (myNode.chordInfo.FRefInfo.fileRef[i].Key == targetKey)
				{
					break;
				}
			}

			memset(&tempMsg, 0, sizeof(tempMsg));
			tempMsg.msgID = 12;
			tempMsg.msgType = 1;
			tempMsg.fileInfo = myNode.chordInfo.FRefInfo.fileRef[i];
			tempMsg.nodeInfo = myNode.nodeInfo;
			tempMsg.bodySize = 0;

			sendto(rpSock, (char *)&tempMsg, sizeof(tempMsg), 0,
				(struct sockaddr *) &peerAddr, sizeof(struct sockaddr));

			if (sMode == 0) {
				printf("CHORD> File Reference Info Response Message has been sent.\n");
			}
			break;

		}
	}
}

void procPPandFF(void *arg)
{
	int *exitFlag = (int *)arg;
	unsigned int delayTime, varTime;
	int retVal, optVal = 5000;
	int i, j, k, targetKey, resultCode;
	struct sockaddr_in peerAddr, leftaddr, targetAddr;
	chordHeaderType tempMsg, bufMsg;
	nodeInfoType succNode, predNode, reqNode, tempNode, leftNode;
	int LeftID;
	int pongCount = 0, OwnerpongCount = 0, refOwnerpongCount = 0;
	int finCount = 0;

	srand(time(NULL));

	pfSock = socket(AF_INET, SOCK_DGRAM, 0); // for ping-pong and fix-finger

	retVal = setsockopt(pfSock, SOL_SOCKET, SO_RCVTIMEO, (char *)&optVal, sizeof(optVal));
	if (retVal == SOCKET_ERROR) {
		printf("\a[ERROR] setsockopt() Error!\n");
		exit(1);
	}

	while (!(*exitFlag))
	{
		for (i = 1; i < baseM; i++)
		{
			targetKey = modPlus(ringSize, myNode.nodeInfo.ID, twoPow(i));
			// initialization
			resultCode = -1;

			// successor check
			if (targetKey == myNode.nodeInfo.ID) {
				succNode = myNode.nodeInfo;
				resultCode = 0;
			}
			else if (myNode.nodeInfo.ID == myNode.chordInfo.fingerInfo.finger[0].ID) {
				succNode = myNode.nodeInfo;  // the Initial case
				resultCode = 0;
			}
			else if (modIn(ringSize, targetKey, myNode.nodeInfo.ID, myNode.chordInfo.fingerInfo.finger[0].ID, 0, 1)) {
				// my node itself is predecessor
				succNode = myNode.chordInfo.fingerInfo.finger[0];
				resultCode = 0;
			}

			if (resultCode == -1)
			{

				resultCode = 0;
				for (j = baseM - 1; j >= 0; j--) {
					if (myNode.chordInfo.fingerInfo.finger[j].ID == -1)
						continue;
					if (modIn(ringSize, myNode.chordInfo.fingerInfo.finger[j].ID, myNode.nodeInfo.ID, targetKey, 0, 0)) {
						peerAddr = myNode.chordInfo.fingerInfo.finger[j].addrInfo;
						break;
					}
				}

				// find predecessor request message 
				memset(&tempMsg, 0, sizeof(tempMsg));
				tempMsg.msgID = 7;
				tempMsg.msgType = 0;
				tempMsg.nodeInfo = myNode.nodeInfo;
				tempMsg.moreInfo = targetKey;
				tempMsg.bodySize = 0;
				sendto(pfSock, (char *)&tempMsg, sizeof(tempMsg), 0,
					(struct sockaddr *) &peerAddr, sizeof(peerAddr));

				retVal = recvfrom(pfSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);
				if (retVal == SOCKET_ERROR) {
					if (WSAGetLastError() == WSAETIMEDOUT) {
						printf("\a[ERROR] Find_Successor(Pre) Request timed out!\n");
					}
					printf("\a[ERROR] Find_Successor(Pre) Recvfrom Error!\n");
				}

				//  Successor Info request message 
				memset(&tempMsg, 0, sizeof(tempMsg));
				tempMsg.msgID = 5;
				tempMsg.msgType = 0;
				tempMsg.nodeInfo = myNode.nodeInfo;
				tempMsg.moreInfo = targetKey;
				tempMsg.bodySize = 0;

				sendto(pfSock, (char *)&tempMsg, sizeof(tempMsg), 0,
					(struct sockaddr *) &bufMsg.nodeInfo.addrInfo, sizeof(bufMsg.nodeInfo.addrInfo));

				retVal = recvfrom(pfSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);
				if (retVal == SOCKET_ERROR) {
					if (WSAGetLastError() == WSAETIMEDOUT) {
						printf("\a[ERROR] Find_Successor(Sccessor_Info) Request timed out!\n");
					}
					printf("\a[ERROR] Find_Successor(Sccessor_Info) Recvfrom Error!\n");
				}

				WaitForSingleObject(hMutex, INFINITE);
				myNode.chordInfo.fingerInfo.finger[i] = bufMsg.nodeInfo;
				ReleaseMutex(hMutex);
			}
			else
			{
				WaitForSingleObject(hMutex, INFINITE);
				myNode.chordInfo.fingerInfo.finger[i] = succNode;
				ReleaseMutex(hMutex);
			}
		}

		// find predecessor request message 
		memset(&tempMsg, 0, sizeof(tempMsg));
		tempMsg.msgID = 7;
		tempMsg.msgType = 0;
		tempMsg.nodeInfo = myNode.nodeInfo;
		tempMsg.moreInfo = myNode.nodeInfo.ID;
		tempMsg.bodySize = 0;
		sendto(pfSock, (char *)&tempMsg, sizeof(tempMsg), 0,
			(struct sockaddr *) &myNode.chordInfo.fingerInfo.finger[0].addrInfo, sizeof(myNode.chordInfo.fingerInfo.finger[0].addrInfo));

		retVal = recvfrom(pfSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);
		if (retVal == SOCKET_ERROR) {
			if (WSAGetLastError() == WSAETIMEDOUT) {
				printf("\a[ERROR] Find_Successor(Pre) Request timed out!\n");
			}
			printf("\a[ERROR] Find_Successor(Pre) Recvfrom Error!\n");
		}
		WaitForSingleObject(hMutex, INFINITE);
		myNode.chordInfo.fingerInfo.Pre = bufMsg.nodeInfo;
		ReleaseMutex(hMutex);


		// doing ping-pong
		for (i = 0; i < baseM + 1; i++)
		{
			if (i == 0)
			{
				leftaddr = myNode.chordInfo.fingerInfo.Pre.addrInfo;
				LeftID = myNode.chordInfo.fingerInfo.Pre.ID;
			}
			else
			{
				leftaddr = myNode.chordInfo.fingerInfo.finger[i - 1].addrInfo;
				LeftID = myNode.chordInfo.fingerInfo.finger[i - 1].ID;
			}
			for (j = 0; j < 3; j++)
			{
				memset(&tempMsg, 0, sizeof(tempMsg));
				tempMsg.msgID = 0;
				tempMsg.msgType = 0;
				tempMsg.nodeInfo = myNode.nodeInfo;
				tempMsg.bodySize = 0;
				sendto(pfSock, (char *)&tempMsg, sizeof(tempMsg), 0,
					(struct sockaddr *) &leftaddr, sizeof(struct sockaddr));

				retVal = recvfrom(pfSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);
				if (retVal == SOCKET_ERROR) {
					if (WSAGetLastError() == WSAECONNRESET || WSAETIMEDOUT) {
						printf("\a[ERROR] PingPong Response timed out!\n");
						pongCount += 1;
						printf("%d\n", pongCount);
					}
					printf("\a[ERROR] Find_Predecessor Recvfrom Error!\n");
				}
				Sleep(500);
			}
			if (pongCount == 3)
			{
				if (LeftID == myNode.chordInfo.fingerInfo.Pre.ID)
				{
					WaitForSingleObject(hMutex, INFINITE);
					memset(&myNode.chordInfo.fingerInfo.Pre, 0, sizeof(myNode.chordInfo.fingerInfo.Pre));
					myNode.chordInfo.fingerInfo.Pre.ID = -1;
					ReleaseMutex(hMutex);
				}

				for (k = 1; k < baseM; k++)
				{
					if (myNode.chordInfo.fingerInfo.finger[k].ID == LeftID)
						continue;
					else
					{
						tempNode = myNode.chordInfo.fingerInfo.finger[k];
						break;
					}
				}

				for (j = baseM - 1; j >= 0; j--)
				{
					if (LeftID == myNode.chordInfo.fingerInfo.finger[j].ID)
					{
						if (j == baseM - 1)
						{
							WaitForSingleObject(hMutex, INFINITE);
							myNode.chordInfo.fingerInfo.finger[j] = myNode.chordInfo.fingerInfo.Pre;
							ReleaseMutex(hMutex);
						}
						else
						{
							WaitForSingleObject(hMutex, INFINITE);
							myNode.chordInfo.fingerInfo.finger[j] = myNode.chordInfo.fingerInfo.finger[j + 1];
							ReleaseMutex(hMutex);
						}
						if (j == 0)
						{
							while (1)
							{
								memset(&tempMsg, 0, sizeof(tempMsg));
								tempMsg.msgID = 3;
								tempMsg.msgType = 0;
								tempMsg.nodeInfo = myNode.nodeInfo;
								tempMsg.moreInfo = 0;
								tempMsg.bodySize = 0;

								sendto(pfSock, (char *)&tempMsg, sizeof(tempMsg), 0,
									(struct sockaddr *) &tempNode.addrInfo, sizeof(struct sockaddr));

								retVal = recvfrom(pfSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);
								if (retVal == SOCKET_ERROR) {
									if (WSAGetLastError() == WSAETIMEDOUT) {
										printf("\a[ERROR] Ping Pong pre-info Request timed out!\n");
										break;
									}
									printf("\a[ERROR] Ping Pong pre-info fail!\n");
									break;
								}

								if (bufMsg.nodeInfo.ID == -1 || bufMsg.nodeInfo.ID == LeftID)
								{
									WaitForSingleObject(hMutex, INFINITE);
									myNode.chordInfo.fingerInfo.finger[0] = tempNode;
									ReleaseMutex(hMutex);

									memset(&tempMsg, 0, sizeof(tempMsg));
									tempMsg.msgID = 4;
									tempMsg.msgType = 0;
									tempMsg.nodeInfo = myNode.nodeInfo;
									tempMsg.moreInfo = 0;
									tempMsg.bodySize = 0;

									sendto(pfSock, (char *)&tempMsg, sizeof(tempMsg), 0,
										(struct sockaddr *) &tempNode.addrInfo, sizeof(struct sockaddr));

									retVal = recvfrom(pfSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);
									if (retVal == SOCKET_ERROR) {
										if (WSAGetLastError() == WSAETIMEDOUT) {
											printf("\a[ERROR] Ping Pong pre-info Request timed out!\n");
											break;
										}
										printf("\a[ERROR] Ping Pong pre-info fail!\n");
										break;
									}
									break;
								}
								else
								{
									tempNode = bufMsg.nodeInfo;
								}
							}
						}
					}
				}
				for (i = 0; i <= baseM; i++)
				{
					if (i == baseM)
					{
						if (myNode.chordInfo.fingerInfo.Pre.ID == -1)
						{
							finCount++;
						}
					}
					else if (myNode.chordInfo.fingerInfo.finger[i].ID == -1)
					{
						finCount++;
					}
					if (finCount == baseM + 1)
					{
						for (int a = 0; a < baseM; a++)
						{
							WaitForSingleObject(hMutex, INFINITE);
							myNode.chordInfo.fingerInfo.finger[a] = myNode.nodeInfo;
							ReleaseMutex(hMutex);
						}
						WaitForSingleObject(hMutex, INFINITE);
						myNode.chordInfo.fingerInfo.Pre = myNode.nodeInfo;
						ReleaseMutex(hMutex);
					}
				}
			}// pingpong end
			pongCount = 0;
			//printf("pingpong ing~~!!\n");
			//printf("%d\n", i);
		}
		//printf("ing~~!!\n");

		// doing Ownerpingpong, refOwnerpingpong
		for (i = 0; i < myNode.fileInfo.fileNum; i++)
		{
			printf("ing~~!!\n");
			for (j = 0; j < 3; j++)
			{
				memset(&tempMsg, 0, sizeof(tempMsg));
				tempMsg.msgID = 0;
				tempMsg.msgType = 0;
				tempMsg.nodeInfo = myNode.nodeInfo;
				tempMsg.bodySize = 0;
				sendto(pfSock, (char *)&tempMsg, sizeof(tempMsg), 0,
					(struct sockaddr *) &myNode.fileInfo.fileRef[i].owner.addrInfo, sizeof(struct sockaddr));

				retVal = recvfrom(pfSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);
				if (retVal == SOCKET_ERROR) {
					if (WSAGetLastError() == WSAECONNRESET || WSAETIMEDOUT) {
						printf("\a[ERROR] PingPong Response timed out!\n");
						OwnerpongCount += 1;
						printf("%d\n", pongCount);
					}
					printf("\a[ERROR] Find_Predecessor Recvfrom Error!\n");
				}

				memset(&tempMsg, 0, sizeof(tempMsg));
				tempMsg.msgID = 0;
				tempMsg.msgType = 0;
				tempMsg.nodeInfo = myNode.nodeInfo;
				tempMsg.bodySize = 0;
				sendto(pfSock, (char *)&tempMsg, sizeof(tempMsg), 0,
					(struct sockaddr *) &myNode.fileInfo.fileRef[i].refOwner.addrInfo, sizeof(struct sockaddr));

				retVal = recvfrom(pfSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);
				if (retVal == SOCKET_ERROR) {
					if (WSAGetLastError() == WSAECONNRESET || WSAETIMEDOUT) {
						printf("\a[ERROR] PingPong Response timed out!\n");
						refOwnerpongCount += 1;
						printf("%d\n", pongCount);
					}
					printf("\a[ERROR] Find_Predecessor Recvfrom Error!\n");
				}
				Sleep(500);
			}
			if (OwnerpongCount == 3)
			{
				WaitForSingleObject(hMutex, INFINITE);
				for (j = i; j < myNode.chordInfo.FRefInfo.fileNum; j++)
				{
					myNode.chordInfo.FRefInfo.fileRef[j] = myNode.chordInfo.FRefInfo.fileRef[j + 1];
				}
				myNode.chordInfo.FRefInfo.fileNum--;
				memset(&myNode.chordInfo.FRefInfo.fileRef[j], -1, sizeof(myNode.chordInfo.FRefInfo.fileRef[j]));
				ReleaseMutex(hMutex);
			}
			OwnerpongCount = 0;
			printf("OwnerpongCount ing~~\n");

			if (refOwnerpongCount == 3)
			{

				resultCode = -1;
				reqNode = tempMsg.nodeInfo;
				targetKey = modPlus(ringSize, tempMsg.nodeInfo.ID, 1);

				// successor check
				if (targetKey == myNode.nodeInfo.ID) {
					succNode = myNode.nodeInfo;
					resultCode = 0;
				}
				else if (myNode.nodeInfo.ID == myNode.chordInfo.fingerInfo.finger[0].ID) {
					succNode = myNode.nodeInfo;  // the Initial case
					resultCode = 0;
				}
				else if (modIn(ringSize, targetKey, myNode.nodeInfo.ID, myNode.chordInfo.fingerInfo.finger[0].ID, 0, 1)) {
					// my node itself is predecessor
					succNode = myNode.chordInfo.fingerInfo.finger[0];
					resultCode = 0;
				}
				if (resultCode == -1)
				{

					resultCode = 0;
					for (j = baseM - 1; j >= 0; j--) {
						if (myNode.chordInfo.fingerInfo.finger[j].ID == -1)
							continue;
						if (modIn(ringSize, myNode.chordInfo.fingerInfo.finger[j].ID, myNode.nodeInfo.ID, targetKey, 0, 0)) {
							targetAddr = myNode.chordInfo.fingerInfo.finger[j].addrInfo;
							break;
						}
					}

					// find predecessor request message 
					memset(&tempMsg, 0, sizeof(tempMsg));
					tempMsg.msgID = 7;
					tempMsg.msgType = 0;
					tempMsg.nodeInfo = myNode.nodeInfo;
					tempMsg.moreInfo = targetKey;
					tempMsg.bodySize = 0;

					sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
						(struct sockaddr *) &targetAddr, sizeof(targetAddr));

					retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);

					if (retVal == SOCKET_ERROR) {
						if (WSAGetLastError() == WSAETIMEDOUT) {
							printf("\a[ERROR] Find_Successor(Pre) Request timed out!\n");
						}
						printf("\a[ERROR] Find_Successor(Pre) Recvfrom Error!\n");
					}

					//  Successor Info request message 
					memset(&tempMsg, 0, sizeof(tempMsg));
					tempMsg.msgID = 5;
					tempMsg.msgType = 0;
					tempMsg.nodeInfo = myNode.nodeInfo;
					tempMsg.moreInfo = targetKey;
					tempMsg.bodySize = 0;

					sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
						(struct sockaddr *) &bufMsg.nodeInfo.addrInfo, sizeof(bufMsg.nodeInfo.addrInfo));

					retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, (struct sockaddr *) 0, 0);
					if (retVal == SOCKET_ERROR) {
						if (WSAGetLastError() == WSAETIMEDOUT) {
							printf("\a[ERROR] Find_Successor(Sccessor_Info) Request timed out!\n");
						}
						printf("\a[ERROR] Find_Successor(Sccessor_Info) Recvfrom Error!\n");
					}
					succNode = bufMsg.nodeInfo;
				} // !! You should code here!!

				WaitForSingleObject(hMutex, INFINITE);
				myNode.fileInfo.fileRef[i].refOwner = succNode;
				ReleaseMutex(hMutex);

				// Leave Keys Request
				memset(&tempMsg, 0, sizeof(tempMsg));
				tempMsg.msgID = 8;
				tempMsg.msgType = 0;
				tempMsg.fileInfo.Key = myNode.fileInfo.fileRef[i].Key;
				strcpy(tempMsg.fileInfo.Name, myNode.fileInfo.fileRef[i].Name);
				tempMsg.fileInfo.owner = myNode.nodeInfo;
				tempMsg.fileInfo.refOwner = succNode;

				sendto(rqSock, (char *)&tempMsg, sizeof(tempMsg), 0,
					(struct sockaddr *) &succNode.addrInfo, sizeof(struct sockaddr));

				retVal = recvfrom(rqSock, (char *)&bufMsg, sizeof(bufMsg), 0, NULL, NULL);

				if (retVal == SOCKET_ERROR) {
					if (WSAGetLastError() == WSAETIMEDOUT) {
						printf("\a[ERROR] File_add Request timed out!\n");
						continue;
					}
					printf("\a[ERROR] File_add Recvfrom Error!\n");
					continue;
				}

			}
			refOwnerpongCount = 0;
			printf("refOwnerpongCount ing~~\n");
		} // Ownerpingpong, refOwnerpingpong end
	} // while 끝
	closesocket(pfSock);
}

void recvFile(void *arg)
{
	int addrSize;
	int numTotal;
	int fileSize;
	int retVal;
	int temp;
	int *exitFlag = (int *)arg;
	char fileBuf[1024];
	struct sockaddr_in peerAddr;
	FILE *fp;
	// file receive using tcp socket
	// accept()
	while (!(*exitFlag)) {
		if ((frSock = accept(flSock, (struct sockaddr *) &peerAddr, sizeof(struct sockaddr))))
		{
			//printf("ERROR> accept() failed\n");
			continue;
		}

		fileSize = recvFileSize;
		fp = fopen(recvFileName, "wb");
		if (fp == NULL) {
			printf("CHORD> But the file %s is in this node!\n", recvFileName);
			closesocket(frSock);
			continue;
		}
		// 파일 데이터 받기
		numTotal = 0;
		while (1) {
			retVal = recvn(frSock, fileBuf, fBufSize, 0);
			if (retVal == SOCKET_ERROR) {
				printf("\a[ERROR] recv()!\n");
				break;
			}
			else if (retVal == 0)
				break;
			else {
				fwrite(fileBuf, 1, retVal, fp);
				if (ferror(fp)) {
					printf("\a[ERROR] File I/O Error!\n");
					break;
				}
				numTotal += retVal;
			}
		}
		fclose(fp);

		if (numTotal == fileSize)
			printf("File %s has been received successfully!\n", recvFileName);
		else
			printf("\a[ERROR] File %s Receiving Fails!\n", recvFileName);

		// closesocket()
		closesocket(frSock);
	}
}
int modIn(int modN, int targNum, int range1, int range2, int leftmode, int rightmode)
// leftmode, rightmode: 0 => range boundary not included, 1 => range boundary included   
{
	int result = 0;

	if (range1 == range2) {
		if ((leftmode == 0) || (rightmode == 0))
			return 0;
	}

	if (modPlus(ringSize, range1, 1) == range2) {
		if ((leftmode == 0) && (rightmode == 0))
			return 0;
	}

	if (leftmode == 0)
		range1 = modPlus(ringSize, range1, 1);
	if (rightmode == 0)
		range2 = modMinus(ringSize, range2, 1);

	if (range1 < range2) {
		if ((targNum >= range1) && (targNum <= range2))
			result = 1;
	}
	else if (range1 > range2) {
		if (((targNum >= range1) && (targNum < modN))
			|| ((targNum >= 0) && (targNum <= range2)))
			result = 1;
	}
	else if ((targNum == range1) && (targNum == range2))
		result = 1;

	return result;
}

int twoPow(int power)
{
	int i;
	int result = 1;

	if (power >= 0)
		for (i = 0; i < power; i++)
			result *= 2;
	else
		result = -1;

	return result;
}

int modMinus(int modN, int minuend, int subtrand)
{
	if (minuend - subtrand >= 0)
		return minuend - subtrand;
	else
		return (modN - subtrand) + minuend;
}

int modPlus(int modN, int addend1, int addend2)
{
	if (addend1 + addend2 < modN)
		return addend1 + addend2;
	else
		return (addend1 + addend2) - modN;
}

void showCommand(void)
{
	printf("CHORD> Enter a command - (c)reate: Create the chord network\n");
	printf("CHORD> Enter a command - (j)oin  : Join the chord network\n");
	printf("CHORD> Enter a command - (l)eave : Leave the chord network\n");
	printf("CHORD> Enter a command - (a)dd   : Add a file to the network\n");
	printf("CHORD> Enter a command - (d)elete: Delete a file to the network\n");
	printf("CHORD> Enter a command - (s)earch: File search and download\n");
	printf("CHORD> Enter a command - (f)inger: Show the finger table\n");
	printf("CHORD> Enter a command - (i)nfo  : Show the node information\n");
	printf("CHORD> Enter a command - (m)ute  : Toggle the silent mode\n");
	printf("CHORD> Enter a command - (h)elp  : Show the help message\n");
	printf("CHORD> Enter a command - (q)uit  : Quit the program\n");
}

char *fgetsCleanup(char *string)
{
	if (string[strlen(string) - 1] == '\n')
		string[strlen(string) - 1] = '\0';
	else
		flushStdin();

	return string;
}

void flushStdin(void)
{
	int ch;

	fseek(stdin, 0, SEEK_END);
	if (ftell(stdin) > 0)
		do
			ch = getchar();
	while (ch != EOF && ch != '\n');
}

static const unsigned char sTable[256] =
{
	0xa3,0xd7,0x09,0x83,0xf8,0x48,0xf6,0xf4,0xb3,0x21,0x15,0x78,0x99,0xb1,0xaf,0xf9,
	0xe7,0x2d,0x4d,0x8a,0xce,0x4c,0xca,0x2e,0x52,0x95,0xd9,0x1e,0x4e,0x38,0x44,0x28,
	0x0a,0xdf,0x02,0xa0,0x17,0xf1,0x60,0x68,0x12,0xb7,0x7a,0xc3,0xe9,0xfa,0x3d,0x53,
	0x96,0x84,0x6b,0xba,0xf2,0x63,0x9a,0x19,0x7c,0xae,0xe5,0xf5,0xf7,0x16,0x6a,0xa2,
	0x39,0xb6,0x7b,0x0f,0xc1,0x93,0x81,0x1b,0xee,0xb4,0x1a,0xea,0xd0,0x91,0x2f,0xb8,
	0x55,0xb9,0xda,0x85,0x3f,0x41,0xbf,0xe0,0x5a,0x58,0x80,0x5f,0x66,0x0b,0xd8,0x90,
	0x35,0xd5,0xc0,0xa7,0x33,0x06,0x65,0x69,0x45,0x00,0x94,0x56,0x6d,0x98,0x9b,0x76,
	0x97,0xfc,0xb2,0xc2,0xb0,0xfe,0xdb,0x20,0xe1,0xeb,0xd6,0xe4,0xdd,0x47,0x4a,0x1d,
	0x42,0xed,0x9e,0x6e,0x49,0x3c,0xcd,0x43,0x27,0xd2,0x07,0xd4,0xde,0xc7,0x67,0x18,
	0x89,0xcb,0x30,0x1f,0x8d,0xc6,0x8f,0xaa,0xc8,0x74,0xdc,0xc9,0x5d,0x5c,0x31,0xa4,
	0x70,0x88,0x61,0x2c,0x9f,0x0d,0x2b,0x87,0x50,0x82,0x54,0x64,0x26,0x7d,0x03,0x40,
	0x34,0x4b,0x1c,0x73,0xd1,0xc4,0xfd,0x3b,0xcc,0xfb,0x7f,0xab,0xe6,0x3e,0x5b,0xa5,
	0xad,0x04,0x23,0x9c,0x14,0x51,0x22,0xf0,0x29,0x79,0x71,0x7e,0xff,0x8c,0x0e,0xe2,
	0x0c,0xef,0xbc,0x72,0x75,0x6f,0x37,0xa1,0xec,0xd3,0x8e,0x62,0x8b,0x86,0x10,0xe8,
	0x08,0x77,0x11,0xbe,0x92,0x4f,0x24,0xc5,0x32,0x36,0x9d,0xcf,0xf3,0xa6,0xbb,0xac,
	0x5e,0x6c,0xa9,0x13,0x57,0x25,0xb5,0xe3,0xbd,0xa8,0x3a,0x01,0x05,0x59,0x2a,0x46
};


#define PRIME_MULT 1717


unsigned int strHash(const char *str)  /* Hash: String to Key */
{
	unsigned int len = sizeof(str);
	unsigned int hash = len, i;


	for (i = 0; i != len; i++, str++)
	{

		hash ^= sTable[(*str + i) & 255];
		hash = hash * PRIME_MULT;
	}

	return hash % ringSize;
}

int recvn(SOCKET s, char *buf, int len, int flags)
{
	int received;
	char *ptr = buf;
	int left = len;

	while (left > 0) {
		received = recv(s, ptr, left, flags);
		if (received == SOCKET_ERROR)
			return SOCKET_ERROR;
		else if (received == 0)
			break;
		left -= received;
		ptr += received;
	}
	return (len - left);
}
