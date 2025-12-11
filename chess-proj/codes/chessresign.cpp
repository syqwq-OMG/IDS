#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <cmath>
#include <iomanip>
#include <filesystem>

// 引入依赖库
#include "chess.hpp"
#include "json.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

// ================= 配置区域 =================
const std::string PGN_FILE_PATH = "../dataset/lichess_db_standard_rated_2025-11.pgn"; 
const std::string OUTCOME_DIR = "../output/chess_resign"; 
const std::string FINAL_RESULT_FILE = OUTCOME_DIR + "/final_resignation_stats.json";
const int WINDOW_SIZE = 5; 
// ===========================================

using ResignationMatrix = std::map<int, std::map<int, int>>;

struct ResignationStats {
    ResignationMatrix white_resigned;
    ResignationMatrix black_resigned;
    int games_processed = 0;
    int resignation_games_found = 0;
};

// ---------------------------------------------------------
// 辅助函数：判断是否被将死 (Checkmate)
// 逻辑：当前方没有合法移动 (Legal Moves == 0) 且 处于被将军状态 (In Check)
// ---------------------------------------------------------
bool is_checkmate(chess::Board& board) {
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    return moves.empty() && board.inCheck();
}

// ---------------------------------------------------------
// 核心逻辑函数：计算盘面兵力 
// 修复点：sq 是 unsigned char，需通过除法计算 rank
// ---------------------------------------------------------
double calculate_material_score(const chess::Board& board, chess::Color color) {
    double score = 0.0;

    // 1. 常规子力价值
    score += board.pieces(chess::PieceType::QUEEN, color).count() * 9.0;
    score += board.pieces(chess::PieceType::ROOK, color).count() * 5.0;
    score += board.pieces(chess::PieceType::BISHOP, color).count() * 3.0;
    score += board.pieces(chess::PieceType::KNIGHT, color).count() * 3.0;

    // 2. 兵的特殊处理
    chess::Bitboard pawns = board.pieces(chess::PieceType::PAWN, color);

    while (pawns) {
        auto sq = pawns.pop(); // sq 是 0-63 的整数
        // 修复：直接通过整数除法获取 rank (0-7)
        int rank = static_cast<int>(sq) / 8;  

        // 白兵在 Rank 6 (第7行), 黑兵在 Rank 1 (第2行)
        if (color == chess::Color::WHITE && rank == 6) {
            score += 2.0; 
        } else if (color == chess::Color::BLACK && rank == 1) {
            score += 2.0;
        } else {
            score += 1.0;
        }
    }
    return score;
}

// ---------------------------------------------------------
// Visitor 类：严格按照 chess::pgn::Visitor 接口实现
// ---------------------------------------------------------
class ResignationVisitor : public chess::pgn::Visitor {
    ResignationStats& stats;
    chess::Board board;
    
    // 状态追踪
    std::deque<double> material_diff_window;
    std::string current_result; // 暂存 Header 中的 Result

public:
    ResignationVisitor(ResignationStats& s) : stats(s) {
        board.setFen(chess::constants::STARTPOS);
    }

    // 1. 开始新对局
    void startPgn() override {
        board.setFen(chess::constants::STARTPOS);
        material_diff_window.clear();
        material_diff_window.push_back(0.0);
        current_result = "*"; // 重置结果
    }

    // 2. 读取 Header (提取 Result)
    void header(std::string_view key, std::string_view value) override {
        if (key == "Result") {
            current_result = std::string(value);
        }
    }

    // 3. 开始读取移动 (空实现即可)
    void startMoves() override {
    }

    // 4. 读取每一步移动
    // 修复：必须叫 move，且参数包含 comment
    void move(std::string_view move_str, std::string_view comment) override {
        (void)comment; // 忽略注释
        
        chess::Move move = chess::uci::parseSan(board, move_str);
        board.makeMove(move);

        // --- 心理压力分析 ---
        double w_score = calculate_material_score(board, chess::Color::WHITE);
        double b_score = calculate_material_score(board, chess::Color::BLACK);
        double diff = w_score - b_score; 

        material_diff_window.push_back(diff);
        if (material_diff_window.size() > WINDOW_SIZE) {
            material_diff_window.pop_front();
        }
    }

    // 5. 对局结束
    // 修复：逻辑移至 endPgn，且使用 is_checkmate 辅助函数
    void endPgn() override {
        stats.games_processed++;

        // 排除平局
        if (current_result == "1/2-1/2" || current_result == "*") return;

        // 判定投降：分出胜负 且 不是将死
        // 注意：is_checkmate 需要 board 当前状态
        if (!is_checkmate(board)) {
            stats.resignation_games_found++;

            double sum_diff = 0.0;
            if (material_diff_window.empty()) return;
            
            for (double d : material_diff_window) sum_diff += d;
            double avg_diff = sum_diff / material_diff_window.size();

            int diff_key = static_cast<int>(std::round(avg_diff * 10));
            int turn_number = board.fullMoveNumber(); 

            if (current_result == "1-0") {
                // 白胜 -> 黑方投降
                stats.black_resigned[turn_number][diff_key]++;
            } else if (current_result == "0-1") {
                // 黑胜 -> 白方投降
                stats.white_resigned[turn_number][diff_key]++;
            }
        }

        if (stats.games_processed % 10000 == 0) {
            std::cout << "Processed: " << stats.games_processed 
                      << " | Resignations found: " << stats.resignation_games_found << "\r" << std::flush;
        }
    }
};

// ---------------------------------------------------------
// 主函数
// ---------------------------------------------------------
int main() {
    if (!fs::exists(OUTCOME_DIR)) {
        fs::create_directories(OUTCOME_DIR);
    }

    ResignationStats stats;
    std::ifstream pgn_file(PGN_FILE_PATH, std::ios::binary);

    if (!pgn_file.is_open()) {
        std::cerr << "Error: PGN file not found at " << PGN_FILE_PATH << std::endl;
        return 1;
    }

    std::cout << "Starting Resignation Analysis..." << std::endl;

    // 实例化 Visitor 并解析
    ResignationVisitor visitor(stats);
    chess::pgn::StreamParser<1024> parser(pgn_file); 
    
    parser.readGames(visitor);

    std::cout << "\nAnalysis Complete." << std::endl;
    std::cout << "Total Games: " << stats.games_processed << std::endl;
    std::cout << "Resignation Games: " << stats.resignation_games_found << std::endl;

    // 构建 JSON
    json j_root;
    
    j_root["meta_info"] = {
        {"source_file", PGN_FILE_PATH},
        {"total_games", stats.games_processed},
        {"resignation_count", stats.resignation_games_found},
        {"window_size", WINDOW_SIZE},
        {"note", "material_diff scaled by 10"}
    };

    auto& j_white = j_root["white_resigned"];
    for (const auto& [turn, diff_map] : stats.white_resigned) {
        for (const auto& [diff, count] : diff_map) {
            j_white[std::to_string(turn)][std::to_string(diff)] = count;
        }
    }

    auto& j_black = j_root["black_resigned"];
    for (const auto& [turn, diff_map] : stats.black_resigned) {
        for (const auto& [diff, count] : diff_map) {
            j_black[std::to_string(turn)][std::to_string(diff)] = count;
        }
    }

    std::ofstream out_file(FINAL_RESULT_FILE);
    out_file << std::setw(4) << j_root << std::endl;

    std::cout << "Saved to: " << FINAL_RESULT_FILE << std::endl;

    return 0;
}