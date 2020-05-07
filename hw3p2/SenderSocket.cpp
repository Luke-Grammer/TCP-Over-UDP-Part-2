// SenderSocket.cpp
// CSCE 463-500
// Luke Grammer
// 11/12/19

#include "pch.h"

using namespace std;

/* Constructor initializes WinSock and sets up a UDP socket for RDP. 
 * In addition, the server also starts a timer for the life of the 
 * SenderSocket object. calls exit() if socket creation is unsuccessful. */
SenderSocket::SenderSocket(Properties* p)
{
	STRUCT WSAData wsaData;
	WORD wVerRequested;

	//initialize WinSock
	wVerRequested = MAKEWORD(2, 2);
	if (WSAStartup(wVerRequested, &wsaData) != 0) {
		printf("\tWSAStartup error %d\n", WSAGetLastError());
		WSACleanup();
		exit(EXIT_FAILURE);
	}

	// open a UDP socket
	sock = socket(AF_INET, SOCK_DGRAM, NULL);
	if (sock == INVALID_SOCKET)
	{
		printf("\tsocket() generated error %d\n", WSAGetLastError());
		WSACleanup();
		exit(EXIT_FAILURE);
	}

	// Bind socket to local machine
	struct sockaddr_in local;
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = INADDR_ANY;
	local.sin_port = NULL;

	if (bind(sock, (struct sockaddr*) &local, sizeof(local)) == SOCKET_ERROR)
	{
		printf("bind() generated error %d\n", WSAGetLastError());
		WSACleanup();
		exit(EXIT_FAILURE);
	}

	// Set up address for local DNS server
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;

	properties = p;
	properties->totalTime = chrono::high_resolution_clock::now();

	// Create Stats thread
	//cout << "DEBUG: About to create stats thread\n";
	statsHandle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)StatsManager::PrintStats, properties, 0, NULL);
	//cout << "DEBUG: Created stats thread\n";

	if (statsHandle == NULL)
	{
		printf("Could not create stats thread! exiting...\n");
		exit(EXIT_FAILURE);
	}
}

/* Basic destructor for SenderSocket cleans up socket and WinSock. */
SenderSocket::~SenderSocket()
{
	//cout << "DEBUG: Calling SenderSocket destructor\n";
	// Signal stats thread to quit
	if (properties->eventQuit != NULL)
		SetEvent(properties->eventQuit);

	//cout << "DEBUG: Waiting for stats handle\n";
	WaitForSingleObject(statsHandle, INFINITE);
	CloseHandle(statsHandle);

	//cout << "DEBUG: Stats handle closed, cleaning up\n";
	closesocket(sock);
	WSACleanup();
}

/* GetServerInfo does a forward lookup on the destination host string if necessary
 * and populates it's internal server information with the result. Called in Open().
 * Returns code 0 to indicate success or 3 if the target hostname does not have an 
 * entry in DNS. */
WORD SenderSocket::GetServerInfo(CONST CHAR* destination, WORD port)
{
	STRUCT hostent* remote;
	DWORD destinationIP = inet_addr(destination);

	// host is a valid IP, do not do a DNS lookup
	if (destinationIP != INADDR_NONE)
		server.sin_addr.S_un.S_addr = destinationIP;
	else
	{
		if ((remote = gethostbyname(destination)) == NULL)
		{
			// failure in gethostbyname
			chrono::time_point<chrono::high_resolution_clock> stopTime = chrono::high_resolution_clock::now();
			printf("[%2.3f] --> ", chrono::duration_cast<chrono::milliseconds>(stopTime - properties->totalTime).count() / 1000.0);
			printf("target %s is invalid\n", destination);
			return INVALID_NAME;
		}
		// take the first IP address and copy into sin_addr
		else
		{
			destinationIP = *(u_long*)remote->h_addr;
			memcpy((char*) & (server.sin_addr), remote->h_addr, remote->h_length);
		}
	}
	server.sin_port = htons(port);
	serverAddr.s_addr = destinationIP;

	return STATUS_OK;
}

/* Open() calls GetServerInfo() to populate internal server information and then creates a handshake
 * packet and attempts to send it to the corresponding server. If un-acknowledged, it will retransmit
 * this packet up to MAX_SYN_ATTEMPS times (by default 3). Open() will set the retransmission
 * timeout for future communication with the server to a constant scale of the handshake RTT.
 * Returns 0 to indicate success or a positive number to indicate failure. */
WORD SenderSocket::Open(CONST CHAR* destination, WORD port, DWORD senderWindow, STRUCT LinkProperties* lp)
{
	INT result = -1;
	properties->windowSize = senderWindow;
	RTO = max(1000, 2 * (lp->RTT * 1000));
	
	if (connected)
		return ALREADY_CONNECTED;

	if ((result = GetServerInfo(destination, port)) != STATUS_OK)
		return result;

	// create handshake packet
	SenderSynHeader handshake;
	handshake.sdh.flags.SYN = 1;
	handshake.sdh.seq = properties->senderBase;
	handshake.lp = *lp;
	handshake.lp.bufferSize = senderWindow + MAX_DATA_ATTEMPTS;

	// attempt to send SYN and receive ACK MAX_SYN_ATTEMPTS times
	CHAR* buf = new CHAR[MAX_PKT_SIZE];

	chrono::time_point<chrono::high_resolution_clock> startTime, stopTime;

	for (USHORT i = 1; i <= MAX_SYN_ATTEMPTS; i++)
	{
		// ************ SEND MESSAGE ************ //
		stopTime = chrono::high_resolution_clock::now();
		//printf("[%2.3f] --> ", chrono::duration_cast<chrono::milliseconds>(stopTime - properties->totalTime).count() / 1000.0);
		//printf("DEBUG-SYN: Seq. %d (attempt %d of %d, RTO % .3f) to %s\n", handshake.sdh.seq, i, MAX_SYN_ATTEMPTS, (RTO / 1000.0), inet_ntoa(serverAddr));
		
		// attempt to send the SYN packet to server
		result = sendto(sock, (CHAR*)&handshake, sizeof(handshake), NULL, (STRUCT sockaddr*) & server, sizeof(server));
		if (result == SOCKET_ERROR)
		{
			stopTime = chrono::high_resolution_clock::now();
			printf("[%2.3f] --> ", chrono::duration_cast<chrono::milliseconds>(stopTime - properties->totalTime).count() / 1000.0);
			printf("failed sendto with %d\n", WSAGetLastError());
			delete[] buf;
			return FAILED_SEND;
		}

		startTime = chrono::high_resolution_clock::now();

		// ********** RECEIVE RESPONSE ********** //
		if ((result = ReceiveACK(buf, properties->senderBase, TRUE)) != TIMEOUT)
		{
			stopTime = chrono::high_resolution_clock::now();
			if (result == STATUS_OK)
			{
				ReceiverHeader responseHeader = *(ReceiverHeader*)buf;
				//printf("[%2.3f] <-- ", chrono::duration_cast<chrono::milliseconds>(stopTime - properties->totalTime).count() / 1000.0);
				
				EnterCriticalSection(&properties->criticalSection);
				properties->estRTT = (INT) chrono::duration_cast<chrono::milliseconds>(stopTime - startTime).count();
				//printf("DEBUG-SYN: Got packet, estimated RTT %d\n", properties->estRTT);
				properties->devRTT = abs((INT)chrono::duration_cast<chrono::milliseconds>(stopTime - startTime).count() - properties->estRTT);
				//printf("DEBUG-SYN: estimated deviation %d\n", properties->devRTT);
				LeaveCriticalSection(&properties->criticalSection);
				RTO = properties->estRTT + 4 * max(properties->devRTT, 10);
				//printf("DEBUG-SYN: Setting RTO to %d ms\n", RTO);

				connected = true;
				delete[] buf;
				return STATUS_OK;
			}
			else
			{
				delete[] buf;
				return result;
			}
		}
	}

	// all attempts timed out, return
	delete[] buf;
	return TIMEOUT;
}

/* Attempts to send a single packet to the connected server. This is the externally facing 
 * Send() function and therefore requires a previously successful call to Open(). 
 * Returns 0 to indicate success or a positive number for failure. */
WORD SenderSocket::Send(CONST CHAR* message, INT messageSize)
{
	INT result = -1;
	// send single packet to server with connectivity check
	if (!connected)
		return NOT_CONNECTED;

	SenderDataHeader packet;

	// Find next available sequence number
	packet.seq = properties->senderBase;

	char* newMessage = new char[sizeof(packet) + messageSize];
	char* response = new char[MAX_PKT_SIZE];
	
	memcpy(newMessage, &packet, sizeof(packet));
	memcpy(newMessage + sizeof(packet), message, messageSize);

	chrono::time_point<chrono::high_resolution_clock> startTime, stopTime;

	// Attempt to communicate with server N times
	for (USHORT i = 1; i <= MAX_DATA_ATTEMPTS; i++)
	{
		// ************ SEND MESSAGE ************ //
		result = sendto(sock, newMessage, sizeof(packet) + messageSize, NULL, (STRUCT sockaddr*)& server, sizeof(server));
		if (result == SOCKET_ERROR)
		{
			stopTime = chrono::high_resolution_clock::now();
			printf("[%2.3f] --> ", chrono::duration_cast<chrono::milliseconds>(stopTime - properties->totalTime).count() / 1000.0);
			printf("failed sendto with %d\n", WSAGetLastError());
			return FAILED_SEND;
		}

		//printf("DEBUG-SEND: SN %d (attempt %d of %d, RTO % .3f)\n", ((SenderDataHeader*)newMessage)->seq, i, MAX_DATA_ATTEMPTS, (RTO / 1000.0));

		startTime = chrono::high_resolution_clock::now();

		// ********** RECEIVE RESPONSE ********** //
		result = ReceiveACK(response, ((SenderDataHeader*)newMessage)->seq, FALSE);
		if (result != TIMEOUT && result != FAST_RTX)
		{
			stopTime = chrono::high_resolution_clock::now();

			if (result == STATUS_OK)
			{
				ReceiverHeader responseHeader = *(ReceiverHeader*)response;
				InterlockedAdd((volatile LONG*)&properties->bytesAcked, messageSize);
				InterlockedAdd((volatile LONG*)&properties->goodput, messageSize);
				InterlockedIncrement((volatile LONG*)&properties->senderBase);

				if (i == 1)
				{
					EnterCriticalSection(&properties->criticalSection);
					properties->estRTT = .875 * properties->estRTT + .125 * (INT)chrono::duration_cast<chrono::milliseconds>(stopTime - startTime).count();
					properties->devRTT = .75 * properties->devRTT + .25 * abs((INT)chrono::duration_cast<chrono::milliseconds>(stopTime - startTime).count() - properties->estRTT);
					LeaveCriticalSection(&properties->criticalSection);
					RTO = properties->estRTT + 4 * max(properties->devRTT, 10);
				//printf("DEBUG-SEND: Setting RTO to %d ms (est. RTT %.3fs est. DEV %.3fs)\n", RTO, properties->estRTT / 1000.0, properties->devRTT / 1000.0);
				}

				return STATUS_OK;
			}
			else
				return result;
		}
	}

	// all attempts timed out, return
	delete[] newMessage;
	delete[] response;
	return TIMEOUT;
}

/* Attempts to receive a single acknowledgement packet from the connected server. 
 * Uses the current RTO and store the acknowledgement the 'response' buffer.
 * It is assumed that this buffer has already been allocated and is capacity 
 * of at least MAX_PKT_SIZE bytes. Returns 0 to indicate success or a positive 
 * number for failure. */
WORD SenderSocket::ReceiveACK(CHAR* response, DWORD packetNumber, BOOL setupMessage)
{
	INT result = -1;
	chrono::time_point<chrono::high_resolution_clock> startTime, stopTime;
	startTime = chrono::high_resolution_clock::now();
	stopTime = startTime;

	fd_set fd;
	FD_ZERO(&fd);

	SHORT numDuplicateACKS = 0;

	// create address struct for responder
	STRUCT sockaddr_in response_addr;
	INT response_size = sizeof(response_addr);

	while (true)
	{
		LONG timeLeft = RTO * 1000 - chrono::duration_cast<chrono::microseconds>(stopTime - startTime).count();
		// add margin of error because select operates +- 1 ms from the actual timeout value 
		if (timeLeft < 1000)
			break;

		//printf("RCV-DEBUG: Attempting to receive ACK for packet %d, %.2fs left before retransmission\n", packetNumber, (timeLeft / 1000000.0));

		// set timeout
		STRUCT timeval timeout;
		timeout.tv_sec = 0;
		timeout.tv_usec = timeLeft;

		FD_SET(sock, &fd);

		result = select(0, &fd, NULL, NULL, &timeout);
		if (result > 0)
		{
			// attempt to get response from server
			result = recvfrom(sock, response, MAX_PKT_SIZE, NULL, (STRUCT sockaddr*) & response_addr, &response_size);
			if (result == SOCKET_ERROR)
			{
				stopTime = chrono::high_resolution_clock::now();
				printf("[%2.3f] <-- ", chrono::duration_cast<chrono::milliseconds>(stopTime - properties->totalTime).count() / 1000.0);
				printf("failed recvfrom with %d\n", WSAGetLastError());
				return FAILED_RECV;
			}

			ReceiverHeader responseHeader = *(ReceiverHeader*)response;
			if (responseHeader.ackSeq == packetNumber && setupMessage)
			{
				//printf("RCV-DEBUG: Received expected setup packet %d\n", responseHeader.ackSeq);
				return STATUS_OK;
			}
			else if (responseHeader.ackSeq > packetNumber && !setupMessage)
			{
				//printf("RCV-DEBUG: Received expected data packet %d\n", responseHeader.ackSeq);
				//Move up window and signal producer (sender)
				return STATUS_OK;
			}
			else if (responseHeader.ackSeq == properties->senderBase && !setupMessage)
			{
				//printf("RCV-DEBUG: Received unexpected packet %d\n", responseHeader.ackSeq);
				numDuplicateACKS++;
				if (numDuplicateACKS == FAST_RTX_NUM)
				{
					//printf("RCV-DEBUG: Fast retransmit signalled\n");
					InterlockedIncrement((volatile LONG*)&properties->fastRetxPackets);
					return FAST_RTX; // Allow sender to retransmit (premature timeout)
				}
			}
		}
		else if (result != 0)
		{
			stopTime = chrono::high_resolution_clock::now();
			printf("[%2.3f] <-- ", chrono::duration_cast<chrono::milliseconds>(stopTime - properties->totalTime).count() / 1000.0);
			printf("failed select with %d\n", WSAGetLastError());
			return FAILED_RECV;
		}

		stopTime = chrono::high_resolution_clock::now();
	}

	//cout << "RCV-DEBUG: Timeout\n";
	InterlockedIncrement((volatile LONG*)&properties->timeoutPackets);
	return TIMEOUT;
}

/* Closes connection to the current server. Sends a connection termination packet and waits 
 * for an acknowledgement using the RTO calculated in the call to Open(). Returns 0 to 
 * indicate success or a positive number for failure.*/
WORD SenderSocket::Close(DOUBLE &elapsedTime)
{
	INT result = -1;

	if (!connected)
		return NOT_CONNECTED;

	// create connection termination packet
	SenderDataHeader termination;
	termination.flags.FIN = 1;
	termination.seq = properties->senderBase;

	CHAR* buf = new CHAR[MAX_PKT_SIZE];

	chrono::time_point<chrono::high_resolution_clock> stopTime;

	// Wait until Receive signals no more pending data packets

	for (USHORT i = 1; i <= MAX_DATA_ATTEMPTS; i++)
	{
		// ************ SEND MESSAGE ************ //
		//printf("FIN-DEBUG: SN %d (attempt %d of %d, RTO % .3f)\n", termination.seq, i, MAX_DATA_ATTEMPTS, (RTO / 1000.0));

		// attempt to send the SYN packet to server
		result = sendto(sock, (CHAR*)&termination, sizeof(termination), NULL, (STRUCT sockaddr*) & server, sizeof(server));
		if (result == SOCKET_ERROR)
		{
			stopTime = chrono::high_resolution_clock::now();
			printf("[%2.3f] --> ", chrono::duration_cast<chrono::milliseconds>(stopTime - properties->totalTime).count() / 1000.0);
			printf("failed sendto with %d\n", WSAGetLastError());
			delete[] buf;
			return FAILED_SEND;
		}

		// ********** RECEIVE RESPONSE ********** //
		if ((result = ReceiveACK(buf, properties->senderBase, TRUE)) != TIMEOUT)
		{
			if (result == STATUS_OK)
			{
				ReceiverHeader responseHeader = *(ReceiverHeader*)buf;
				stopTime = chrono::high_resolution_clock::now();
				elapsedTime = chrono::duration_cast<chrono::milliseconds>(stopTime - properties->totalTime).count() / 1000.0;
				printf("[%2.3f] <-- ", elapsedTime);
				printf("FIN-ACK %d window 0x%X\n", ((ReceiverHeader*)buf)->ackSeq, ((ReceiverHeader*)buf)->recvWnd);
				connected = false;
				delete[] buf;
				return STATUS_OK;
			}
			else
			{
				delete[] buf;
				return result;
			}
		}
	}

	// all attempts timed out, return
	delete[] buf;
	return TIMEOUT;
}