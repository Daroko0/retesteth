#include "Socket.h"
#include <string>
#include <chrono>
#include <thread>
#include <iostream>
#include <retesteth/EthChecks.h>
#include <curl/curl.h>


using namespace std;

Socket::Socket(SocketType _type, string const& _path): m_path(_path), m_socketType(_type)
{
#if defined(_WIN32)
    m_socket = CreateFile(
        m_path.c_str(),   // pipe name
        GENERIC_READ |  // read and write access
        GENERIC_WRITE,
        0,              // no sharing
        NULL,           // default security attribute
        OPEN_EXISTING,  // opens existing pipe
        0,              // default attributes
        NULL);          // no template file

    if (m_socket == INVALID_HANDLE_VALUE)
        ETH_FAIL_MESSAGE("Error creating IPC socket object!");

#else
    if (_type == SocketType::IPC)
    {
        if (_path.length() >= sizeof(sockaddr_un::sun_path))
            ETH_FAIL_MESSAGE("Error opening IPC: socket path is too long!");

        struct sockaddr_un saun;
        memset(&saun, 0, sizeof(sockaddr_un));
        saun.sun_family = AF_UNIX;
        strcpy(saun.sun_path, _path.c_str());

        // http://idletechnology.blogspot.ca/2011/12/unix-domain-sockets-on-osx.html
        //
        // SUN_LEN() might be optimal, but it seemingly affects the portability,
        // with at least Android missing this macro.  Just using the sizeof() for
        // structure seemingly works, and would only have the side-effect of
        // sending larger-than-required packets over the socket.  Given that this
        // code is only used for unit-tests, that approach seems simpler.
        #if defined(__APPLE__)
            saun.sun_len = sizeof(struct sockaddr_un);
        #endif //  defined(__APPLE__)

        if ((m_socket = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
            ETH_FAIL_MESSAGE("Error creating IPC socket object");

        if (connect(m_socket, reinterpret_cast<struct sockaddr const*>(&saun), sizeof(struct sockaddr_un)) < 0)
        {
            close(m_socket);
            ETH_FAIL_MESSAGE("Error connecting to IPC socket: " + _path);
        }
    } else if (_type == SocketType::TCP)
    {

        struct sockaddr_in sin;
        sin.sin_family = AF_INET;

        size_t pos = _path.find_last_of(':');
        string address = _path.substr(0, pos);
        int port = atoi(_path.substr(pos + 1).c_str());

        sin.sin_addr.s_addr = inet_addr(address.c_str());
        sin.sin_port = htons(port);

        if ((m_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
            ETH_FAIL_MESSAGE("Error creating TCP socket object");

        if (connect(m_socket, reinterpret_cast<struct sockaddr const*>(&sin), sizeof(struct sockaddr_in)) < 0)
        {
            close(m_socket);
            ETH_FAIL_MESSAGE("Error connecting to TCP socket: " + _path);
        }
    }
#endif
}

namespace
{
    std::size_t writecallback(
            const char* in,
            std::size_t size,
            std::size_t num,
            std::string* out)
    {
        const std::size_t totalBytes(size * num);
        out->append(in, totalBytes);
        return totalBytes;
    }

    #if defined(_WIN32)
    string sendRequestWin(string const& _req)
    {
        // Write to the pipe.
        DWORD cbWritten;
        BOOL fSuccess = WriteFile(
            m_socket,               // pipe handle
            _req.c_str(),           // message
            _req.size(),            // message length
            &cbWritten,             // bytes written
            NULL);                  // not overlapped

        if (!fSuccess || (_req.size() != cbWritten))
            ETH_FAIL_MESSAGE("WriteFile to pipe failed");

        // Read from the pipe.
        DWORD cbRead;
        fSuccess = ReadFile(
            m_socket,          // pipe handle
            m_readBuf,         // buffer to receive reply
            sizeof(m_readBuf), // size of buffer
            &cbRead,           // number of bytes read
            NULL);             // not overlapped

        if (!fSuccess)
            ETH_FAIL_MESSAGE("ReadFile from pipe failed");

        return string(m_readBuf, m_readBuf + cbRead);
    }
    #endif

    string sendRequestTCP(string const& _req, string const& _address)
    {
        CURL *curl;
        CURLcode res;
        curl = curl_easy_init();

        string url = _address;
        if (_address.find("http") == string::npos)
            url = "http://" + _address;
        if (curl)
        {
            std::unique_ptr<std::string> httpData(new std::string());
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writecallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, httpData.get());
            struct curl_slist *header = NULL;
            header = curl_slist_append(header, "Content-Type: application/json");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, _req.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, -1L);

            res = curl_easy_perform(curl);
            if(res != CURLE_OK)
                ETH_FAIL_MESSAGE("curl_easy_perform() failed " + string(curl_easy_strerror(res)));

            curl_easy_cleanup(curl);
            return *httpData.get();
        }
        else
            ETH_FAIL_MESSAGE("Error initializing Curl");
        return string();
    }
}

string Socket::sendRequestIPC(string const& _req, SocketResponseValidator& _validator)
{
    char buf;
    recv(m_socket, &buf, 1, MSG_PEEK | MSG_DONTWAIT);
    if (errno == ENOTCONN)
        ETH_FAIL_MESSAGE("Socket connection error! ");

    if (send(m_socket, _req.c_str(), _req.length(), 0) != (ssize_t)_req.length())
        ETH_FAIL_MESSAGE("Writing on socket failed.");

    auto start = chrono::steady_clock::now();
    ssize_t ret = 0;
    string reply;

    while (
        _validator.completeResponse() == false &&
        chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - start).count() <
            m_readTimeOutMS)
    {
        ret = recv(m_socket, m_readBuf, sizeof(m_readBuf), 0);

        // Also consider closed socket an error.
        if (ret < 0)
            ETH_FAIL_MESSAGE("Reading on socket failed!");

        _validator.acceptResponse(m_readBuf);
        memset(&m_readBuf[0], 0, sizeof(m_readBuf));
    }

    reply = _validator.getResponse();

    if (ret == 0)
        ETH_FAIL_MESSAGE("Timeout reading on socket.");

    return reply;
}

string Socket::sendRequest(string const& _req, SocketResponseValidator& _val)
{
    #if defined(_WIN32)
        return sendRequestWin(_req);
    #endif

    if (m_socketType == Socket::TCP)
        return sendRequestTCP(_req, m_path);

    if (m_socketType == Socket::IPC)
        return sendRequestIPC(_req, _val);

    return string();
}

JsonObjectValidator::JsonObjectValidator()
{
    m_status = false;
    m_bracersCount = 0;
}
void JsonObjectValidator::acceptResponse(std::string const& _response)
{
    m_response += _response;
    for (size_t i = 0; i < _response.size(); i++)
    {
        if (_response[i] == '{')
            m_bracersCount++;
        else if (_response[i] == '}')
            m_bracersCount--;
    }
    if (m_bracersCount == 0)
        m_status = true;
}

bool JsonObjectValidator::completeResponse() const
{
    return m_status;
}

std::string JsonObjectValidator::getResponse() const
{
    return m_response;
}
