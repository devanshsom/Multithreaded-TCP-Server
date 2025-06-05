#include <crow.h>
#include <uwebsockets/App.h>
#include <json/json.h>
#include <unordered_map>
#include <vector>
#include <iostream>
#include <memory>
#include <mutex>


struct User {
    std::string id;
    std::string username;
    std::string room;
};

std::unordered_map<std::string, std::shared_ptr<User>> users;
std::unordered_map<std::string, std::vector<std::shared_ptr<User>>> rooms;
std::mutex user_mutex;

std::shared_ptr<User> userJoin(const std::string& id, const std::string& username, const std::string& room) {
    auto user = std::make_shared<User>(User{id, username, room});
    {
        std::lock_guard<std::mutex> lock(user_mutex);
        users[id] = user;
        rooms[room].push_back(user);
    }
    return user;
}

std::shared_ptr<User> getCurrentUser(const std::string& id) {
    std::lock_guard<std::mutex> lock(user_mutex);
    return users.count(id) ? users[id] : nullptr;
}

void userLeaves(const std::string& id) {
    std::lock_guard<std::mutex> lock(user_mutex);
    if (users.count(id)) {
        auto user = users[id];
        auto& room_users = rooms[user->room];
        room_users.erase(std::remove_if(room_users.begin(), room_users.end(), [&](std::shared_ptr<User> u) {
            return u->id == id;
        }), room_users.end());
        users.erase(id);
    }
}

std::vector<std::shared_ptr<User>> getRoomUsers(const std::string& room) {
    std::lock_guard<std::mutex> lock(user_mutex);
    return rooms.count(room) ? rooms[room] : std::vector<std::shared_ptr<User>>();
}

std::string formatMessage(const std::string& sender, const std::string& text) {
    Json::Value message;
    message["username"] = sender;
    message["text"] = text;
    Json::StreamWriterBuilder writer;
    return Json::writeString(writer, message);
}

int main() {
    crow::SimpleApp app;

    // Serve static files from the "Frontend" directory
    CROW_ROUTE(app, "/<string>")
    ([](const crow::request&, crow::response& res, std::string filename){
        res.set_static_file_info("Frontend/" + filename);
        res.end();
    });

    // Setup WebSocket server
    uWS::App().ws<UserData>("/*", {
        .open = [](auto* ws) {
            std::cout << "Connected!" << std::endl;
        },
        .message = [](auto* ws, std::string_view message, uWS::OpCode opCode) {
            // Parse the message
            Json::CharReaderBuilder reader;
            Json::Value jsonMessage;
            std::string errors;

            if (Json::parseFromStream(reader, message, &jsonMessage, &errors)) {
                auto type = jsonMessage["type"].asString();
                if (type == "joinRoom") {
                    auto user = userJoin(ws->getUserData()->id, jsonMessage["username"].asString(), jsonMessage["room"].asString());
                    ws->subscribe(user->room);
                    ws->send(formatMessage("PingIO Bot", "Welcome to PingIO!"), uWS::OpCode::TEXT);
                    ws->publish(user->room, formatMessage("PingIO Bot", user->username + " has joined the chat!"), uWS::OpCode::TEXT);
                } else if (type == "chat-message") {
                    auto user = getCurrentUser(ws->getUserData()->id);
                    if (user) {
                        ws->publish(user->room, formatMessage(user->username, jsonMessage["text"].asString()), uWS::OpCode::TEXT);
                    }
                }
            }
        },
        .close = [](auto* ws, int, std::string_view) {
            auto user = getCurrentUser(ws->getUserData()->id);
            if (user) {
                userLeaves(ws->getUserData()->id);
                ws->publish(user->room, formatMessage("PingIO Bot", user->username + " has left the chat!"), uWS::OpCode::TEXT);
            }
        }
    }).listen(3000, [](auto* token) {
        if (token) {
            std::cout << "PingIO listening on port 3000" << std::endl;
        } else {
            std::cout << "Failed to listen on port 3000" << std::endl;
        }
    }).run();

    return 0;
}
