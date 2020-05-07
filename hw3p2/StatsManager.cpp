// StatsManager.cpp
// CSCE 463-500
// Luke Grammer
// 11/12/19

#include "pch.h"

using namespace std;

// print running statistics for webcrawling worker threads at a fixed 2s interval
void StatsManager::PrintStats(LPVOID properties)
{
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

	Properties* p = (Properties*)properties;
	
	std::chrono::time_point<std::chrono::high_resolution_clock> segmentStartTime = std::chrono::high_resolution_clock::now();
	std::chrono::time_point<std::chrono::high_resolution_clock> stopTime;

	// until shutdown request has been sent by main
	while (WaitForSingleObject(p->eventQuit, STATS_INTERVAL * 1000) == WAIT_TIMEOUT)
	{
		// enter critical section
		stopTime = std::chrono::high_resolution_clock::now();

		// time since last print
		size_t segmentTime = std::chrono::duration_cast<std::chrono::milliseconds>(stopTime - segmentStartTime).count();

		// print statistics
		std::printf("[%2d] B %6d (%5.1f MB) N %6d T %d F %d W %d S %0.3f Mbps RTT %5.3f\n",
			(int) std::chrono::duration_cast<std::chrono::seconds>(stopTime - p->totalTime).count(), 
			p->senderBase, p->bytesAcked / 1000000.0, (DWORD) (p->senderBase + 1), p->timeoutPackets, 
			p->fastRetxPackets, p->windowSize, ((UINT64) p->goodput * 8) / (1000.0 * segmentTime), 
			p->estRTT / 1000.0);

		// reset goodput
		EnterCriticalSection(&p->criticalSection);
		p->goodput = 0;
		LeaveCriticalSection(&p->criticalSection);

		segmentStartTime = std::chrono::high_resolution_clock::now();
	}

}