#include <retesteth/ExitHandler.h>
#include <retesteth/TestOutputHelper.h>
#include <mutex>
#include <thread>
using namespace test;

std::mutex g_executionMutex;
bool ExitHandler::m_receivedExitSig = false;

bool ExitHandler::receivedExitSignal()
{
    std::lock_guard<std::mutex> lock(g_executionMutex);
    return m_receivedExitSig;
}

std::mutex g_runOnce;
void ExitHandler::doExit()
{
    std::lock_guard<std::mutex> lock(g_runOnce);
    static bool runOnce = false;
    if (!runOnce)
    {
        // must be run after all TestOutputHelper::finishTest methods are called;
        while (!TestOutputHelper::isAllTestsFinished())
        {
            static int totaltime = 0;
            totaltime++;
            if (totaltime > 50)
            {
                ETH_STDERROR_MESSAGE("Waiting for TestOutputHelper::finishTest()");
                // attempt to close unfinished tests manually ???
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
        RPCSession::clear();
        test::TestOutputHelper::printTestExecStats();
        runOnce = true;
    }
}

void ExitHandler::exitHandler(int)
{
    std::lock_guard<std::mutex> lock(g_executionMutex);
    m_receivedExitSig = true;
    std::cerr << "Finishing retesteth run" << std::endl;
}


