#include "httplib.h"
#include <iostream>
#include <string>
#include <vector>
#include <mutex>
#include <fstream>
#include <algorithm>
#include <random>
#include <chrono>
#include <ctime>
#include <iomanip>
#include "nlohmann/json.hpp"

// For convenience
using json = nlohmann::json;

// Global mutex for thread-safe file operations
std::mutex g_file_mutex;

// Data file path
const std::string DATA_FILE = "bin_data.json";

// WasteBin class
class WasteBin {
public:
    int id;
    std::string location;
    int fillLevel;
    bool needsCollection;
    std::string lastUpdated;

    // Default constructor
    WasteBin() : id(0), location(""), fillLevel(0), needsCollection(false) {
        lastUpdated = getCurrentTimestamp();
    }

    // Constructor with fields
    WasteBin(int id, const std::string& location, int fillLevel = 0, bool needsCollection = false)
        : id(id), location(location), fillLevel(fillLevel), needsCollection(needsCollection) {
        lastUpdated = getCurrentTimestamp();
    }

    // Convert to JSON
    json toJson() const {
        return {
            {"id", id},
            {"location", location},
            {"fillLevel", fillLevel},
            {"needsCollection", needsCollection},
            {"lastUpdated", lastUpdated}
        };
    }

    // Create from JSON
    static WasteBin fromJson(const json& j) {
        WasteBin bin;
        bin.id = j.at("id").get<int>();
        bin.location = j.at("location").get<std::string>();
        bin.fillLevel = j.at("fillLevel").get<int>();
        bin.needsCollection = j.at("needsCollection").get<bool>();
        bin.lastUpdated = j.at("lastUpdated").get<std::string>();
        return bin;
    }

private:
    // Get current time as ISO string
    static std::string getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now);
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::stringstream ss;
        ss << std::put_time(std::gmtime(&now_c), "%Y-%m-%dT%H:%M:%S");
        ss << '.' << std::setfill('0') << std::setw(3) << now_ms.count();
        ss << "Z";
        return ss.str();
    }
};

// Global variables
std::vector<WasteBin> g_bins;
int g_next_bin_id = 1;

// Helper: Create standard API response JSON
json createApiResponse(bool success, const std::string& message, const json& data = nullptr) {
    json response = {
        {"success", success},
        {"message", message}
    };

    if (!data.is_null()) {
        response["data"] = data;
    }

    return response;
}

// Helper: Load data from file
void loadBinsFromFile() {
    std::lock_guard<std::mutex> lock(g_file_mutex);

    try {
        std::ifstream file(DATA_FILE);
        if (!file.is_open()) {
            g_bins.clear();
            g_next_bin_id = 1;
            return;
        }

        json data = json::parse(file);
        g_bins.clear();

        for (const auto& item : data) {
            g_bins.push_back(WasteBin::fromJson(item));
        }

        // Update next_bin_id to avoid ID collisions
        if (!g_bins.empty()) {
            g_next_bin_id = std::max_element(g_bins.begin(), g_bins.end(),
                [](const WasteBin& a, const WasteBin& b) { return a.id < b.id; })->id + 1;
        } else {
            g_next_bin_id = 1;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error loading data: " << e.what() << std::endl;
        g_bins.clear();
        g_next_bin_id = 1;
    }
}

// Helper: Save data to file
void saveBinsToFile() {
    std::lock_guard<std::mutex> lock(g_file_mutex);

    try {
        json data = json::array();
        for (const auto& bin : g_bins) {
            data.push_back(bin.toJson());
        }

        std::ofstream file(DATA_FILE);
        file << data.dump(4);
    }
    catch (const std::exception& e) {
        std::cerr << "Error saving data: " << e.what() << std::endl;
    }
}

int main() {
    // Load data on startup
    loadBinsFromFile();

    // Create server
    httplib::Server svr;

    // Welcome page
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(
            "<html>"
            "<head><title>Smart Waste Management API</title>"
            "<style>"
            "body { font-family: Arial, sans-serif; margin: 40px; line-height: 1.6; }"
            "h1 { color: #2c3e50; }"
            "h2 { color: #3498db; }"
            "code { background: #f4f4f4; padding: 2px 5px; border-radius: 3px; }"
            "ul { list-style-type: none; padding-left: 20px; }"
            "li { margin-bottom: 10px; }"
            "a { color: #3498db; text-decoration: none; }"
            "a:hover { text-decoration: underline; }"
            "</style></head>"
            "<body>"
            "<h1>Smart Waste Management System API</h1>"
            "<p>Version 1.0.0</p>"
            "<h2>Available Endpoints:</h2>"
            "<ul>"
            "<li><code>GET /bins</code> - List all waste bins</li>"
            "<li><code>GET /bins/{id}</code> - Get a specific bin by ID</li>"
            "<li><code>POST /bins</code> - Add new waste bins</li>"
            "<li><code>PUT /bins/{id}</code> - Update a bin's properties</li>"
            "<li><code>DELETE /bins/{id}</code> - Delete a waste bin</li>"
            "<li><code>POST /bins/collect-sensor-data</code> - Simulate sensor data collection</li>"
            "<li><code>GET /optimize-route</code> - Get optimized collection route</li>"
            "<li><code>GET /dashboard/stats</code> - Get dashboard statistics</li>"
            "<li><code>GET /health</code> - API health check</li>"
            "</ul>"
            "</body></html>",
            "text/html");
    });

    // Add new bins
    svr.Post("/bins", [](const httplib::Request& req, httplib::Response& res) {
        try {
            // Parse request body
            json requestData = json::parse(req.body);

            if (!requestData.is_array()) {
                json binData = requestData;  // Single bin case
                requestData = json::array();
                requestData.push_back(binData);
            }

            std::vector<WasteBin> created;
            for (const auto& binData : requestData) {
                // Get location from request
                if (!binData.contains("location") || !binData["location"].is_string()) {
                    res.status = 400;
                    res.set_content(createApiResponse(false, "Each bin must have a location string").dump(), "application/json");
                    return;
                }

                std::string location = binData["location"].get<std::string>();

                // Create new bin
                WasteBin newBin(g_next_bin_id++, location);
                g_bins.push_back(newBin);
                created.push_back(newBin);
            }

            // Save to file
            saveBinsToFile();

            // Convert created bins to JSON array
            json createdJson = json::array();
            for (const auto& bin : created) {
                createdJson.push_back(bin.toJson());
            }

            // Return success response
            res.status = 201;
            res.set_content(
                createApiResponse(true, std::to_string(created.size()) + " bins added successfully", createdJson).dump(),
                "application/json"
            );
        }
        catch (const std::exception& e) {
            res.status = 400;
            res.set_content(
                createApiResponse(false, std::string("Error: ") + e.what()).dump(),
                "application/json"
            );
        }
    });

    // Get all bins
    svr.Get("/bins", [](const httplib::Request&, httplib::Response& res) {
        if (g_bins.empty()) {
            res.set_content(
                createApiResponse(true, "No bins available", json::array()).dump(),
                "application/json"
            );
            return;
        }

        json binsJson = json::array();
        for (const auto& bin : g_bins) {
            binsJson.push_back(bin.toJson());
        }

        res.set_content(
            createApiResponse(true, "Retrieved " + std::to_string(g_bins.size()) + " bins", binsJson).dump(),
            "application/json"
        );
    });

    // Get bin by ID
    svr.Get(R"(/bins/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
        int binId = std::stoi(req.matches[1]);

        for (const auto& bin : g_bins) {
            if (bin.id == binId) {
                res.set_content(
                    createApiResponse(true, "Retrieved bin with ID " + std::to_string(binId), bin.toJson()).dump(),
                    "application/json"
                );
                return;
            }
        }

        res.status = 404;
        res.set_content(
            createApiResponse(false, "Bin with ID " + std::to_string(binId) + " not found").dump(),
            "application/json"
        );
    });

    // Delete bin by ID
    svr.Delete(R"(/bins/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
        int binId = std::stoi(req.matches[1]);

        auto it = std::find_if(g_bins.begin(), g_bins.end(), [binId](const WasteBin& bin) {
            return bin.id == binId;
        });

        if (it != g_bins.end()) {
            g_bins.erase(it);
            saveBinsToFile();

            res.set_content(
                createApiResponse(true, "Bin with ID " + std::to_string(binId) + " deleted successfully").dump(),
                "application/json"
            );
            return;
        }

        res.status = 404;
        res.set_content(
            createApiResponse(false, "Bin with ID " + std::to_string(binId) + " not found").dump(),
            "application/json"
        );
    });

    // Update bin by ID
    svr.Put(R"(/bins/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
        try {
            int binId = std::stoi(req.matches[1]);
            json updateData = json::parse(req.body);

            auto it = std::find_if(g_bins.begin(), g_bins.end(), [binId](const WasteBin& bin) {
                return bin.id == binId;
            });

            if (it != g_bins.end()) {
                // Update only provided fields
                if (updateData.contains("location") && updateData["location"].is_string()) {
                    it->location = updateData["location"].get<std::string>();
                }

                if (updateData.contains("fillLevel") && updateData["fillLevel"].is_number()) {
                    it->fillLevel = std::max(0, std::min(100, updateData["fillLevel"].get<int>()));
                }

                if (updateData.contains("needsCollection") && updateData["needsCollection"].is_boolean()) {
                    it->needsCollection = updateData["needsCollection"].get<bool>();
                }

                // Always update timestamp
                it->lastUpdated = WasteBin().lastUpdated;  // Use default constructor to get current time

                saveBinsToFile();

                res.set_content(
                    createApiResponse(true, "Bin with ID " + std::to_string(binId) + " updated successfully", it->toJson()).dump(),
                    "application/json"
                );
                return;
            }

            res.status = 404;
            res.set_content(
                createApiResponse(false, "Bin with ID " + std::to_string(binId) + " not found").dump(),
                "application/json"
            );
        }
        catch (const std::exception& e) {
            res.status = 400;
            res.set_content(
                createApiResponse(false, std::string("Error: ") + e.what()).dump(),
                "application/json"
            );
        }
    });

    // Collect sensor data
    svr.Post("/bins/collect-sensor-data", [](const httplib::Request&, httplib::Response& res) {
        if (g_bins.empty()) {
            res.status = 404;
            res.set_content(
                createApiResponse(false, "No bins available").dump(),
                "application/json"
            );
            return;
        }

        // Random number generator
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> distrib(0, 100);

        json updatedBins = json::array();

        for (auto& bin : g_bins) {
            bin.fillLevel = distrib(gen);
            bin.needsCollection = bin.fillLevel >= 75;
            bin.lastUpdated = WasteBin().lastUpdated;  // Current timestamp
            updatedBins.push_back(bin.toJson());
        }

        saveBinsToFile();

        res.set_content(
            createApiResponse(true, "Sensor data collected and updated", updatedBins).dump(),
            "application/json"
        );
    });

    // Optimize collection route
    svr.Get("/optimize-route", [](const httplib::Request&, httplib::Response& res) {
        std::vector<WasteBin> toCollect;

        for (const auto& bin : g_bins) {
            if (bin.needsCollection) {
                toCollect.push_back(bin);
            }
        }

        if (toCollect.empty()) {
            res.set_content(
                createApiResponse(true, "No bins need collection right now", json::array()).dump(),
                "application/json"
            );
            return;
        }

        // Sort bins by fill level (highest first)
        std::sort(toCollect.begin(), toCollect.end(), [](const WasteBin& a, const WasteBin& b) {
            return a.fillLevel > b.fillLevel;
        });

        // Prepare route data
        json routeJson = json::array();
        for (const auto& bin : toCollect) {
            routeJson.push_back({
                {"id", bin.id},
                {"location", bin.location},
                {"fillLevel", bin.fillLevel},
                {"lastUpdated", bin.lastUpdated}
            });
        }

        json responseData = {
            {"binsToCollect", toCollect.size()},
            {"route", routeJson}
        };

        res.set_content(
            createApiResponse(true, "Found " + std::to_string(toCollect.size()) + " bins needing collection", responseData).dump(),
            "application/json"
        );
    });

    // Dashboard statistics
    svr.Get("/dashboard/stats", [](const httplib::Request&, httplib::Response& res) {
        if (g_bins.empty()) {
            json emptyStats = {
                {"totalBins", 0},
                {"binsNeedingCollection", 0},
                {"averageFillLevel", 0.0},
                {"fillLevelDistribution", {
                    {"low", 0},
                    {"medium", 0},
                    {"high", 0},
                    {"critical", 0}
                }}
            };

            res.set_content(
                createApiResponse(true, "No bins available", emptyStats).dump(),
                "application/json"
            );
            return;
        }

        // Calculate statistics
        int totalBins = g_bins.size();
        int binsNeedingCollection = 0;
        int totalFill = 0;

        // Fill level distribution
        int lowCount = 0;
        int mediumCount = 0;
        int highCount = 0;
        int criticalCount = 0;

        for (const auto& bin : g_bins) {
            totalFill += bin.fillLevel;
            if (bin.needsCollection) binsNeedingCollection++;

            if (bin.fillLevel < 25) lowCount++;
            else if (bin.fillLevel < 50) mediumCount++;
            else if (bin.fillLevel < 75) highCount++;
            else criticalCount++;
        }

        double averageFill = totalBins > 0 ? static_cast<double>(totalFill) / totalBins : 0.0;

        json stats = {
            {"totalBins", totalBins},
            {"binsNeedingCollection", binsNeedingCollection},
            {"averageFillLevel", round(averageFill * 10) / 10.0},  // Round to 1 decimal place
            {"fillLevelDistribution", {
                {"low", lowCount},
                {"medium", mediumCount},
                {"high", highCount},
                {"critical", criticalCount}
            }}
        };

        res.set_content(
            createApiResponse(true, "Dashboard statistics retrieved successfully", stats).dump(),
            "application/json"
        );
    });

    // Admin: Load data from file
    svr.Post("/admin/load-data", [](const httplib::Request&, httplib::Response& res) {
        loadBinsFromFile();

        res.set_content(
            createApiResponse(true, "Successfully loaded " + std::to_string(g_bins.size()) + " bins from file").dump(),
            "application/json"
        );
    });

    // Admin: Save data to file
    svr.Post("/admin/save-data", [](const httplib::Request&, httplib::Response& res) {
        saveBinsToFile();

        res.set_content(
            createApiResponse(true, "Successfully saved " + std::to_string(g_bins.size()) + " bins to file").dump(),
            "application/json"
        );
    });

    // Health check
    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        WasteBin tmp;  // Just to get current timestamp

        json health = {
            {"status", "ok"},
            {"timestamp", tmp.lastUpdated},
            {"version", "1.0.0"}
        };

        res.set_content(health.dump(), "application/json");
    });

    // Set CORS headers for all responses
    svr.set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS"},
        {"Access-Control-Allow-Headers", "Content-Type, Authorization"},
        {"Access-Control-Max-Age", "86400"}
    });

    // Handle OPTIONS requests for CORS preflight
    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("", "text/plain");
    });

    std::cout << "Smart Waste Management API server started on http://0.0.0.0:8080" << std::endl;
    svr.listen("0.0.0.0", 8080);

    return 0;
}
