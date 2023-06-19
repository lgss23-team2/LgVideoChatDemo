//#include "definition.h"
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#include <ws2tcpip.h>
#include < cstdlib >
#include <iostream>

#include "AccessControlClient.h"
#include "LgVideoChatDemo.h"
#include "TcpSendRecv.h"
#include "definition.h"

static HANDLE hClientEvent = INVALID_HANDLE_VALUE;
static HANDLE hEndACClientEvent = INVALID_HANDLE_VALUE;
static HANDLE hThreadACClient = INVALID_HANDLE_VALUE;
static HANDLE hTimer = INVALID_HANDLE_VALUE;

static SOCKET Client = INVALID_SOCKET;
static DWORD ThreadACClientID;

static DWORD WINAPI ThreadACClient(LPVOID ivalue);
static int RecvHandler(SOCKET __InputSock, char* data, int datasize, sockaddr_in sockip, int socklen);

char MyEmail[GENERAL_BUFSIZE]{};

extern bool IsLogin;
extern char ContactList[MAX_DEVSIZE][GENERAL_BUFSIZE];

static void AccessControlClientSetExitEvent(void)
{
	if (hEndACClientEvent != INVALID_HANDLE_VALUE)
		SetEvent(hEndACClientEvent);
}

static void CleanUpAccessControlClient(void)
{
    std::cout << "Cleanup Access Control Client" << std::endl;

    if (hClientEvent != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hClientEvent);
        hClientEvent = INVALID_HANDLE_VALUE;
    }
    if (hEndACClientEvent != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hEndACClientEvent);
        hEndACClientEvent = INVALID_HANDLE_VALUE;
    }

    if (Client != INVALID_SOCKET)
    {
        closesocket(Client);
        Client = INVALID_SOCKET;
    }
}

bool StartAccessControlClient(void)
{
    hThreadACClient = CreateThread(NULL, 0, ThreadACClient, NULL, 0, &ThreadACClientID);
    return true;
}

bool StopAccessControlClient(void)
{
    AccessControlClientSetExitEvent();
    if (hThreadACClient != INVALID_HANDLE_VALUE)
    {
        WaitForSingleObject(hThreadACClient, INFINITE);
        CloseHandle(hThreadACClient);
        hThreadACClient = INVALID_HANDLE_VALUE;
    }
    ;
    return true;
}
bool IsACClientRunning(void)
{
    if (hThreadACClient == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    else return true;
}

bool ConnectToACSever(const char* remotehostname, unsigned short remoteport)
{
    int iResult;
    struct addrinfo   hints;
    struct addrinfo* result = NULL;
    char remoteportno[128];

    sprintf_s(remoteportno, sizeof(remoteportno), "%d", remoteport);

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    iResult = getaddrinfo(remotehostname, remoteportno, &hints, &result);
    if (iResult != 0)
    {
        std::cout << "getaddrinfo: Failed" << std::endl;
        return false;
    }
    if (result == NULL)
    {
        std::cout << "getaddrinfo: Failed" << std::endl;
        return false;
    }

    if ((Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET)
    {
        freeaddrinfo(result);
        std::cout << "video client socket() failed with error " << WSAGetLastError() << std::endl;
        return false;
    }

    //----------------------
    // Connect to server.
    iResult = connect(Client, result->ai_addr, (int)result->ai_addrlen);
    freeaddrinfo(result);
    if (iResult == SOCKET_ERROR) {
        std::cout << "connect function failed with error : " << WSAGetLastError() << std::endl;
        iResult = closesocket(Client);
        Client = INVALID_SOCKET;
        if (iResult == SOCKET_ERROR)
            std::cout << "closesocket function failed with error :" << WSAGetLastError() << std::endl;
        return false;
    }
    return true;

}


static DWORD WINAPI ThreadACClient(LPVOID ivalue)
{
    HANDLE ghEvents[3];
    int NumEvents;
    int iResult;
    DWORD dwEvent;
    LARGE_INTEGER liDueTime;

    char* InputBuffer = NULL;
    char* InputBufferWithOffset = NULL;
    unsigned int CurrentInputBufferSize = 1024 * 10;
    unsigned int InputBytesNeeded = 1024 * 10;

    InputBuffer = (char*)std::realloc(InputBuffer, CurrentInputBufferSize);
    InputBufferWithOffset = InputBuffer;

    if (InputBuffer == NULL)
    {
        std::cout << "InputBuffer Realloc failed" << std::endl;
        return 1;
    }

    liDueTime.QuadPart = 0LL;

    hTimer = CreateWaitableTimer(NULL, FALSE, NULL);

    if (NULL == hTimer)
    {
        std::cout << "CreateWaitableTimer failed " << GetLastError() << std::endl;
        return 2;
    }

    if (!SetWaitableTimer(hTimer, &liDueTime, ACS_DELAY, NULL, NULL, 0))
    {
        std::cout << "SetWaitableTimer failed  " << GetLastError() << std::endl;
        return 3;
    }

    hClientEvent = WSACreateEvent();
    hEndACClientEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (WSAEventSelect(Client, hClientEvent, FD_READ | FD_CLOSE) == SOCKET_ERROR)
    {
        std::cout << "WSAEventSelect() failed with error " << WSAGetLastError() << std::endl;
        iResult = closesocket(Client);
        Client = INVALID_SOCKET;
        if (iResult == SOCKET_ERROR)
            std::cout << "closesocket function failed with error : " << WSAGetLastError() << std::endl;
        return 4;
    }
    ghEvents[0] = hEndACClientEvent;
    ghEvents[1] = hClientEvent;
    ghEvents[2] = hTimer;
    
    NumEvents = 2;

    while (1) {
        dwEvent = WaitForMultipleObjects(
            NumEvents,        // number of objects in array
            ghEvents,       // array of objects
            FALSE,           // wait for any object
            INFINITE);  // INFINITE) wait

        if (dwEvent == WAIT_OBJECT_0) 
            break;
        else if (dwEvent == WAIT_OBJECT_0 + 1)
        {
            WSANETWORKEVENTS NetworkEvents;
            if (SOCKET_ERROR == WSAEnumNetworkEvents(Client, hClientEvent, &NetworkEvents))
            {
                std::cout << "WSAEnumNetworkEvent: " << WSAGetLastError() << "dwEvent " << dwEvent << " lNetworkEvent " << std::hex << NetworkEvents.lNetworkEvents << std::endl;
                NetworkEvents.lNetworkEvents = 0;
            }
            else
            {
                if (NetworkEvents.lNetworkEvents & FD_READ)
                {
                    if (NetworkEvents.iErrorCode[FD_READ_BIT] != 0)
                    {
                        std::cout << "FD_READ failed with error " << NetworkEvents.iErrorCode[FD_READ_BIT] << std::endl;
                    }
                    else
                    {
                        int iResult;
                        sockaddr_in safrom;
                        int socklen = sizeof(sockaddr_in);

                        iResult = recvfrom(Client, (char*)InputBufferWithOffset, InputBytesNeeded, 0, (sockaddr*)&safrom, &socklen);
                        if (iResult != SOCKET_ERROR)
                        {
                            //std::cout << "AC client recevied : " << InputBufferWithOffset << std::endl;
                            RecvHandler(Client, InputBufferWithOffset, iResult, safrom, socklen);
                        }
                        else 
                            std::cout << "ReadDataTcpNoBlock buff failed " << WSAGetLastError() << std::endl;

                    }
                }
                if (NetworkEvents.lNetworkEvents & FD_WRITE)
                {
                    if (NetworkEvents.iErrorCode[FD_WRITE_BIT] != 0)
                    {
                        std::cout << "FD_WRITE failed with error " << NetworkEvents.iErrorCode[FD_WRITE_BIT] << std::endl;
                    }
                    else
                    {
                        std::cout << "FD_WRITE" << std::endl;
                    }
                }

                if (NetworkEvents.lNetworkEvents & FD_CLOSE)
                {
                    if (NetworkEvents.iErrorCode[FD_CLOSE_BIT] != 0)

                    {
                        std::cout << "FD_CLOSE failed with error " << NetworkEvents.iErrorCode[FD_CLOSE_BIT] << std::endl;
                    }
                    else
                    {
                        std::cout << "FD_CLOSE" << std::endl;
                        PostMessage(hWndMain, WM_CLIENT_LOST, 0, 0);
                        break;
                    }
                }
            }
        }
        else if (dwEvent == WAIT_OBJECT_0 + 2)
        {
            char testText[1000];
            memcpy(testText, "Test timer from client", 22);
            if(sendMsgtoACS((char*)testText, sizeof(testText)))
            { 
                std::cout << "send Test Text to AC Server" << std::endl;
            }
            else
            {
                std::cout << "WriteDataTcp Failed " << WSAGetLastError() << std::endl;
                PostMessage(hWndMain, WM_CLIENT_LOST, 0, 0);
                break;
            }
        }

    }
    if (InputBuffer)
    {
        std::free(InputBuffer);
        InputBuffer = nullptr;
    }
    CleanUpAccessControlClient();
    std::cout << "Access Control Client Exiting" << std::endl;
    return 0;
}

int sendMsgtoACS(char* data, int len)
{
    return send(Client, data, len, 0);
}


//static int OnConnectACS(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
int OnConnectACS(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static char IpAdressACS[512] = ACS_IP;

    if (!IsACClientRunning())
    {
        if (ConnectToACSever(IpAdressACS, ACS_PORT))
        {
            std::cout << "Connected to Server" << std::endl;
            StartAccessControlClient();
            std::cout << "Client Started.." << std::endl;

            return 1;
        }
        else
        {
            std::cout << "Connection AC server Failed!" << std::endl;
            MessageBox(hWnd, TEXT("Connection AC server Failed!"), TEXT("ERROR"), MB_OK | MB_ICONEXCLAMATION);
            return 0;
        }

    }
    else
    {
        std::cout << "AC client running" << std::endl;
        return 0;
    }

    return 0;
}

//static int OnDisconnectACS(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
int OnDisconnectACS(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (IsACClientRunning())
    {
        StopAccessControlClient();
        std::cout << "AC Client Stopped" << std::endl;
    }
    return 1;
}


static int RecvHandler(SOCKET __InputSock, char* data, int datasize, sockaddr_in sockip, int socklen)
{
    oCommandOnly* getMsg = (oCommandOnly*)data;

    switch (getMsg->MessageType)
    {
    case RegistrationResponse:
    {
        // Registration
        std::cout << "registed User Information " << std::endl;

        break;
    }
    case LoginResponse:
    {
        std::cout << "login rsp " << std::endl;
        TRspWithMessage2 *rsp = (TRspWithMessage2*)data;
        printf("%s", rsp->Message1);
        if (!strncmp((const char*)rsp->Message1, "Login Success", strlen("Login Success")))
        {
            IsLogin = true;
            //get contact list
            TReqwithMessage req{};
            req.MessageType = RequestContactList;
            sendMsgtoACS((char*)&req, sizeof(req));

            memcpy(MyEmail, rsp->Message2, rsp->MessageLen2);

/*
            //get missd call
            req.MessageType = MissedCall;

            req.MessageLen = strlen((const char*)rsp->Message2);
            memcpy(req.Message, rsp->Message2, req.MessageLen);

            sendMsgtoACS((char*)&req, sizeof(req));
            */

            MessageBox(NULL, L"Login Success", L"", MB_OK);
            break;
        }
        MessageBox(NULL, L"Login Failed", L"", MB_OK | MB_ICONERROR);
        

        break;
    }
    case LogoutResponse:
    {
        break;
    }
    case RequestStatus:
    {
        break;
    }
    case SendStatus:
    {
        break;
    }

    case RequestContactList:
    {
        // Load stored data

        // send contact list
        // TContactList *clist = (TContactList *)std::malloc(sizeof(TContactList));
        // clist->MessageType = SendContactList
        // for i in range(Sizeof stored clist)
        //		(clist->DevID).append(stored_clist[i])
        // sendto(Accepter, (char *)clist, sizeof(TContactList), 0, 0, <<sockaddr>>, <<sockaddr_len>>);

        break;
    }
    case SendContactList:
    {
        TContactList* req = (TContactList*)data;

        for (int i = 0; i < MAX_DEVSIZE; i++) {
            printf("%s\n", req->ListBuf[i]);
            if (strlen((const char*)req->ListBuf[i]) > 0)
                memcpy(ContactList[i], req->ListBuf[i], strlen((const char*)req->ListBuf[i]));
        }

        break;
    }
    case RequestCall:
    {
        TDeviceID* tmp = (TDeviceID*)data;
        // char* dev_id = tmp->DevID;
        // std::cout << dev_id << std::endl;
        // Load stored IP_addres of Receiver 
        // sendto(Accepter, (char*)tmp, sizeof(TDeviceID), 0, 0, << sockaddr_forward >> , << sockaddr_len >> );
        break;
    }
    case AcceptCall:
    {
        TDeviceID* tmp = (TDeviceID*)data;
        // char* dev_id = tmp->DevID;
        // std::cout << dev_id << std::endl;
        break;
    }
    case RejectCall:
    {
        TDeviceID* tmp = (TDeviceID*)data;
        // char* dev_id = tmp->DevID;
        // std::cout << dev_id << std::endl;
        break;
    }
    case MissedCall:
    {
        TMissedCall* req = (TMissedCall*)data;

        // need to discuss missed call structure
        for (int i = 0; i < MAX_DEVSIZE; i++) {
            printf("%s\n", req->ListBuf[i]);
        }

        break;
    }

    default:
        break;
    }
    return 0;
}

