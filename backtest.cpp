#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <ctime>
#include <iomanip>
#include <algorithm>
#include <map>

// Structure to hold OHLC data
struct OHLC {
    std::string timestamp;
    double open, high, low, close;
    std::string expiry;
    double strike; // For option data
};

// Structure for tradelog entries
struct TradeLog {
    std::string key;
    std::string exitTime;
    std::string symbol;
    double entryPrice;
    double exitPrice;
    int quantity;
    std::string positionStatus;
    double pnl;
    std::string exitType;
};

// Function to parse date-time string to time_t
time_t parseDateTime(const std::string& dt) {
    std::tm t = {};
    std::istringstream ss(dt);
    ss >> std::get_time(&t, "%d-%m-%Y %H:%M");
    return std::mktime(&t);
}

// Function to parse date string to time_t
time_t parseDate(const std::string& date) {
    std::tm t = {};
    std::istringstream ss(date);
    ss >> std::get_time(&t, "%d-%m-%Y");
    return std::mktime(&t);
}

// Function to format time_t to string
std::string formatDateTime(time_t t) {
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// Function to format date for symbol
std::string formatSymbolDate(time_t t) {
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&t), "%d%b%y");
    std::string result = oss.str();
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    return result;
}

// Function to read CSV file
std::vector<OHLC> readCSV(const std::string& filename, bool isOption) {
    std::vector<OHLC> data;
    std::ifstream file(filename);
    std::string line;
    
    // Skip header
    std::getline(file, line);
    
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string token;
        OHLC record;
        
        std::getline(ss, token, ','); // timestamp
        record.timestamp = token;
        std::getline(ss, token, ','); // open
        record.open = std::stod(token);
        std::getline(ss, token, ','); // high
        record.high = std::stod(token);
        std::getline(ss, token, ','); // low
        record.low = std::stod(token);
        std::getline(ss, token, ','); // close
        record.close = std::stod(token);
        std::getline(ss, token, ','); // expiry
        record.expiry = token;
        
        if (isOption) {
            std::getline(ss, token, ','); // strike
            record.strike = std::stod(token);
        }
        
        data.push_back(record);
    }
    
    return data;
}

// Function to write tradelog to CSV
void writeTradeLog(const std::vector<TradeLog>& tradelog, const std::string& filename) {
    std::ofstream file(filename);
    file << "Key,ExitTime,Symbol,EntryPrice,ExitPrice,Quantity,PositionStatus,Pnl,ExitType\n";
    
    for (const auto& trade : tradelog) {
        file << trade.key << "," 
             << trade.exitTime << "," 
             << trade.symbol << "," 
             << trade.entryPrice << "," 
             << trade.exitPrice << "," 
             << trade.quantity << "," 
             << trade.positionStatus << "," 
             << trade.pnl << "," 
             << trade.exitType << "\n";
    }
}

// Backtesting function
std::vector<TradeLog> backtestStrategy(const std::vector<OHLC>& niftyData, 
                                     const std::vector<OHLC>& ceData, 
                                     const std::vector<OHLC>& peData,
                                     const std::string& startDate,
                                     const std::string& endDate) {
    std::vector<TradeLog> tradelog;
    time_t start = parseDate(startDate);
    time_t end = parseDate(endDate);
    
    // Create map for faster lookup
    std::map<std::string, OHLC> niftyMap;
    std::map<std::string, std::vector<OHLC>> ceMap;
    std::map<std::string, std::vector<OHLC>> peMap;
    
    for (const auto& n : niftyData) {
        niftyMap[n.timestamp] = n;
    }
    for (const auto& c : ceData) {
        ceMap[c.timestamp].push_back(c);
    }
    for (const auto& p : peData) {
        peMap[p.timestamp].push_back(p);
    }
    
    // Get unique dates
    std::vector<time_t> uniqueDates;
    for (const auto& n : niftyData) {
        time_t t = parseDateTime(n.timestamp);
        std::tm* tm = std::localtime(&t);
        tm->tm_hour = 0; tm->tm_min = 0; tm->tm_sec = 0;
        time_t dayStart = std::mktime(tm);
        if (dayStart >= start && dayStart <= end) {
            if (std::find(uniqueDates.begin(), uniqueDates.end(), dayStart) == uniqueDates.end()) {
                uniqueDates.push_back(dayStart);
            }
        }
    }
    
    for (time_t day : uniqueDates) {
        std::tm* tm = std::localtime(&day);
        std::ostringstream entryTimeSS, exitTimeSS;
        entryTimeSS << std::put_time(tm, "%Y-%m-%d") << " 13:00:00";
        exitTimeSS << std::put_time(tm, "%Y-%m-%d") << " 15:00:00";
        std::string entryTimeStr = entryTimeSS.str();
        std::string exitTimeStr = exitTimeSS.str();
        
        if (niftyMap.find(entryTimeStr) == niftyMap.end()) continue;
        
        double niftySpot = niftyMap[entryTimeStr].close;
        double atmStrike = std::round(niftySpot / 100.0) * 100.0;
        double ceStrike = atmStrike + 100.0;
        double peStrike = atmStrike - 100.0;
        std::string expiry = niftyMap[entryTimeStr].expiry;
        
        // Find option data
        double ceEntryPrice = 0.0, peEntryPrice = 0.0;
        bool ceFound = false, peFound = false;
        
        for (const auto& c : ceMap[entryTimeStr]) {
            if (c.strike == ceStrike && c.expiry == expiry) {
                ceEntryPrice = c.close;
                ceFound = true;
                break;
            }
        }
        for (const auto& p : peMap[entryTimeStr]) {
            if (p.strike == peStrike && p.expiry == expiry) {
                peEntryPrice = p.close;
                peFound = true;
                break;
            }
        }
        
        if (!ceFound || !peFound) continue;
        
        double ceSL = ceEntryPrice * 1.30;
        double peSL = peEntryPrice * 1.30;
        bool ceSLHit = false, peSLHit = false;
        double ceExitPrice = 0.0, peExitPrice = 0.0;
        std::string ceExitTime = exitTimeStr, peExitTime = exitTimeStr;
        std::string ceExitType = "Exit @ 3pm", peExitType = "Exit @ 3pm";
        
        // Check stop-loss minute by minute
        time_t entryT = parseDateTime(entryTimeStr);
        time_t exitT = parseDateTime(exitTimeStr);
        
        for (time_t t = entryT; t <= exitT; t += 60) { // Increment by 1 minute
            std::string currentTime = formatDateTime(t);
            
            if (!ceSLHit && ceMap.find(currentTime) != ceMap.end()) {
                for (const auto& c : ceMap[currentTime]) {
                    if (c.strike == ceStrike && c.expiry == expiry && c.high >= ceSL) {
                        ceSLHit = true;
                        ceExitPrice = ceSL;
                        ceExitTime = currentTime;
                        ceExitType = "stoploss hit";
                        peSL = peEntryPrice; // Move other leg's SL to cost
                        break;
                    }
                }
            }
            
            if (!peSLHit && peMap.find(currentTime) != peMap.end()) {
                for (const auto& p : peMap[currentTime]) {
                    if (p.strike == peStrike && p.expiry == expiry && p.high >= peSL) {
                        peSLHit = true;
                        peExitPrice = peSL;
                        peExitTime = currentTime;
                        peExitType = "stoploss hit";
                        if (!ceSLHit) ceSL = ceEntryPrice;
                        break;
                    }
                }
            }
        }
        
        // If no SL hit, set exit prices to 3pm prices
        if (!ceSLHit) {
            for (const auto& c : ceMap[exitTimeStr]) {
                if (c.strike == ceStrike && c.expiry == expiry) {
                    ceExitPrice = c.close;
                    break;
                }
            }
        }
        if (!peSLHit) {
            for (const auto& p : peMap[exitTimeStr]) {
                if (p.strike == peStrike && p.expiry == expiry) {
                    peExitPrice = p.close;
                    break;
                }
            }
        }
        
        // Calculate PnL
        double cePnl = (ceEntryPrice - ceExitPrice) * 25;
        double pePnl = (peEntryPrice - peExitPrice) * 25;
        
        // Add to tradelog
        tradelog.push_back({
            entryTimeStr, ceExitTime, 
            "NIFTY" + formatSymbolDate(parseDate(expiry)) + std::to_string(static_cast<int>(ceStrike)) + "CE",
            ceEntryPrice, ceExitPrice, -25, "Closed", cePnl, ceExitType
        });
        tradelog.push_back({
            entryTimeStr, peExitTime,
            "NIFTY" + formatSymbolDate(parseDate(expiry)) + std::to_string(static_cast<int>(peStrike)) + "PE",
            peEntryPrice, peExitPrice, -25, "Closed", pePnl, peExitType
        });
    }
    
    return tradelog;
}

int main() {
    // Load data
    std::vector<OHLC> niftyData = readCSV("Nifty_spot_data_min_2024.csv", false);
    std::vector<OHLC> ceData = readCSV("Call-Option-Data-1min.csv", true);
    std::vector<OHLC> peData = readCSV("Put-Option-Data-1min.csv", true);
    
    // Run backtest
    std::vector<TradeLog> tradelog = backtestStrategy(niftyData, ceData, peData, "2024-11-04", "2024-12-31");
    
    // Save tradelog to tradelogCPP.csv
    writeTradeLog(tradelog, "tradelogCPP.csv");
    
    // Print detailed output of first few trades
    std::cout << "\n--- Backtest Tradlog (First 5 Trades) ---\n";
    std::cout << std::fixed << std::setprecision(2);
    for (size_t i = 0; i < std::min<size_t>(5, tradelog.size()); ++i) {
        const auto& trade = tradelog[i];
        std::cout << "Trade " << (i + 1) << ":\n"
                  << "  Key: " << trade.key << "\n"
                  << "  Symbol: " << trade.symbol << "\n"
                  << "  Entry Price: " << trade.entryPrice << "\n"
                  << "  Exit Price: " << trade.exitPrice << "\n"
                  << "  Quantity: " << trade.quantity << "\n"
                  << "  PnL: " << trade.pnl << "\n"
                  << "  Exit Type: " << trade.exitType << "\n"
                  << "  Exit Time: " << trade.exitTime << "\n\n";
    }
    
    // Summary statistics
    double totalPnl = 0.0;
    size_t totalTrades = tradelog.size();
    size_t slHitCount = 0;
    for (const auto& trade : tradelog) {
        totalPnl += trade.pnl;
        if (trade.exitType == "stoploss hit") {
            slHitCount++;
        }
    }
    std::cout << "--- Summary ---\n"
              << "Total Trades: " << totalTrades << "\n"
              << "Total PnL: " << totalPnl << "\n"
              << "Stop-Loss Hit Count: " << slHitCount << "\n"
              << "Tradelog saved to: tradelogCPP.csv\n";
    
    return 0;
}