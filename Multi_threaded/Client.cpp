/**
 * Enhanced Multithreaded TCP Client
 * 
 * Features:
 * - Multiple concurrent client connections using threads
 * - Configurable connection parameters
 * - Robust error handling and logging
 * - File transfer capabilities
 * - Interactive and batch modes
 * - Performance metrics
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <random>
#include <memory>
#include <queue>
#include <condition_variable>
#include <functional>
#include <algorithm>
#include <iomanip>
#include <ctime>
#include <cstring>
#include <stdexcept>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <future>
#include <unordered_map>  // For unordered_map
#include <string>         // For std::string


using namespace std;

#pragma comment(lib, "ws2_32.lib")

// Buffer size for receiving data
#define BUFFER_SIZE 4096
// Default timeout in milliseconds
#define DEFAULT_TIMEOUT 5000
// Maximum retry attempts for connections
#define MAX_RETRIES 3

using namespace std;

// Forward declarations
class Logger;
class ClientMetrics;
class ClientConfig;
class ClientThread;
class ThreadPool;
class FileTransfer;

/**
 * Logger class for centralized logging with different severity levels
 */
class Logger {
public:
    enum LogLevel {
        DEBUG,
        INFO,
        WARNING,
        ERROR_,
        FATAL
    };

    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    void setLogLevel(LogLevel level) {
        m_logLevel = level;
    }

    void setLogToFile(const string& filename) {
        m_logToFile = true;
        m_logFile.open(filename, ios::out | ios::app);
        if (!m_logFile.is_open()) {
            cerr << "Failed to open log file: " << filename << endl;
            m_logToFile = false;
        }
    }

    void log(LogLevel level, const string& message) {
        if (level < m_logLevel) return;

        lock_guard<mutex> lock(m_mutex);
        
        // Get current time
        auto now = chrono::system_clock::now();
        auto now_c = chrono::system_clock::to_time_t(now);
        tm local_tm;
        localtime_s(&local_tm, &now_c);
        
        stringstream ss;
        ss << "[" << setfill('0') << setw(2) << local_tm.tm_hour << ":"
           << setfill('0') << setw(2) << local_tm.tm_min << ":"
           << setfill('0') << setw(2) << local_tm.tm_sec << "] "
           << "[" << levelToString(level) << "] " << message;
        
        string logMessage = ss.str();
        
        // Log to console
        if (level == ERROR || level == FATAL) {
            cerr << logMessage << endl;
        } else {
            cout << logMessage << endl;
        }
        
        // Log to file if enabled
        if (m_logToFile && m_logFile.is_open()) {
            m_logFile << logMessage << endl;
            m_logFile.flush();
        }
    }

    void debug(const string& message) {
        log(DEBUG, message);
    }

    void info(const string& message) {
        log(INFO, message);
    }

    void warning(const string& message) {
        log(WARNING, message);
    }

    void error(const string& message) {
        log(ERROR_, message);
    }

    void fatal(const string& message) {
        log(FATAL, message);
    }

    ~Logger() {
        if (m_logToFile && m_logFile.is_open()) {
            m_logFile.close();
        }
    }

private:
    Logger() : m_logLevel(INFO), m_logToFile(false) {}
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    string levelToString(LogLevel level) {
        switch (level) {
            case DEBUG: return "DEBUG";
            case INFO: return "INFO";
            case WARNING: return "WARNING";
            case ERROR_: return "ERROR";
            case FATAL: return "FATAL";
            default: return "UNKNOWN";
        }
    }

    LogLevel m_logLevel;
    bool m_logToFile;
    ofstream m_logFile;
    mutex m_mutex;
};

/**
 * Class to track client performance metrics
 */
class ClientMetrics {
public:
    ClientMetrics() : 
        m_totalConnections(0),
        m_successfulConnections(0),
        m_failedConnections(0),
        m_totalBytesSent(0),
        m_totalBytesReceived(0),
        m_totalResponseTime(0) {}

    void incrementTotalConnections() {
        m_totalConnections++;
    }

    void incrementSuccessfulConnections() {
        m_successfulConnections++;
    }

    void incrementFailedConnections() {
        m_failedConnections++;
    }

    void addBytesSent(size_t bytes) {
        m_totalBytesSent += bytes;
    }

    void addBytesReceived(size_t bytes) {
        m_totalBytesReceived += bytes;
    }

    void addResponseTime(chrono::milliseconds time) {
        m_totalResponseTime += time.count();
    }

    void addConnectionTime(const string& server, chrono::milliseconds time) {
        lock_guard<mutex> lock(m_mutex);
        m_serverResponseTimes[server] += time.count();
        m_serverConnections[server]++;
    }

    void displayMetrics() const {
        cout << "\n===== Client Performance Metrics =====\n";
        cout << "Total connection attempts: " << m_totalConnections << endl;
        cout << "Successful connections: " << m_successfulConnections << endl;
        cout << "Failed connections: " << m_failedConnections << endl;
        
        if (m_successfulConnections > 0) {
            cout << "Average response time: " << (m_totalResponseTime / m_successfulConnections) << " ms" << endl;
        }
        
        cout << "Total data sent: " << formatDataSize(m_totalBytesSent) << endl;
        cout << "Total data received: " << formatDataSize(m_totalBytesReceived) << endl;
        
        if (!m_serverResponseTimes.empty()) {
            cout << "\nServer-specific metrics:\n";
            for (const auto& server : m_serverResponseTimes) {
                cout << "Server " << server.first << ":\n";
                cout << "  Connections: " << m_serverConnections.at(server.first) << endl;
                cout << "  Average response time: " 
                     << (server.second / m_serverConnections.at(server.first)) << " ms" << endl;
            }
        }
        cout << "=====================================\n";
    }

private:
    string formatDataSize(size_t bytes) const {
        const char* suffixes[] = {"B", "KB", "MB", "GB", "TB"};
        int suffixIndex = 0;
        double size = static_cast<double>(bytes);
        
        while (size >= 1024 && suffixIndex < 4) {
            size /= 1024;
            suffixIndex++;
        }
        
        stringstream ss;
        ss << fixed << setprecision(2) << size << " " << suffixes[suffixIndex];
        return ss.str();
    }

    atomic<size_t> m_totalConnections;
    atomic<size_t> m_successfulConnections;
    atomic<size_t> m_failedConnections;
    atomic<size_t> m_totalBytesSent;
    atomic<size_t> m_totalBytesReceived;
    atomic<long long> m_totalResponseTime;
    
    mutex m_mutex;
    unordered_map<string, long long> m_serverResponseTimes;
    unordered_map<string, int> m_serverConnections;
};

/**
 * Configuration class for client settings
 */
class ClientConfig {
public:
    ClientConfig() :
        m_serverAddress("127.0.0.1"),
        m_port(8080),
        m_numConnections(5),
        m_timeout(DEFAULT_TIMEOUT),
        m_retryAttempts(MAX_RETRIES),
        m_retryDelay(1000),
        m_batchMode(false) {}

    string getServerAddress() const { return m_serverAddress; }
    int getPort() const { return m_port; }
    int getNumConnections() const { return m_numConnections; }
    int getTimeout() const { return m_timeout; }
    int getRetryAttempts() const { return m_retryAttempts; }
    int getRetryDelay() const { return m_retryDelay; }
    bool getBatchMode() const { return m_batchMode; }
    const vector<string>& getMessages() const { return m_messages; }




    void setServerAddress(const string& address) { m_serverAddress = address; }
    void setPort(int port) { m_port = port; }
    void setNumConnections(int num) { m_numConnections = num; }
    void setTimeout(int timeout) { m_timeout = timeout; }
    void setRetryAttempts(int attempts) { m_retryAttempts = attempts; }
    void setRetryDelay(int delay) { m_retryDelay = delay; }
    void setBatchMode(bool mode) { m_batchMode = mode; }
    
    void addMessage(const string& message) {
        m_messages.push_back(message);
    }




    void loadConfigFromFile(const string& filename) {
        ifstream file(filename);
        if (!file.is_open()) {
            throw runtime_error("Failed to open config file: " + filename);
        }

        string line;
        while (getline(file, line)) {
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#') {
                continue;
            }

            size_t pos = line.find('=');
            if (pos == string::npos) {
                continue;
            }

            string key = line.substr(0, pos);
            string value = line.substr(pos + 1);
            
            // Remove whitespace
            key.erase(remove_if(key.begin(), key.end(), ::isspace), key.end());
            value.erase(remove_if(value.begin(), value.end(), ::isspace), value.end());

            if (key == "server_address") {
                m_serverAddress = value;
            } else if (key == "port") {
                m_port = stoi(value);
            } else if (key == "num_connections") {
                m_numConnections = stoi(value);
            } else if (key == "timeout") {
                m_timeout = stoi(value);
            } else if (key == "retry_attempts") {
                m_retryAttempts = stoi(value);
            } else if (key == "retry_delay") {
                m_retryDelay = stoi(value);
            } else if (key == "batch_mode") {
                m_batchMode = (value == "true" || value == "1");
            } else if (key == "message") {
                m_messages.push_back(value);
            }
        }
    }



    void saveConfigToFile(const string& filename) {
        ofstream file(filename);
        if (!file.is_open()) {
            throw runtime_error("Failed to open config file for writing: " + filename);
        }

        file << "# Client Configuration\n";
        file << "server_address=" << m_serverAddress << endl;
        file << "port=" << m_port << endl;
        file << "num_connections=" << m_numConnections << endl;
        file << "timeout=" << m_timeout << endl;
        file << "retry_attempts=" << m_retryAttempts << endl;
        file << "retry_delay=" << m_retryDelay << endl;
        file << "batch_mode=" << (m_batchMode ? "true" : "false") << endl;
        
        for (const auto& message : m_messages) {
            file << "message=" << message << endl;
        }
    }

private:
    string m_serverAddress;
    int m_port;
    int m_numConnections;
    int m_timeout;
    int m_retryAttempts;
    int m_retryDelay;
    bool m_batchMode;
    vector<string> m_messages;
};

/**
 * File transfer utilities
 */
class FileTransfer {
public:
    static bool sendFile(SOCKET sock, const string& filename) {
        ifstream file(filename, ios::binary);
        if (!file.is_open()) {
            Logger::getInstance().error("Failed to open file for sending: " + filename);
            return false;
        }

        // Get file size
        file.seekg(0, ios::end);
        size_t fileSize = file.tellg();
        file.seekg(0, ios::beg);

        // Send file name and size first
        string header = "FILE:" + filename + ":" + to_string(fileSize) + "\n";
        if (send(sock, header.c_str(), header.length(), 0) == SOCKET_ERROR) {
            Logger::getInstance().error("Failed to send file header: " + to_string(WSAGetLastError()));
            return false;
        }

        // Read and send file in chunks
        char buffer[BUFFER_SIZE];
        size_t totalSent = 0;
        size_t bytesRead;

        while (totalSent < fileSize) {
            file.read(buffer, min(static_cast<size_t>(BUFFER_SIZE), fileSize - totalSent));
            bytesRead = file.gcount();

            if (bytesRead <= 0) break;

            int sendResult = send(sock, buffer, bytesRead, 0);
            if (sendResult == SOCKET_ERROR) {
                Logger::getInstance().error("Failed to send file data: " + to_string(WSAGetLastError()));
                return false;
            }

            totalSent += sendResult;
        }

        Logger::getInstance().info("File sent successfully: " + filename + " (" + to_string(totalSent) + " bytes)");
        return true;
    }

    static bool receiveFile(SOCKET sock, const string& saveDir) {
        char buffer[BUFFER_SIZE];
        int bytesReceived = recv(sock, buffer, BUFFER_SIZE - 1, 0);
        
        if (bytesReceived <= 0) {
            Logger::getInstance().error("Failed to receive file header");
            return false;
        }

        buffer[bytesReceived] = '\0';
        string headerStr(buffer);
        
        // Parse header
        if (headerStr.substr(0, 5) != "FILE:") {
            Logger::getInstance().error("Invalid file header received");
            return false;
        }

        size_t pos1 = headerStr.find(':', 5);
        if (pos1 == string::npos) {
            Logger::getInstance().error("Malformed file header");
            return false;
        }

        size_t pos2 = headerStr.find('\n', pos1);
        if (pos2 == string::npos) {
            Logger::getInstance().error("Malformed file header, missing newline");
            return false;
        }

        string filename = headerStr.substr(5, pos1 - 5);
        size_t fileSize = stoull(headerStr.substr(pos1 + 1, pos2 - pos1 - 1));
        
        // Create the output file
        string savePath = saveDir + "\\" + filename;
        ofstream outFile(savePath, ios::binary);
        if (!outFile.is_open()) {
            Logger::getInstance().error("Failed to open output file: " + savePath);
            return false;
        }

        // Calculate how much of the file data is already in the buffer
        int headerSize = pos2 + 1;
        int dataInBuffer = bytesReceived - headerSize;
        
        // Write initial data from buffer
        if (dataInBuffer > 0) {
            outFile.write(buffer + headerSize, dataInBuffer);
        }
        
        size_t totalReceived = dataInBuffer;
        
        // Receive the rest of the file
        while (totalReceived < fileSize) {
            bytesReceived = recv(sock, buffer, BUFFER_SIZE, 0);
            
            if (bytesReceived <= 0) {
                Logger::getInstance().error("Connection closed while receiving file");
                outFile.close();
                return false;
            }
            
            outFile.write(buffer, bytesReceived);
            totalReceived += bytesReceived;
        }
        
        outFile.close();
        Logger::getInstance().info("File received successfully: " + savePath);
        return true;
    }
};




/**
 * Thread pool for managing client connections
 */
class ThreadPool {
public:
    ThreadPool(size_t numThreads = thread::hardware_concurrency()) : m_stop(false) {
        for (size_t i = 0; i < numThreads; ++i) {
            m_workers.emplace_back([this] {
                while (true) {
                    function<void()> task;
                    {
                        unique_lock<mutex> lock(m_queueMutex);
                        m_condition.wait(lock, [this] { 
                            return m_stop || !m_tasks.empty(); 
                        });
                        
                        if (m_stop && m_tasks.empty()) {
                            return;
                        }
                        
                        task = move(m_tasks.front());
                        m_tasks.pop();
                    }
                    
                    task();
                }
            });
        }
    }
    

    template<class F>
    void enqueue(F&& f) {
        {
            unique_lock<mutex> lock(m_queueMutex);
            m_tasks.emplace(forward<F>(f));
        }
        m_condition.notify_one();
    }
    
    ~ThreadPool() {
        {
            unique_lock<mutex> lock(m_queueMutex);
            m_stop = true;
        }
        
        m_condition.notify_all();
        
        for (thread& worker : m_workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
    
private:
    vector<thread> m_workers;
    queue<function<void()>> m_tasks;
    mutex m_queueMutex;
    condition_variable m_condition;
    bool m_stop;
};

/**
 * Client thread class representing a single client connection
 */
class ClientThread {
public:
    ClientThread(const ClientConfig& config, ClientMetrics& metrics, int clientId) :
        m_config(config),
        m_metrics(metrics),
        m_clientId(clientId),
        m_socket(INVALID_SOCKET) {}

    void run() {
        auto& logger = Logger::getInstance();
        logger.info("Client thread " + to_string(m_clientId) + " starting...");
        
        m_metrics.incrementTotalConnections();
        
        for (int attempt = 0; attempt < m_config.getRetryAttempts(); ++attempt) {
            try {
                if (connectToServer()) {
                    // Successfully connected
                    m_metrics.incrementSuccessfulConnections();
                    
                    // Send data to the server
                    sendData();
                    
                    // Receive response
                    receiveData();
                    
                    // Close the connection
                    cleanup();
                    
                    return;
                }
            } catch (const exception& ex) {
                logger.error("Client thread " + to_string(m_clientId) + " exception: " + ex.what());
            }
            
            // If we reached here, connection failed
            logger.warning("Connection attempt " + to_string(attempt + 1) + " failed. Retrying...");
            
            // Wait before retrying
            this_thread::sleep_for(chrono::milliseconds(m_config.getRetryDelay()));
        }
        
        // If we reached here after all retry attempts, mark as failed
        m_metrics.incrementFailedConnections();
        logger.error("Client thread " + to_string(m_clientId) + " failed after " + 
                    to_string(m_config.getRetryAttempts()) + " attempts");
    }

private:
    bool connectToServer() {
        WSADATA wsaData;
        struct sockaddr_in serv_addr;
        auto& logger = Logger::getInstance();
        
        // Initialize Winsock
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            logger.error("WSAStartup failed with error: " + to_string(WSAGetLastError()));
            return false;
        }
        
        // Create socket
        m_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (m_socket == INVALID_SOCKET) {
            logger.error("Socket creation error: " + to_string(WSAGetLastError()));
            WSACleanup();
            return false;
        }
        
        // Set timeout
        DWORD timeout = m_config.getTimeout();
        if (setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) == SOCKET_ERROR) {
            logger.warning("Failed to set receive timeout: " + to_string(WSAGetLastError()));
        }
        
        if (setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout)) == SOCKET_ERROR) {
            logger.warning("Failed to set send timeout: " + to_string(WSAGetLastError()));
        }
        
        // Set up server address
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(m_config.getPort());
        
        // Convert IPv4 address from text to binary form
        serv_addr.sin_addr.s_addr = inet_addr(m_config.getServerAddress().c_str());
        // Connect to server
        logger.info("Client " + to_string(m_clientId) + " connecting to " + 
                   m_config.getServerAddress() + ":" + to_string(m_config.getPort()));
        
        auto startTime = chrono::high_resolution_clock::now();
        
        if (connect(m_socket, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == SOCKET_ERROR) {
            logger.error("Connection failed with error: " + to_string(WSAGetLastError()));
            cleanup();
            return false;
        }
        
        auto endTime = chrono::high_resolution_clock::now();
        auto duration = chrono::duration_cast<chrono::milliseconds>(endTime - startTime);
        
        m_metrics.addConnectionTime(m_config.getServerAddress(), duration);
        
        logger.info("Client " + to_string(m_clientId) + " connected successfully in " + to_string(duration.count()) + "ms");
        
        return true;
    }
    
    void sendData() {
        auto& logger = Logger::getInstance();
        
        // Get message to send
        string message;
        const vector<string>& messages = m_config.getMessages();
        
        if (!messages.empty()) {
            // Use messages from config if available
            size_t index = m_clientId % messages.size();
            message = messages[index];
        } else {
            // Default message
            message = "Hello from client " + to_string(m_clientId);
        }
        
       std::ostringstream oss;
       oss << std::this_thread::get_id();
       message += " (Thread ID: " + oss.str() + ")";

        
        // Send the message
        int sendResult = send(m_socket, message.c_str(), message.length(), 0);
        if (sendResult == SOCKET_ERROR) {
            logger.error("Send failed with error: " + to_string(WSAGetLastError()));
            return;
        }
        
        m_metrics.addBytesSent(sendResult);
        logger.info("Client " + to_string(m_clientId) + " sent: " + message);
    }
    
    void receiveData() {
        auto& logger = Logger::getInstance();
        char buffer[BUFFER_SIZE];
        
        auto startTime = chrono::high_resolution_clock::now();
        
        // Receive response from server
        int bytesReceived = recv(m_socket, buffer, BUFFER_SIZE - 1, 0);
        
        auto endTime = chrono::high_resolution_clock::now();
        auto duration = chrono::duration_cast<chrono::milliseconds>(endTime - startTime);
        
        m_metrics.addResponseTime(duration);
        
        if (bytesReceived > 0) {
            buffer[bytesReceived] = '\0';
            m_metrics.addBytesReceived(bytesReceived);
            logger.info("Client " + to_string(m_clientId) + " received: " + string(buffer) + 
                       " (in " + to_string(duration.count()) + "ms)");
        } else if (bytesReceived == 0) {
            logger.warning("Client " + to_string(m_clientId) + ": Connection closed by server");
        } else {
            logger.error("Client " + to_string(m_clientId) + ": Receive failed with error: " + 
                        to_string(WSAGetLastError()));
        }
    }
    
    void cleanup() {
        if (m_socket != INVALID_SOCKET) {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }
        WSACleanup();
    }

    const ClientConfig& m_config;
    ClientMetrics& m_metrics;
    int m_clientId;
    SOCKET m_socket;
};

/**
 * Main client class that orchestrates the operations
 */
class Client {
public:
    Client() : m_threadPool(thread::hardware_concurrency()) {
        // Set default log level
        Logger::getInstance().setLogLevel(Logger::INFO);
    }

    void loadConfig(const string& filename) {
        m_config.loadConfigFromFile(filename);
        Logger::getInstance().info("Configuration loaded from " + filename);
    }

    void saveConfig(const string& filename) {
        m_config.saveConfigToFile(filename);
        Logger::getInstance().info("Configuration saved to " + filename);
    }

    void run() {
        Logger::getInstance().info("Starting client with " + to_string(m_config.getNumConnections()) + " connections");
        
        vector<future<void>> futures;
        for (int i = 0; i < m_config.getNumConnections(); ++i) {
            futures.push_back(async(launch::async, [this, i]() {
                ClientThread client(m_config, m_metrics, i);
                client.run();
            }));
        }
        
        // Wait for all connections to complete
        for (auto& f : futures) {
            f.wait();
        }
        
        // Display performance metrics
        m_metrics.displayMetrics();
    }

    void runInteractive() {
        auto& logger = Logger::getInstance();
        logger.info("Interactive mode started. Type 'exit' to quit.");
        
        string command;
        while (true) {
            cout << "\nEnter command: ";
            getline(cin, command);
            
            if (command == "exit") {
                break;
            } else if (command == "help") {
                displayHelp();
            } else if (command == "connect") {
                int numConnections;
                cout << "Number of connections: ";
                cin >> numConnections;
                cin.ignore();
                
                m_config.setNumConnections(numConnections);
                run();
            } else if (command == "config") {
                configureSettings();
            } else if (command == "stats") {
                m_metrics.displayMetrics();
            } else if (command == "send") {
                string message;
                cout << "Enter message to send: ";
                getline(cin, message);
                
                m_config.addMessage(message);
                m_config.setNumConnections(1);
                run();
            } else if (command == "sendfile") {
                string filename;
                cout << "Enter file path to send: ";
                getline(cin, filename);
                
                // TODO: Implement file sending logic
                logger.info("File sending not implemented in interactive mode yet");
            } else {
                logger.warning("Unknown command: " + command);
            }
        }
    }

    ClientConfig& getConfig() {
        return m_config;
    }

    

private:
    void displayHelp() {
        cout << "\nAvailable commands:\n";
        cout << "  help     - Display this help message\n";
        cout << "  connect  - Start client connections\n";
        cout << "  config   - Configure client settings\n";
        cout << "  stats    - Display performance metrics\n";
        cout << "  send     - Send a custom message\n";
        cout << "  sendfile - Send a file to the server\n";
        cout << "  exit     - Exit the program\n";
    }

    void configureSettings() {
        string address;
        int port, timeout, retries, delay;
        
        cout << "Server address [" << m_config.getServerAddress() << "]: ";
        getline(cin, address);
        if (!address.empty()) {
            m_config.setServerAddress(address);
        }
        
        cout << "Port [" << m_config.getPort() << "]: ";
        string portStr;
        getline(cin, portStr);
        if (!portStr.empty()) {
            m_config.setPort(stoi(portStr));
        }
        
        cout << "Connection timeout (ms) [" << m_config.getTimeout() << "]: ";
        string timeoutStr;
        getline(cin, timeoutStr);
        if (!timeoutStr.empty()) {
            m_config.setTimeout(stoi(timeoutStr));
        }
        
        cout << "Retry attempts [" << m_config.getRetryAttempts() << "]: ";
        string retriesStr;
        getline(cin, retriesStr);
        if (!retriesStr.empty()) {
            m_config.setRetryAttempts(stoi(retriesStr));
        }
        
        cout << "Retry delay (ms) [" << m_config.getRetryDelay() << "]: ";
        string delayStr;
        getline(cin, delayStr);
        if (!delayStr.empty()) {
            m_config.setRetryDelay(stoi(delayStr));
        }
        
        Logger::getInstance().info("Configuration updated");
    }

    ClientConfig m_config;
    ClientMetrics m_metrics;
    ThreadPool m_threadPool;
};

/**
 * Main function with command-line argument handling
 */
int main(int argc, char* argv[]) {
    try {
        Client client;


        
        // Set up logger
        Logger::getInstance().setLogLevel(Logger::INFO);
        Logger::getInstance().setLogToFile("client_log.txt");
        
        // Parse command line arguments
        for (int i = 1; i < argc; i++) {
            string arg = argv[i];
            
            if (arg == "-h" || arg == "--help") {
                cout << "Usage: " << argv[0] << " [options]\n";
                cout << "Options:\n";
                cout << "  -h, --help                  Show this help message\n";
                cout << "  -c, --config <file>         Load configuration from file\n";
                cout << "  -s, --server <address>      Server address\n";
                cout << "  -p, --port <port>           Server port\n";
                cout << "  -n, --num-connections <n>   Number of connections\n";
                cout << "  -t, --timeout <ms>          Connection timeout in milliseconds\n";
                cout << "  -r, --retries <n>           Number of retry attempts\n";
                cout << "  -d, --delay <ms>            Retry delay in milliseconds\n";
                cout << "  -m, --message <message>     Message to send\n";
                cout << "  -i, --interactive           Run in interactive mode\n";
                cout << "  -b, --batch                 Run in batch mode\n";
                cout << "  -l, --log-level <level>     Set log level (DEBUG, INFO, WARNING, ERROR, FATAL)\n";
                return 0;
            } else if (arg == "-c" || arg == "--config") {
                if (i + 1 < argc) {
                    client.loadConfig(argv[++i]);
                }
            } else if (arg == "-s" || arg == "--server") {
                if (i + 1 < argc) {
                    client.getConfig().setServerAddress(argv[++i]);
                }
            } else if (arg == "-p" || arg == "--port") {
                if (i + 1 < argc) {
                    client.getConfig().setPort(stoi(argv[++i]));
                }
            } else if (arg == "-n" || arg == "--num-connections") {
                if (i + 1 < argc) {
                    client.getConfig().setNumConnections(stoi(argv[++i]));
                }
            } else if (arg == "-t" || arg == "--timeout") {
                if (i + 1 < argc) {
                    client.getConfig().setTimeout(stoi(argv[++i]));
                }
            } else if (arg == "-r" || arg == "--retries") {
                if (i + 1 < argc) {
                    client.getConfig().setRetryAttempts(stoi(argv[++i]));
                }
            } else if (arg == "-d" || arg == "--delay") {
                if (i + 1 < argc) {
                    client.getConfig().setRetryDelay(stoi(argv[++i]));
                }
            } else if (arg == "-m" || arg == "--message") {
                if (i + 1 < argc) {
                    client.getConfig().addMessage(argv[++i]);
                }
            } else if (arg == "-i" || arg == "--interactive") {
                client.runInteractive();
                return 0;
            } else if (arg == "-b" || arg == "--batch") {
                client.getConfig().setBatchMode(true);
            } else if (arg == "-l" || arg == "--log-level") {
                if (i + 1 < argc) {
                    string level = argv[++i];
                    if (level == "DEBUG") {
                        Logger::getInstance().setLogLevel(Logger::DEBUG);
                    } else if (level == "INFO") {
                        Logger::getInstance().setLogLevel(Logger::INFO);
                    } else if (level == "WARNING") {
                        Logger::getInstance().setLogLevel(Logger::WARNING);
                    } else if (level == "ERROR") {
                        Logger::getInstance().setLogLevel(Logger::ERROR_);
                    } else if (level == "FATAL") {
                        Logger::getInstance().setLogLevel(Logger::FATAL);
                    }
                }
            }
        }

        
        // Run the client in batch mode
        client.run();
        

    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }
    
    return 0;
}