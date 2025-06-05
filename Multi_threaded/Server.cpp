/**
 * Enhanced Multithreaded TCP Server
 * 
 * Features:
 * - Multiple concurrent client handling using threads
 * - Configurable server parameters
 * - Robust error handling and logging
 * - File transfer capabilities
 * - Performance metrics
 * - Thread pool for efficient connection management
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
#include <queue>
#include <memory>
#include <condition_variable>
#include <functional>
#include <unordered_map>
#include <algorithm>
#include <iomanip>
#include <ctime>
#include <cstring>
#include <stdexcept>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <direct.h>
#include <future>

#pragma comment(lib, "ws2_32.lib")



// Buffer size for receiving data
#define BUFFER_SIZE 4096
// Default timeout in milliseconds
#define DEFAULT_TIMEOUT 30000
// Maximum pending connections
#define MAX_PENDING_CONNECTIONS 10

using namespace std;



// Forward declarations
class Logger;
class ServerMetrics;
class ServerConfig;
class ClientHandler;
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
 * Class to track server performance metrics
 */
class ServerMetrics {
public:
    ServerMetrics() :
        m_totalConnections(0),
        m_activeConnections(0),
        m_totalBytesSent(0),
        m_totalBytesReceived(0),
        m_totalProcessingTime(0) {}

    void incrementTotalConnections() {
        m_totalConnections++;
    }

    void incrementActiveConnections() {
        m_activeConnections++;
    }

    void decrementActiveConnections() {
        m_activeConnections--;
    }

    void addBytesSent(size_t bytes) {
        m_totalBytesSent += bytes;
    }

    void addBytesReceived(size_t bytes) {
        m_totalBytesReceived += bytes;
    }

    void addProcessingTime(chrono::milliseconds time) {
        m_totalProcessingTime += time.count();
    }

    void addClientData(const string& clientIp, size_t bytesSent, size_t bytesReceived, long long processingTime) {
        lock_guard<mutex> lock(m_mutex);
        m_clientStats[clientIp].connections++;
        m_clientStats[clientIp].bytesSent += bytesSent;
        m_clientStats[clientIp].bytesReceived += bytesReceived;
        m_clientStats[clientIp].processingTime += processingTime;
    }

    void displayMetrics() const {
        cout << "\n===== Server Performance Metrics =====\n";
        cout << "Total connections handled: " << m_totalConnections << endl;
        cout << "Currently active connections: " << m_activeConnections << endl;
        
        if (m_totalConnections > 0) {
            cout << "Average processing time: " << (m_totalProcessingTime / m_totalConnections) << " ms" << endl;
        }
        
        cout << "Total data sent: " << formatDataSize(m_totalBytesSent) << endl;
        cout << "Total data received: " << formatDataSize(m_totalBytesReceived) << endl;
        
        if (!m_clientStats.empty()) {
            cout << "\nTop client statistics:\n";
            
            // Get top 5 clients by connection count
            vector<pair<string, ClientStat>> clientStats(m_clientStats.begin(), m_clientStats.end());
            sort(clientStats.begin(), clientStats.end(), 
                [](const pair<string, ClientStat>& a, const pair<string, ClientStat>& b) {
                    return a.second.connections > b.second.connections;
                });
                
            int count = 0;
            for (const auto& client : clientStats) {
                cout << "Client IP: " << client.first << "\n";
                cout << "  Connections: " << client.second.connections << "\n";
                cout << "  Data sent: " << formatDataSize(client.second.bytesSent) << "\n";
                cout << "  Data received: " << formatDataSize(client.second.bytesReceived) << "\n";
                
                if (client.second.connections > 0) {
                    cout << "  Average processing time: " 
                         << (client.second.processingTime / client.second.connections) << " ms\n";
                }
                
                if (++count >= 5) break;
            }
        }
        cout << "=====================================\n";
    }

private:
    struct ClientStat {
        size_t connections = 0;
        size_t bytesSent = 0;
        size_t bytesReceived = 0;
        long long processingTime = 0;
    };


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
    atomic<size_t> m_activeConnections;
    atomic<size_t> m_totalBytesSent;
    atomic<size_t> m_totalBytesReceived;
    atomic<long long> m_totalProcessingTime;
    
    mutex m_mutex;
    unordered_map<string, ClientStat> m_clientStats;
};


/**
 * Configuration class for server settings
 */
class ServerConfig {
public:
    ServerConfig() :
        m_port(8080),
        m_maxConnections(100),
        m_timeout(DEFAULT_TIMEOUT),
        m_backlogSize(MAX_PENDING_CONNECTIONS),
        m_threadPoolSize(thread::hardware_concurrency()),
        m_downloadDirectory("downloads"),
        m_uploadDirectory("uploads") {}

    int getPort() const { return m_port; }
    int getMaxConnections() const { return m_maxConnections; }
    int getTimeout() const { return m_timeout; }
    int getBacklogSize() const { return m_backlogSize; }
    int getThreadPoolSize() const { return m_threadPoolSize; }
    string getDownloadDirectory() const { return m_downloadDirectory; }
    string getUploadDirectory() const { return m_uploadDirectory; }

    void setPort(int port) { m_port = port; }
    void setMaxConnections(int max) { m_maxConnections = max; }
    void setTimeout(int timeout) { m_timeout = timeout; }
    void setBacklogSize(int size) { m_backlogSize = size; }
    void setThreadPoolSize(int size) { m_threadPoolSize = size; }
    void setDownloadDirectory(const string& dir) { m_downloadDirectory = dir; }
    void setUploadDirectory(const string& dir) { m_uploadDirectory = dir; }

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

            if (key == "port") {
                m_port = stoi(value);
            } else if (key == "max_connections") {
                m_maxConnections = stoi(value);
            } else if (key == "timeout") {
                m_timeout = stoi(value);
            } else if (key == "backlog_size") {
                m_backlogSize = stoi(value);
            } else if (key == "thread_pool_size") {
                m_threadPoolSize = stoi(value);
            } else if (key == "download_directory") {
                m_downloadDirectory = value;
            } else if (key == "upload_directory") {
                m_uploadDirectory = value;
            }
        }
    }

    void saveConfigToFile(const string& filename) {
        ofstream file(filename);
        if (!file.is_open()) {
            throw runtime_error("Failed to open config file for writing: " + filename);
        }

        file << "# Server Configuration\n";
        file << "port=" << m_port << endl;
        file << "max_connections=" << m_maxConnections << endl;
        file << "timeout=" << m_timeout << endl;
        file << "backlog_size=" << m_backlogSize << endl;
        file << "thread_pool_size=" << m_threadPoolSize << endl;
        file << "download_directory=" << m_downloadDirectory << endl;
        file << "upload_directory=" << m_uploadDirectory << endl;
    }

private:
    int m_port;
    int m_maxConnections;
    int m_timeout;
    int m_backlogSize;
    int m_threadPoolSize;
    string m_downloadDirectory;
    string m_uploadDirectory;
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
            // Change this line in the FileTransfer::sendFile method
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
 * Thread pool for managing server connections
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
 * Handler for client connections
 */
class ClientHandler {
public:
    ClientHandler(SOCKET clientSocket, const sockaddr_in& clientAddr, ServerMetrics& metrics, const ServerConfig& config) :
        m_clientSocket(clientSocket),
        m_clientAddr(clientAddr),
        m_metrics(metrics),
        m_config(config),
        m_bytesSent(0),
        m_bytesReceived(0) {
        
        char* ipStr = inet_ntoa(m_clientAddr.sin_addr);
        std::string clientIp(ipStr);

    }

    void handleClient() {
        auto& logger = Logger::getInstance();
        logger.info("Handling client: " + m_clientIp + ":" + to_string(ntohs(m_clientAddr.sin_port)));
        
        m_metrics.incrementTotalConnections();
        m_metrics.incrementActiveConnections();
        
        auto startTime = chrono::high_resolution_clock::now();
        


        try {
            // Process client request
            bool keepAlive = processClientRequest();
            
            // Keep connection alive if requested
            while (keepAlive) {
                keepAlive = processClientRequest();
            }
        } catch (const exception& ex) {
            logger.error("Error handling client: " + string(ex.what()));
        }
        
        auto endTime = chrono::high_resolution_clock::now();
        auto duration = chrono::duration_cast<chrono::milliseconds>(endTime - startTime);
        
        m_metrics.addProcessingTime(duration);
        m_metrics.addClientData(m_clientIp, m_bytesSent, m_bytesReceived, duration.count());
        
        // Close the connection
        closesocket(m_clientSocket);
        m_metrics.decrementActiveConnections();
        
        logger.info("Client disconnected: " + m_clientIp + " (processed in " + 
                   to_string(duration.count()) + "ms)");
    }

private:
    bool processClientRequest() {
        auto& logger = Logger::getInstance();
        char buffer[BUFFER_SIZE];
        
        // Receive data from client
        int bytesReceived = recv(m_clientSocket, buffer, BUFFER_SIZE - 1, 0);
        if (bytesReceived <= 0) {
            if (bytesReceived == 0) {
                logger.info("Client closed connection: " + m_clientIp);
            } else {
                logger.error("Receive failed: " + to_string(WSAGetLastError()));
            }
            return false;
        }
        
        buffer[bytesReceived] = '\0';
        m_bytesReceived += bytesReceived;
        m_metrics.addBytesReceived(bytesReceived);
        
        string request(buffer);
        logger.info("Received from " + m_clientIp + ": " + request);
        
        // Check if this is a file transfer request
        if (request.substr(0, 5) == "FILE:") {
            return handleFileTransfer(request);
        }
        
        // Process regular request
        string response = "Server response: Received '" + request + "' from " + m_clientIp;
        
        int bytesSent = send(m_clientSocket, response.c_str(), response.length(), 0);
        if (bytesSent == SOCKET_ERROR) {
            logger.error("Send failed: " + to_string(WSAGetLastError()));
            return false;
        }
        
        m_bytesSent += bytesSent;
        m_metrics.addBytesSent(bytesSent);
        
        // Return true to keep connection alive for simple demo
        // In a real server, you'd analyze the request to determine if 
        // the connection should be kept alive
        return false;
    }
    
    bool handleFileTransfer(const string& request) {
        auto& logger = Logger::getInstance();
        
        if (request.substr(0, 9) == "FILE:GET:") {
            // Client is requesting a file
            string filename = request.substr(9);
            logger.info("Client requested file: " + filename);
            
            string filePath = m_config.getDownloadDirectory() + "\\" + filename;
            
            if (!FileTransfer::sendFile(m_clientSocket, filePath)) {
                string errorMsg = "ERROR: File not found or couldn't be sent";
                send(m_clientSocket, errorMsg.c_str(), errorMsg.length(), 0);
                return false;
            }
            
            return true;
            
        } else if (request.substr(0, 9) == "FILE:PUT:") {
            // Client wants to upload a file
            logger.info("Client wants to upload a file");
            
            string response = "READY_TO_RECEIVE";
            send(m_clientSocket, response.c_str(), response.length(), 0);
            
            if (!FileTransfer::receiveFile(m_clientSocket, m_config.getUploadDirectory())) {
                return false;
            }
            
            return true;
        }
        
        return false;
    }




    SOCKET m_clientSocket;
    sockaddr_in m_clientAddr;
    ServerMetrics& m_metrics;
    const ServerConfig& m_config;
    string m_clientIp;
    size_t m_bytesSent;
    size_t m_bytesReceived;
};


/**
 * Main server class
 */

class Server {
public:
    Server() : m_threadPool(thread::hardware_concurrency()), m_running(false) {
        // Set default log level
        Logger::getInstance().setLogLevel(Logger::INFO);
        
        // Create directories if they don't exist
        _mkdir(m_config.getDownloadDirectory().c_str());
        _mkdir(m_config.getUploadDirectory().c_str());
    }

    void loadConfig(const string& filename) {
        m_config.loadConfigFromFile(filename);
        Logger::getInstance().info("Configuration loaded from " + filename);
    }

    void saveConfig(const string& filename) {
        m_config.saveConfigToFile(filename);
        Logger::getInstance().info("Configuration saved to " + filename);
    }

    void start() {
        auto& logger = Logger::getInstance();
        
        if (m_running) {
            logger.warning("Server is already running");
            return;
        }
        

        try {
            WSADATA wsaData;
            SOCKET serverSocket = INVALID_SOCKET;
            struct sockaddr_in serverAddr;
            int opt = 1;
            
            // Initialize Winsock
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
                throw runtime_error("WSAStartup failed: " + to_string(WSAGetLastError()));
            }
            

            // Create socket
            serverSocket = socket(AF_INET, SOCK_STREAM, 0);
            if (serverSocket == INVALID_SOCKET) {
                WSACleanup();
                throw runtime_error("Socket creation failed: " + to_string(WSAGetLastError()));
            }
            
            // Set socket options
            if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt)) == SOCKET_ERROR) {
                closesocket(serverSocket);
                WSACleanup();
                throw runtime_error("setsockopt failed: " + to_string(WSAGetLastError()));
            }
            


            // Set up server address
            serverAddr.sin_family = AF_INET;
            serverAddr.sin_addr.s_addr = INADDR_ANY;
            serverAddr.sin_port = htons(m_config.getPort());
            

            // Bind socket
            if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
                closesocket(serverSocket);
                WSACleanup();
                throw runtime_error("Bind failed: " + to_string(WSAGetLastError()));
            }


            
            // Listen for connections
            if (listen(serverSocket, m_config.getBacklogSize()) == SOCKET_ERROR) {
                closesocket(serverSocket);
                WSACleanup();
                throw runtime_error("Listen failed: " + to_string(WSAGetLastError()));
            }
            


            logger.info("Server started on port " + to_string(m_config.getPort()) + 
                       " with thread pool size " + to_string(m_config.getThreadPoolSize()));
            
            m_running = true;
            


            // Start metrics display thread
            m_metricsThread = thread([this]() {
                while (m_running) {
                    this_thread::sleep_for(chrono::seconds(10));
                    if (m_running) { // Check again after sleep
                        m_metrics.displayMetrics();
                    }
                }
            });
            
            // Accept connections
            while (m_running) {
                sockaddr_in clientAddr;
                int addrLen = sizeof(clientAddr);
                
                SOCKET clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &addrLen);
                if (clientSocket == INVALID_SOCKET) {
                    if (!m_running) break; // Server was stopped
                    
                    logger.error("Accept failed: " + to_string(WSAGetLastError()));
                    continue;
                }
                

                // Set timeout
                DWORD timeout = m_config.getTimeout();
                if (setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) == SOCKET_ERROR) {
                    logger.warning("Failed to set receive timeout: " + to_string(WSAGetLastError()));
                }
                
                if (setsockopt(clientSocket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout)) == SOCKET_ERROR) {
                    logger.warning("Failed to set send timeout: " + to_string(WSAGetLastError()));
                }
                
                // Handle client in thread pool
                m_threadPool.enqueue([this, clientSocket, clientAddr]() {
                    ClientHandler handler(clientSocket, clientAddr, m_metrics, m_config);
                    handler.handleClient();
                });
            }
            



            // Cleanup
            closesocket(serverSocket);
            WSACleanup();
            
        } catch (const exception& ex) {
            logger.fatal("Server error: " + string(ex.what()));
            m_running = false;
        }
    }

    void stop() {
        if (!m_running) return;
        
        Logger::getInstance().info("Stopping server...");
        m_running = false;
        
        // Wait for metrics thread to finish
        if (m_metricsThread.joinable()) {
            m_metricsThread.join();
        }
        

        Logger::getInstance().info("Server stopped");
    }



    void runInteractive() {
        auto& logger = Logger::getInstance();
        logger.info("Interactive mode started. Type 'help' for commands.");
        
        // Start server in a separate thread
        thread serverThread([this]() {
            this->start();
        });
        
        string command;
        while (true) {
            cout << "\nServer> ";
            getline(cin, command);
            
            if (command == "exit") {
                stop();
                break;
            } else if (command == "help") {
                displayHelp();
            } else if (command == "stats") {
                m_metrics.displayMetrics();
            } else if (command == "config") {
                configureSettings();
            } else if (command == "save") {
                string filename;
                cout << "Config filename: ";
                getline(cin, filename);
                if (filename.empty()) filename = "server_config.ini";
                saveConfig(filename);
            } else if (command == "load") {
                string filename;
                cout << "Config filename: ";
                getline(cin, filename);
                if (filename.empty()) filename = "server_config.ini";
                loadConfig(filename);
            } else if (command == "start") {
                if (!m_running) {
                    cout << "Starting server..." << endl;
                    thread t([this]() { this->start(); });
                    t.detach();
                } else {
                    cout << "Server is already running" << endl;
                }
            } else if (command == "stop") {
                if (m_running) {
                    stop();
                } else {
                    cout << "Server is not running" << endl;
                }
            } else if (command == "log") {
                configureLogging();
            } else {
                cout << "Unknown command: " << command << endl;
            }
        }
        
        if (serverThread.joinable()) {
            serverThread.join();
        }
    }



private:
    void displayHelp() {
        cout << "\n===== Server Commands =====\n";
        cout << "help   - Display this help message\n";
        cout << "stats  - Display server statistics\n";
        cout << "config - Configure server settings\n";
        cout << "save   - Save configuration to file\n";
        cout << "load   - Load configuration from file\n";
        cout << "start  - Start the server if not running\n";
        cout << "stop   - Stop the server if running\n";
        cout << "log    - Configure logging settings\n";
        cout << "exit   - Exit the server application\n";
        cout << "==========================\n";
    }
    
    void configureSettings() {
        cout << "\n===== Server Configuration =====\n";
        cout << "1. Port: " << m_config.getPort() << endl;
        cout << "2. Max Connections: " << m_config.getMaxConnections() << endl;
        cout << "3. Timeout (ms): " << m_config.getTimeout() << endl;
        cout << "4. Backlog Size: " << m_config.getBacklogSize() << endl;
        cout << "5. Thread Pool Size: " << m_config.getThreadPoolSize() << endl;
        cout << "6. Download Directory: " << m_config.getDownloadDirectory() << endl;
        cout << "7. Upload Directory: " << m_config.getUploadDirectory() << endl;
        cout << "0. Back\n";
        
        int choice;
        cout << "Enter choice (0-7): ";
        cin >> choice;
        cin.ignore();
        
        string value;
        switch (choice) {
            case 1:
                cout << "Enter port: ";
                getline(cin, value);
                if (!value.empty()) m_config.setPort(stoi(value));
                break;
            case 2:
                cout << "Enter max connections: ";
                getline(cin, value);
                if (!value.empty()) m_config.setMaxConnections(stoi(value));
                break;
            case 3:
                cout << "Enter timeout (ms): ";
                getline(cin, value);
                if (!value.empty()) m_config.setTimeout(stoi(value));
                break;
            case 4:
                cout << "Enter backlog size: ";
                getline(cin, value);
                if (!value.empty()) m_config.setBacklogSize(stoi(value));
                break;
            case 5:
                cout << "Enter thread pool size: ";
                getline(cin, value);
                if (!value.empty()) m_config.setThreadPoolSize(stoi(value));
                break;
            case 6:
                cout << "Enter download directory: ";
                getline(cin, value);
                if (!value.empty()) m_config.setDownloadDirectory(value);
                break;
            case 7:
                cout << "Enter upload directory: ";
                getline(cin, value);
                if (!value.empty()) m_config.setUploadDirectory(value);
                break;
            case 0:
            default:
                break;
        }
    }
    
    void configureLogging() {
        cout << "\n===== Logging Configuration =====\n";
        cout << "1. Set Log Level\n";
        cout << "2. Set Log File\n";
        cout << "0. Back\n";
        
        int choice;
        cout << "Enter choice (0-2): ";
        cin >> choice;
        cin.ignore();
        
        string value;
        switch (choice) {
            case 1:
                cout << "Enter log level (0=DEBUG, 1=INFO, 2=WARNING, 3=ERROR, 4=FATAL): ";
                getline(cin, value);
                if (!value.empty()) {
                    int level = stoi(value);
                    Logger::getInstance().setLogLevel(static_cast<Logger::LogLevel>(level));
                }
                break;
            case 2:
                cout << "Enter log file path (leave empty to disable file logging): ";
                getline(cin, value);
                if (!value.empty()) {
                    Logger::getInstance().setLogToFile(value);
                }
                break;
            case 0:
            default:
                break;
        }
    }
    

    ServerConfig m_config;
    ServerMetrics m_metrics;
    ThreadPool m_threadPool;
    bool m_running;
    thread m_metricsThread;
};

/**
 * Main function
 */
int main(int argc, char* argv[]) {
    try {
        Server server;
        
        // Check for config file in arguments
        if (argc > 1) {
            server.loadConfig(argv[1]);
        } else {
            // Try to load default config
            try {
                server.loadConfig("server_config.ini");
            } catch (const exception& ex) {
                cout << "Using default configuration: " << ex.what() << endl;
            }
        }
        
        // Run interactive mode
        server.runInteractive();
        
    } catch (const exception& ex) {
        cerr << "Fatal error: " << ex.what() << endl;
        return 1;
    }
    
    return 0;
}