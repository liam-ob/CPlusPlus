
// To deploy this server, simply execute the executable in a folder with a `ident-allowed-ips.conf` file next to it.
// The file should look like
/*
# One IP per line
# Comments supported (server name goes here, e.g. loopback)
127.0.0.1
*/

//To Build this application please build it statically (on the architecture and OS that you wish to deploy it on (currently only suppports windows)):
/*
g++ -static -std=c++17 ident.cpp -o ident_server -lwsock32 -lws2_32 -lpsapi -liphlpapi
*/

// To use this, put this into a shell script and replace server_port and client_port with the desired port:
/*
# Create query string
query="$server_port,$client_port"

# Use telnet to query the ident server
(echo "$query"; sleep 1) | telnet "$dest_ip" 113
*/


// tasklist /v /fo list

// built by Liam OB

#include <iostream>
#include <fstream>
#include <string>
#include <set>
#include <sstream>
#include <cstring>
#include <chrono>
#include <iomanip>
#include <system_error>
#include <memory>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <iphlpapi.h> 
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")  // Add this for TCP table functions
typedef SOCKET socket_t;



class ProcessIdentifier {
public:
    struct ProcessInfo {
        std::string username;
        std::string processName;
        int pid;
        bool success;
        std::string errorMessage;

        ProcessInfo() : username("UNKNOWN"), processName("UNKNOWN"), pid(0), success(false), errorMessage("") {}
    };

    static ProcessInfo getProcessForPort(int port) {
        ProcessInfo info;
        
        try {
            // Create TCP table snapshot
            ULONG size = 0;
            DWORD result = GetExtendedTcpTable(nullptr, &size, TRUE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
            if (result != ERROR_INSUFFICIENT_BUFFER) {
                throw std::runtime_error("Failed to get TCP table size: " + std::to_string(result));
            }

            std::vector<char> buffer(size);
            PMIB_TCPTABLE_OWNER_PID tcpTable = reinterpret_cast<PMIB_TCPTABLE_OWNER_PID>(buffer.data());
            
            result = GetExtendedTcpTable(tcpTable, &size, TRUE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
            if (result != NO_ERROR) {
                throw std::runtime_error("Failed to get TCP table: " + std::to_string(result));
            }

            for (DWORD i = 0; i < tcpTable->dwNumEntries; i++) {
                if (ntohs(tcpTable->table[i].dwLocalPort) == port) {
                    info.pid = tcpTable->table[i].dwOwningPid;
                    
                    // Get process name using RAII pattern
                    if (!getProcessName(info)) {
                        throw std::runtime_error("Failed to get process name for PID " + std::to_string(info.pid) + ": " + info.errorMessage);
                    }
                    
                    // Get username using RAII pattern
                    if (!getProcessUsername(info)) {
                        throw std::runtime_error("Failed to get username for PID: " + std::to_string(info.pid));
                    }

                    info.success = true;
                    return info;
                }
            }
            
            throw std::runtime_error("No process found for port: " + std::to_string(port));
        }
        catch (const std::exception& e) {
            info.errorMessage = e.what();
            return info;
        }
    }

private:
    static std::string GetLastErrorAsString() {
        DWORD error = GetLastError();
        if (error == 0) {
            return "No error";
        }

        LPSTR messageBuffer = nullptr;
        size_t size = FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            error,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPSTR)&messageBuffer,
            0,
            nullptr
        );

        std::string message;
        if (size > 0 && messageBuffer != nullptr) {
            message = std::string(messageBuffer);
            LocalFree(messageBuffer);
            // Remove trailing newlines
            while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
                message.pop_back();
            }
        } else {
            message = "Error code: " + std::to_string(error);
        }

        return message;
    }

    // Helper function to parse tasklist output
    static bool parseTasklistOutput(const std::string& output, ProcessInfo& info) {
        std::istringstream stream(output);
        std::string line;
        
        // Variables to store parsed values
        std::string imageName;
        std::string userName;
        
        while (std::getline(stream, line)) {
            // Remove carriage return if present
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            
            // Parse key-value pairs
            if (line.find("Image Name:") == 0) {
                imageName = line.substr(11);
                // Trim whitespace
                imageName.erase(0, imageName.find_first_not_of(" \t"));
                imageName.erase(imageName.find_last_not_of(" \t") + 1);
                info.processName = imageName;
            }
            else if (line.find("User Name:") == 0) {
                userName = line.substr(10);
                // Trim whitespace
                userName.erase(0, userName.find_first_not_of(" \t"));
                userName.erase(userName.find_last_not_of(" \t") + 1);
                if (userName != "N/A") {
                    info.username = userName;
                }
            }
        }
        
        return !imageName.empty(); // Return true if we at least got the process name
    }

    static std::string getProcessNameWithError(ProcessInfo& info, std::string& errorMsg) {
        // First try with fewer privileges
        DWORD desiredAccess = PROCESS_QUERY_LIMITED_INFORMATION;
        HANDLE handle = OpenProcess(desiredAccess, FALSE, info.pid);
        
        if (!handle) {
            // If that fails, try with full query access
            desiredAccess = PROCESS_QUERY_INFORMATION | PROCESS_VM_READ;
            handle = OpenProcess(desiredAccess, FALSE, info.pid);
        }

        struct ProcessHandleGuard {
            HANDLE& handle;
            ProcessHandleGuard(HANDLE& h) : handle(h) {}
            ~ProcessHandleGuard() { if (handle) CloseHandle(handle); }
        } processHandle(handle);

        if (!handle) {
            DWORD error = GetLastError();
            switch (error) {
                case ERROR_ACCESS_DENIED:
                    errorMsg = "Access denied when opening process (PID: " + std::to_string(info.pid) + 
                             "). Process might be protected or running in a different security context.\n" +
                             "Attempted access rights: 0x" + std::to_string(desiredAccess);
                    break;
                case ERROR_INVALID_PARAMETER:
                    errorMsg = "Invalid process ID provided: " + std::to_string(info.pid);
                    break;
                default:
                    errorMsg = "Failed to open process: " + GetLastErrorAsString();
            }
            
            // Try to get process info using tasklist as fallback
            std::string cmd = "tasklist /v /FI \"PID eq " + std::to_string(info.pid) + "\" /FO LIST";
            FILE* pipe = _popen(cmd.c_str(), "r");
            if (pipe) {
                char buffer[4096]; // Increased buffer size to handle full output
                std::string result;
                while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                    result += buffer;
                }
                _pclose(pipe);
                
                if (!result.empty()) {
                    if (parseTasklistOutput(result, info)) {
                        return info.processName; // Return the process name we got from tasklist
                    }
                    errorMsg += "\nFailed to parse tasklist output:\n" + result;
                }
            }
            
            return "";
        }

        char processName[MAX_PATH];
        if (!GetModuleBaseNameA(handle, nullptr, processName, MAX_PATH)) {
            // Try GetProcessImageFileNameA as fallback
            if (!GetProcessImageFileNameA(handle, processName, MAX_PATH)) {
                DWORD error = GetLastError();
                switch (error) {
                    case ERROR_ACCESS_DENIED:
                        errorMsg = "Access denied when getting process name. Process might be protected.";
                        break;
                    case ERROR_INVALID_HANDLE:
                        errorMsg = "Invalid process handle.";
                        break;
                    default:
                        errorMsg = "Failed to get process name: " + GetLastErrorAsString();
                }
                return "";
            }
            
            // GetProcessImageFileNameA returns the device path, let's extract just the filename
            std::string fullPath(processName);
            size_t lastBackslash = fullPath.find_last_of('\\');
            if (lastBackslash != std::string::npos) {
                fullPath = fullPath.substr(lastBackslash + 1);
            }
            return fullPath;
        }

        return std::string(processName);
    }

    static bool getProcessName(ProcessInfo& info) {
        std::string errorMsg;
        std::string processName = getProcessNameWithError(info, errorMsg);
        
        if (processName.empty()) {
            info.errorMessage = errorMsg;
            return false;
        }

        info.processName = processName;
        return true;
    }

    static bool getProcessUsername(ProcessInfo& info) {
        // Use RAII for process and token handles
        struct ProcessHandleGuard {
            HANDLE handle;
            ProcessHandleGuard(DWORD pid) : handle(OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid)) {}
            ~ProcessHandleGuard() { if (handle) CloseHandle(handle); }
        } processHandle(info.pid);

        if (!processHandle.handle) {
            return false;
        }

        struct TokenHandleGuard {
            HANDLE handle = nullptr;
            TokenHandleGuard(HANDLE process) {
                OpenProcessToken(process, TOKEN_QUERY, &handle);
            }
            ~TokenHandleGuard() { if (handle) CloseHandle(handle); }
        } tokenHandle(processHandle.handle);

        if (!tokenHandle.handle) {
            return false;
        }

        DWORD size = 0;
        GetTokenInformation(tokenHandle.handle, TokenUser, nullptr, 0, &size);
        if (size == 0) {
            return false;
        }

        std::vector<char> buffer(size);
        PTOKEN_USER tokenUser = reinterpret_cast<PTOKEN_USER>(buffer.data());
        
        if (!GetTokenInformation(tokenHandle.handle, TokenUser, tokenUser, size, &size)) {
            return false;
        }

        char username[256];
        char domain[256];
        DWORD usernameSize = sizeof(username);
        DWORD domainSize = sizeof(domain);
        SID_NAME_USE sidType;
        
        if (LookupAccountSidA(nullptr, tokenUser->User.Sid, username, &usernameSize,
                            domain, &domainSize, &sidType)) {
            info.username = std::string(domain) + "\\" + username;
            return true;
        }
        return false;
    }
};









class IdentServer {
private:
    socket_t server_socket;
    int port;
    std::set<std::string> allowed_ips;
    
    void loadAllowedIPs(const std::string& config_file) {
        std::ifstream file(config_file);
        std::string line;
        
        while (std::getline(file, line)) {
            // Remove whitespace and comments
            size_t comment = line.find('#');
            if (comment != std::string::npos) {
                line = line.substr(0, comment);
            }
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);
            
            if (!line.empty()) {
                allowed_ips.insert(line);
            }
        }
    }

    bool isIPAllowed(const std::string& ip) {
        return allowed_ips.find(ip) != allowed_ips.end();
    }

    std::string getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    void handleClient(socket_t client_socket, const std::string& client_ip) {
        // Set timeout for receive operations
        struct timeval timeout;      
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
            std::cerr << "Failed to set socket timeout" << std::endl;
            return;
        }

        std::vector<char> buffer(1024);
        std::string request;
        size_t total_bytes = 0;
        
        // Read with proper error handling
        while (true) {
            int bytes_received = recv(client_socket, buffer.data(), buffer.size() - 1, 0);
            if (bytes_received < 0) {
                if (WSAGetLastError() == WSAEINTR) continue;  // Interrupted, retry
                std::cerr << "Error receiving data: " << WSAGetLastError() << std::endl;

                return;
            }
            if (bytes_received == 0) break;  // Connection closed
            
            total_bytes += bytes_received;
            if (total_bytes >= buffer.size()) {
                std::cerr << "Request too large" << std::endl;
                const char* error = "ERROR:REQUEST-TOO-LARGE\r\n";
                send(client_socket, error, strlen(error), 0);
                return;
            }
            
            buffer[bytes_received] = '\0';
            request.append(buffer.data(), bytes_received);
            
            // Check for complete request
            if (request.find("\r\n") != std::string::npos) break;
        }
        
        size_t comma_pos = request.find(',');
        if (comma_pos == std::string::npos) {
            const char* error = "ERROR:INVALID-FORMAT\r\nFormat: server_port,client_port\r\n";
            send(client_socket, error, strlen(error), 0);
            return;
        }
        
        try {
            int server_port = std::stoi(request.substr(0, comma_pos));
            int client_port = std::stoi(request.substr(comma_pos + 1));
            
            // Validate port numbers
            if (server_port < 0 || server_port > 65535 || client_port < 0 || client_port > 65535) {
                const char* error = "ERROR:INVALID-PORT-RANGE\r\n";
                send(client_socket, error, strlen(error), 0);
                return;
            }
            
            auto processInfo = ProcessIdentifier::getProcessForPort(server_port);
            std::string response;
            
            if (processInfo.pid != 0) {
                std::stringstream ss;
                ss << server_port << ":" << client_port << "\n"
                << "USERID: " << processInfo.username << "\n"
                << "PROCCESS NAME / PID:  [" << processInfo.processName << ":" << processInfo.pid << "]\n"
                << "ERROR: " << processInfo.errorMessage << "\r\n";
                response = ss.str();
            } else {
                std::stringstream ss;
                ss << server_port << ":" << client_port 
                << ": ERROR:NO-USER\r\n";
                response = ss.str();
            }
            
            // Send with retry on interrupt
            size_t total_sent = 0;
            while (total_sent < response.length()) {
                int sent = send(client_socket, 
                            response.c_str() + total_sent, 
                            response.length() - total_sent, 
                            0);
                if (sent < 0) {
                    if (WSAGetLastError() == WSAEINTR) continue;
                    std::cerr << "Error sending response: " << WSAGetLastError() << std::endl;

                    return;
                }
                total_sent += sent;
            }
            
            std::cout << "Handled request from " << client_ip << ": " << request;
        }
        catch (const std::exception& e) {
            const char* error = "ERROR:INVALID-REQUEST\r\n";
            send(client_socket, error, strlen(error), 0);
            std::cerr << "Error processing request: " << e.what() << std::endl;
        }
    }


public:
    IdentServer(int port = 113, const std::string& config_file = "ident-allowed-ips.conf") : port(port) {

        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket == INVALID_SOCKET) {
            throw std::system_error(errno, std::system_category(), "Socket creation failed");
        }

        int opt = 1;
        setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

        loadAllowedIPs(config_file);
        std::cout << "Loaded " << allowed_ips.size() << " allowed IP addresses" << std::endl;
    }

    ~IdentServer() {
        if (server_socket != INVALID_SOCKET) {
            closesocket(server_socket);
        }

        WSACleanup();

    }

    void start() {
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port);

        if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
            throw std::system_error(errno, std::system_category(), "Bind failed");
        }

        if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
            throw std::system_error(errno, std::system_category(), "Listen failed");
        }

        std::cout << "Ident server listening on port " << port << std::endl;

        while (true) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);

            socket_t client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
            if (client_socket != INVALID_SOCKET) {
                std::string client_ip = inet_ntoa(client_addr.sin_addr);

                if (isIPAllowed(client_ip)) {
                    handleClient(client_socket, client_ip);
                } else {
                    std::cout << "Rejected connection from unauthorized IP: " << client_ip << std::endl;
                }
                closesocket(client_socket);
            }
        }
    }
};

int main() {
    try {
        IdentServer server;
        server.start();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}