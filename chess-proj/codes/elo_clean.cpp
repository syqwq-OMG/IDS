#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <algorithm>

#include "chess.hpp"
#include "json.hpp"

namespace fs = std::filesystem;

// ================= 配置区域 =================
const std::string PGN_FILE_PATH = "../dataset/lichess_db_standard_rated_2025-11.pgn";
// const std::string PGN_FILE_PATH = "../dataset/lichess_db_standard_rated_2017-02.pgn";
const std::string OUT_DIR = "../output/chess_transformer";
// 标记为 strict，表示最严格的清洗模式
const std::string OUTPUT_FILE = OUT_DIR + "/dataset_seq_strict.csv";

const int MAX_PLY = 40; 
// ===========================================

int safe_stoi(std::string_view sv) {
    try {
        std::string s(sv);
        if (s == "?" || s.empty()) return 0;
        return std::stoi(s);
    } catch (...) {
        return 0;
    }
}

// === 核心逻辑：严格归一化 (Strict Normalization) ===
// 目标：将所有移动归约为 [Piece][Dest][Promotion]
// 例如: "hxg8=N+" -> "g8=N"
//       "Rad1"    -> "Rd1"
//       "O-O+"    -> "O-O"
std::string strict_simplify_move(std::string_view sv) {
    std::string raw(sv);
    
    // 1. 处理王车易位 (Castling)
    // 易位也是一种特殊的 "Token"，我们只保留 O-O 或 O-O-O，去掉可能的 + 或 #
    if (raw.rfind("O-O", 0) == 0) { // Starts with O-O
        if (raw.find("O-O-O") == 0) return "O-O-O";
        return "O-O";
    }

    // 2. 寻找目标格 (Destination Square)
    // 逻辑：倒序查找第一个数字 (Rank 1-8)。该数字及其前一个字符即为目标格。
    int rank_idx = -1;
    for (int i = raw.size() - 1; i >= 0; --i) {
        if (isdigit(raw[i])) {
            rank_idx = i;
            break;
        }
    }

    // 异常防御
    if (rank_idx == -1 || rank_idx == 0) return ""; 

    char rank = raw[rank_idx];       // e.g., '1'
    char file = raw[rank_idx - 1];   // e.g., 'd'
    
    // 构建目标格字符串
    std::string dest;
    dest += file;
    dest += rank;

    // 3. 确定棋子类型 (Piece)
    char first = raw[0];
    std::string piece;
    
    // 如果首字母是大写 (N, B, R, Q, K)，则保留
    // 注意：Pawn 在 SAN 中没有 P 前缀，首字母是 a-h，所以这里只有大写才加前缀
    if (isupper(first)) {
        piece += first;
    }
    // 如果是兵 (小写开头)，piece 为空字符串，只保留坐标 (如 e4)

    // 4. 处理升变 (Promotion)
    // 只有当存在 '=' 时才提取随后的一个字母
    std::string promotion;
    size_t eq_pos = raw.find('=');
    if (eq_pos != std::string::npos && eq_pos + 1 < raw.size()) {
        promotion += '=';
        promotion += raw[eq_pos + 1]; // e.g., Q, N
    }

    // 5. 组合
    // 结果只有: Piece + Dest + Promotion
    // 彻底丢弃了: Capture(x), Check(+), Mate(#), Disambiguation(a/1)
    return piece + dest + promotion;
}

class TransformerDataVisitor : public chess::pgn::Visitor {
private:
    std::ofstream& out_stream;
    
    int white_elo = 0;
    int black_elo = 0;
    std::vector<std::string> move_sequence;
    
    long long games_processed = 0;
    long long games_saved = 0;

public:
    TransformerDataVisitor(std::ofstream& os) : out_stream(os) {
        move_sequence.reserve(MAX_PLY);
    }

    void startPgn() override {
        white_elo = 0;
        black_elo = 0;
        move_sequence.clear();
    }

    void header(std::string_view key, std::string_view value) override {
        if (key == "WhiteElo") {
            white_elo = safe_stoi(value);
        } else if (key == "BlackElo") {
            black_elo = safe_stoi(value);
        }
    }

    void startMoves() override {}

    void move(std::string_view move_str, std::string_view comment) override {
        (void)comment;
        if (move_sequence.size() < MAX_PLY) {
            // 使用最严格的清洗
            std::string s = strict_simplify_move(move_str);
            if (!s.empty()) {
                move_sequence.emplace_back(s);
            }
        }
    }

    void endPgn() override {
        games_processed++;

        if (white_elo > 0 && black_elo > 0 && !move_sequence.empty()) {
            
            out_stream << white_elo << "," << black_elo << ",";
            
            for (size_t i = 0; i < move_sequence.size(); ++i) {
                out_stream << move_sequence[i];
                if (i < move_sequence.size() - 1) {
                    out_stream << " ";
                }
            }
            out_stream << "\n";
            games_saved++;
        }

        if (games_processed % 50000 == 0) {
            std::cout << "Processed: " << games_processed 
                      << " | Saved: " << games_saved 
                      << " | Ratio: " << std::fixed << std::setprecision(1) 
                      << (double)games_saved / games_processed * 100.0 << "%" 
                      << "\r" << std::flush;
        }
    }

    void print_summary() {
        std::cout << "\n\n=== Extraction Complete ===" << std::endl;
        std::cout << "Total Games Scanned: " << games_processed << std::endl;
        std::cout << "Valid Games Saved:   " << games_saved << std::endl;
    }
};

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    if (!fs::exists(OUT_DIR)) {
        try { fs::create_directories(OUT_DIR); } catch (...) {}
    }

    std::ifstream pgn_file(PGN_FILE_PATH, std::ios::binary);
    if (!pgn_file.is_open()) {
        std::cerr << "Error: PGN file not found." << std::endl;
        return 1;
    }

    std::ofstream out_file(OUTPUT_FILE);
    if (!out_file.is_open()) {
        std::cerr << "Error: Cannot write to output file." << std::endl;
        return 1;
    }

    out_file << "WhiteElo,BlackElo,Moves\n";

    std::cout << "Starting Extraction (Strict Mode: Piece+Dest Only)..." << std::endl;

    chess::pgn::StreamParser<1024> parser(pgn_file);
    TransformerDataVisitor visitor(out_file);
    
    parser.readGames(visitor);
    
    visitor.print_summary();
    
    return 0;
}