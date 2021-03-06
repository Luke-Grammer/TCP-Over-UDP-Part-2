// Driver.cpp
// CSCE 463-500
// Luke Grammer
// 11/12/19

#include "pch.h"

#pragma comment(lib, "ws2_32.lib")

using namespace std;

int main(INT argc, CHAR** argv)
{
	// debug flag to check for memory leaks
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF); 

	// ************* VALIDATE ARGUMENTS ************** //

	if (argc != 8)
	{
		(argc < 8) ? printf("error: too few arguments\n\n") : printf("error: too many arguments\n\n");
		printf("usage: hw3p1.exe <DSN> <PBS> <SWS> <RTT> <LPF> <LPR> <BLS>\n");
		printf("DSN - Destination server IP or hostname\n");
		printf("PBS - Power of two size for transmission buffer (bytes)\n");
		printf("SWS - Sender window size (packets)\n");
		printf("RTT - Simulated RTT propogation delay (seconds)\n");
		printf("LPF - Simulated loss probability in forward direction\n");
		printf("LPR - Simulated loss probability in reverse direction\n");
		printf("BLS - Bottleneck link speed (Mbps)\n");
		return INVALID_ARGUMENTS;
	}

	// ************ INITIALIZE VARIABLES ************* //
	
	CHAR* destination     = argv[1];
	DWORD dwordBufSize    = (DWORD) 1 << atoi(argv[2]); // Can't use UINT64 because allocation with new uses unsigned 32 bit int
	DWORD senderWindow    = atoi(argv[3]);
	FLOAT RTT             = (FLOAT) atof(argv[4]);
	FLOAT fLossProb       = (FLOAT) atof(argv[5]);
	FLOAT rLossProb       = (FLOAT) atof(argv[6]);
	DWORD bottleneckSpeed = atoi(argv[7]);

	chrono::time_point<chrono::high_resolution_clock> startTime, stopTime;

	STRUCT LinkProperties lp;
	lp.RTT = RTT;
	lp.speed = (FLOAT) bottleneckSpeed * 1000000;
	lp.pLoss[0] = fLossProb;
	lp.pLoss[1] = rLossProb;

	printf("Main:   sender W = %d, RTT = %.3f sec, loss %g / %g, link %d Mbps\n" , senderWindow, RTT, fLossProb, rLossProb, bottleneckSpeed);
	
	// ************* TIMED FILL OF BUFFER ************ //
	
	printf("Main:   initializing DWORD array with 2^%d elements... ", atoi(argv[2]));
	startTime = chrono::high_resolution_clock::now();

	DWORD* dwordBuf = new DWORD[dwordBufSize];
	for (DWORD i = 0; i < dwordBufSize; i++)
		dwordBuf[i] = i;

	stopTime = chrono::high_resolution_clock::now();
	printf("done in %lld ms\n",
		chrono::duration_cast<chrono::milliseconds>
		(stopTime - startTime).count());

	// ********** OPEN CONNECTION TO SERVER ********** //
	
	INT status = -1;
	Properties p;
	SenderSocket socket(&p);

	startTime = chrono::high_resolution_clock::now();
	if ((status = socket.Open(destination, MAGIC_PORT, senderWindow, &lp)) != STATUS_OK)
	{
		printf("Main:   open failed with status %d\n", status);
		delete[] dwordBuf;
		return status;
	}
	stopTime = chrono::high_resolution_clock::now();

	printf("Main:   connected to %s in %0.3f sec, pkt size %d bytes\n", destination, 
		chrono::duration_cast<chrono::milliseconds>
		(stopTime - startTime).count() / 1000.0, MAX_PKT_SIZE);
	
	startTime = chrono::high_resolution_clock::now();

	// ************* SEND DATA TO SERVER ************* //

	UINT64 charBufSize = (UINT64) dwordBufSize << 2;
	CHAR* charBuf = (CHAR*)dwordBuf;
	UINT64 offset = 0;
	UINT numPackets = 0;
	
	while (offset < charBufSize)
	{
		// decide the size of the next chunk
		UINT64 bytes = min(charBufSize - offset, (MAX_PKT_SIZE - sizeof(SenderDataHeader)));
		// send chunk into socket
		//cout << "DEBUG: Main sending " << bytes << " bytes\n";
		if ((status = socket.Send(charBuf + offset, bytes)) != STATUS_OK)
		{
			printf("Main:   send failed with status %d\n", status);
			delete[] dwordBuf;
			return status;
		}

		numPackets++;
		offset += bytes;
	}
	
	stopTime = chrono::high_resolution_clock::now();
	
	// ********** CLOSE CONNECTION TO SERVER ********* //
	
	DOUBLE elapsedTime = 0.0;
	if ((status = socket.Close(elapsedTime)) != STATUS_OK)
	{
		printf("Main:   close failed with status %d\n", status);
		delete[] dwordBuf;
		return status;
	}

	printf("Main:   transfer finished in %0.3f sec\n",
		chrono::duration_cast<chrono::milliseconds>
		(stopTime - startTime).count() / 1000.0);

	Checksum cs;
	DWORD check = cs.CRC32((UCHAR*) charBuf, charBufSize);
	printf("Main:   estRTT %0.3f, ideal rate %0.2f Kbps, checksum 0x%X\n", p.estRTT / 1000.0, ((UINT64) senderWindow * MAX_PKT_SIZE * 8) / (FLOAT) p.estRTT, check);

	delete[] dwordBuf;
	return 0;
}
